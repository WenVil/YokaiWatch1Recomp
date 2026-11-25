// 13SentinelsRecomp entry: load modules, set up the kernel, run nn::init::Start on the main thread.
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "game_addresses.h"
#include "gamebuild.h"
#include "gamelib.h"
#include "paths.h"
#include "settings.h"
#include "hle.h"
#include "kernel.h"
#include "rando.h"

const char *g_romfs_dir;

// rtld normally passes these to nn::init::Start: run every module's init/fini arrays.
static void run_array(Cpu *c, uint64_t arr, uint64_t size) {
    for (uint64_t i = 0; i < size / 8; i++) {
        uint64_t fn = ((uint64_t *)arr)[i];
        if (fn) {
            LOG("  init_array[%llu] %llx\n", (unsigned long long)i, (unsigned long long)fn);
            guest_call(c, fn);
        }
    }
}

// nn::init::Start calls x2 right after nnosInitialize and x3 after nninitStartup, just before
// nnMain: SDK constructors run early, the game's run once nninitStartup has set up its heap.
// rtld runs a module's DT_INIT, which registers it (and its TLS template) with musl via
// __nnmusl_init_dso and then runs its init_array; fall back to the array if there is no DT_INIT.
static void module_init(Cpu *c, Module *m) {
    LOG("init %s (DT_INIT %llx, %llu init_array entries)\n", m->name, m->init, m->init_array_sz / 8);
    if (m->init) {
        LOG("  DT_INIT %llx\n", (unsigned long long)m->init);
        guest_call(c, m->init);
    }
    run_array(c, m->init_array, m->init_array_sz);
    LOG("init %s complete\n", m->name);
}

static bool g_direct_sdk_initialized;
static bool g_direct_main_initialized;

static void call_startup_symbol(Cpu *c, const char *name) {
    uint64_t fn = loader_find_symbol(name);
    if (!fn) {
        LOG("[boot] optional startup symbol %s not found\n", name);
        return;
    }
    LOG("[boot] %s at %llx\n", name, (unsigned long long)fn);
    guest_call(c, fn);
}

static void rtld_init_sdk(Cpu *c) {
    g_direct_sdk_initialized = true;
    module_init(c, &g_modules[1]);
    for (int i = 2; i < g_nmodules; i++) {
        if (!strcmp(g_modules[i].name, "subsdk0")) module_init(c, &g_modules[i]);
    }
}
static void rtld_init_main(Cpu *c) {
    g_direct_main_initialized = true;
    module_init(c, &g_modules[0]);
}

static uint64_t g_direct_start;
static uint64_t g_direct_main;
static void direct_start(Cpu *c) {
    uint64_t thread_handle = c->x[0];
    call_startup_symbol(c, "_init_libc0");
    uint64_t nnos_initialize = loader_find_symbol("nnosInitialize");
    if (nnos_initialize) {
        c->x[0] = thread_handle;
        c->x[1] = 0;
        LOG("[boot] nnosInitialize at %llx\n", (unsigned long long)nnos_initialize);
        guest_call(c, nnos_initialize);
    }
    call_startup_symbol(c, "_init_libc1");
    call_startup_symbol(c, "nninitInitializeSdkModule");
    guest_call(c, g_direct_start);
    LOG("[boot] nninitStartup returned\n");
    call_startup_symbol(c, "_init_libc2");
    if (!g_direct_sdk_initialized) rtld_init_sdk(c);
    if (!g_direct_main_initialized) rtld_init_main(c);
    if (g_direct_main) {
        LOG("[boot] nnMain entered at %llx\n", (unsigned long long)g_direct_main);
        guest_call(c, g_direct_main);
    }
}

// Claim the guest address range before anything else can map there. The host allocator places
// regions at random addresses in the same window; on a collision, re-exec for a fresh layout.
// LARECOMP_REEXEC_FORCE=1 (test): the first process skips the reservation once and starts again, as when the range is
// taken (the new process has LARECOMP_REEXEC set and reserves normally).
#define REEXEC_FORCED "forced by LARECOMP_REEXEC_FORCE"
__attribute__((constructor(101))) static void reserve_guest_space(void) {
    const char *force = getenv("LARECOMP_REEXEC_FORCE");
    bool forced = force && *force && strcmp(force, "0") && !getenv("LARECOMP_REEXEC");
    if (forced) setenv("LARECOMP_REEXEC_WHY", REEXEC_FORCED, 1);
    else if (plat_reserve_guest(GUEST_LO, GUEST_HI)) return;
    else setenv("LARECOMP_REEXEC_WHY", "", 0);  // (the platform may have said what holds the range; else nothing more)
    const char *tries = getenv("LARECOMP_REEXEC");
    int n = tries ? atoi(tries) : 0;
    if (n >= 20) {
        fprintf(stderr, "FATAL: guest address range %llx-%llx is not free\n", GUEST_LO, GUEST_HI);
        exit(1);
    }
    char buf[16];
    snprintf(buf, sizeof buf, "%d", n + 1);
    setenv("LARECOMP_REEXEC", buf, 1);
    plat_exec(NULL);
    perror("re-exec");
    exit(1);
}

// ------------------------------------------------------------------ boot / in-place reboot
// Switching seeds (or vanilla) and going back to the start screen reboot the game inside this process: the window,
// GPU device, settings and shader caches stay. rt_request_reboot (menu, any thread) makes ui_run return on the
// main thread, which then: stops every guest thread (kernel_stop_threads: each unwinds at its next svc / HLE call /
// wait), resets the renderer (textures, address-keyed caches) and all per-boot host state (RT_ON_RESET hooks:
// services, HLE features, seed data, audio), empties the kernel, wipes guest memory, and boots again.
static _Atomic int g_reboot;
void rt_request_reboot(int kind) {
    LOG("[reboot] requested (%s)\n", kind == RT_REBOOT_TITLE ? "start screen" : "boot");
    g_reboot = kind;
}
int rt_reboot_requested(void) { return g_reboot; }

// LARECOMP_REBOOT_AT=<frame>:<seed | vanilla | title>[,...] (test): at that presented frame, switch to the seed /
// vanilla in place, or go back to the start screen (with LARECOMP_LAUNCHER_AUTOPLAY it plays the saved choice).
// Frames count on across reboots. Called by the presenter every frame.
void reboot_test_frame(uint64_t frame) {
    static const char *next;
    static int init;
    if (!init) { init = 1; next = getenv("LARECOMP_REBOOT_AT"); }
    long at;
    char what[128];
    if (!next || !*next || sscanf(next, "%ld:%127[^,]", &at, what) != 2 || (long)frame < at) return;
    next = strchr(next, ',');
    if (next) next++;
    if (!strcmp(what, "title")) { rt_request_reboot(RT_REBOOT_TITLE); return; }
    // (a test run: the next boot's seed without saving it - larecomp.ini is the player's choice)
    strlcpy(g_settings.rando_seed, strcmp(what, "vanilla") ? what : "", sizeof g_settings.rando_seed);
    unsetenv("LARECOMP_RANDO_SEED");
    rt_request_reboot(RT_REBOOT_GAME);
}

#ifdef __APPLE__
extern const ResetFn __start_resets[] __asm__("section$start$__DATA$__rt_reset");
extern const ResetFn __stop_resets[] __asm__("section$end$__DATA$__rt_reset");
#elif defined(__linux__)
extern const ResetFn __start_resets[], __stop_resets[];  // (ELF: defined by the linker)
#else
// PE/COFF: .rst$a < .rst$m (entries, hle.h) < .rst$z (the linker sorts by the text after '$')
__attribute__((section(".rst$a"), used)) static const ResetFn __start_resets[1];
__attribute__((section(".rst$z"), used)) static const ResetFn __stop_resets[1];
#endif

void backend_reset(void);             // renderer: GPU idle, game textures and address-keyed caches dropped
void backend_guest_memory_reset(void);  // renderer: guest memory was replaced (re-wrap the heap)
void backend_clear_window(void);      // a black frame while the game is down
void backend_quit(void);              // quitting: the render thread stopped, the GPU idle, caches kept (before exit())

static void reboot_idle(void) {  // (main thread, while guest threads stop) keep the window alive
    SDL_Event e;
    while (SDL_PollEvent(&e))
        if (e.type == SDL_EVENT_QUIT) { LOG("window closed\n"); backend_quit(); exit(0); }
}

static void teardown(void) {
    uint64_t t0 = plat_now_ns();
    kernel_stop_threads(reboot_idle);
    backend_clear_window();
    backend_reset();
    const ResetFn *start = __start_resets, *stop = __stop_resets;
    __asm__("" : "+r"(start), "+r"(stop));  // separate objects: keep the compiler from reasoning about them
    int n = 0;
    for (const ResetFn *p = start; p < stop; p++)
        if (*p) { (*p)(); n++; }
    kernel_reset();
    if (!plat_reset_guest(GUEST_LO, GUEST_HI)) FATAL("cannot reset guest memory\n");
    backend_guest_memory_reset();
    g_nmodules = 0;
    g_reboot = RT_REBOOT_NONE;
    LOG("[reboot] game torn down in %llums (%d reset hooks)\n", (unsigned long long)((plat_now_ns() - t0) / 1000000), n);
}

static char s_boot_npdm[1200];
static bool s_interactive_loading = false;
bool rt_interactive_loading(void) { return s_interactive_loading; }

void boot_prepare(const char *root, int *stage) {
    char exefs[1024], romfs[1024];
    if (stage) __atomic_store_n(stage, 0, __ATOMIC_RELEASE);
    if (!rt_find_subdir_icase(root, "exefs", exefs, sizeof exefs))
        snprintf(exefs, sizeof exefs, "%s/exefs", root);
    if (!rt_find_subdir_icase(root, "romfs", romfs, sizeof romfs))
        snprintf(romfs, sizeof romfs, "%s/romfs", root);
    snprintf(s_boot_npdm, sizeof s_boot_npdm, "%s/main.npdm", exefs);
    free((void *)g_romfs_dir);
    g_romfs_dir = strdup(romfs);

#ifdef __linux__
    {   // the window and graphics device first (normally made by the start screen or ui_run), so the guest heap can be
        // the driver's memory before any game thread runs (runtime/nvn/vulkan/exec.c backend_guest_heap_prepare)
        extern void ui_window_init(void);
        extern void backend_guest_heap_prepare(void);
        ui_window_init();
        backend_guest_heap_prepare();
    }
#endif
    char why[512];
    if (!game_version_select(root, why, sizeof why)) FATAL("%s\n", why);  // (the address table: game_addresses.h)
    if (stage) __atomic_store_n(stage, 1, __ATOMIC_RELEASE);
    if (!gamelib_load(root, why, sizeof why)) FATAL("%s\n", why);
    if (stage) __atomic_store_n(stage, 2, __ATOMIC_RELEASE);
    loader_load(exefs);
    rando_boot();  // the chosen seed's data patches go into the freshly loaded module
    if (stage) __atomic_store_n(stage, 3, __ATOMIC_RELEASE);
    settings_load();
    kernel_init();
    dispatch_init();
}

void boot_launch_guest(int *stage) {
    if (stage) __atomic_store_n(stage, 4, __ATOMIC_RELEASE);
    uint32_t stack_size = 0x100000;
    int prio = 44;
    FILE *f = plat_fopen(s_boot_npdm, "rb");
    if (f) {
        uint8_t meta[0x80];
        if (fread(meta, 1, sizeof meta, f) == sizeof meta && !memcmp(meta, "META", 4)) {
            prio = meta[0xE];
            stack_size = *(uint32_t *)(meta + 0x1C);
        }
        fclose(f);
    }
    LOG("main thread: prio %d, stack 0x%x\n", prio, stack_size);

    uint64_t start = loader_find_symbol("_ZN2nn4init5StartEmmPFvvES2_");
    if (!start) {
        start = loader_find_symbol("nninitStartup");
        if (!start) {
            loader_log_symbols_containing("Start");
            FATAL("nn::init::Start or nninitStartup not found\n");
        }
        LOG("[boot] using nninitStartup at %llx\n", (unsigned long long)start);
        g_direct_start = start;
        g_direct_main = loader_find_symbol("nnMain");
        if (!g_direct_main) LOG("[boot] nnMain export not found; stopping after nninitStartup\n");
        start = hle_register("<direct_start>", direct_start);
    }
    uint64_t init_fn = hle_register("<rtld_init_sdk>", rtld_init_sdk);
    uint64_t fini_fn = hle_register("<rtld_init_main>", rtld_init_main);
    KThread *t = kernel_create_main_thread(start, stack_size, prio);
    kernel_run_main(t, t->handle, 0, init_fn, fini_fn);  // nn::init::Start(thread_handle, args_region, init, fini)
}

static void boot(const char *root) {
    boot_prepare(root, NULL);
    boot_launch_guest(NULL);
}

// The log's first line after the log's own: this build and the machine, for bug reports. Nothing that identifies the
// player (platform.h plat_system_info). Once per session: a restart of this process (the start screen re-executing
// itself for the game) appends to the same log and inherits LASRECOMP_INFO_LOGGED.
static void log_startup_info(void) {
    if (getenv("LASRECOMP_INFO_LOGGED")) return;
    setenv("LASRECOMP_INFO_LOGGED", "1", 1);
    extern const char *larecomp_version(void);  // runtime/ui/launcher.mm
    PlatSysInfo si = {0};
    plat_system_info(&si);
    char cores[96] = "";
    if (si.cores > 0) {
        int n = snprintf(cores, sizeof cores, " %d cores", si.cores);
        if (si.cores_detail[0]) n += snprintf(cores + n, sizeof cores - n, " (%s)", si.cores_detail);
        if (si.threads > si.cores) snprintf(cores + n, sizeof cores - n, " / %d threads", si.threads);
    } else if (si.threads > 0) snprintf(cores, sizeof cores, " %d threads", si.threads);
    unsigned long long tenths = (si.ram * 10 + (1ull << 29)) >> 30;  // (GB to a tenth; whole when it is: 64 GB, 14.5 GB)
    char ram[32] = "RAM unknown";
    if (si.ram && tenths % 10) snprintf(ram, sizeof ram, "%llu.%llu GB RAM", tenths / 10, tenths % 10);
    else if (si.ram) snprintf(ram, sizeof ram, "%llu GB RAM", tenths / 10);
    fprintf(stderr, "[info] 13SentinelsRecomp %s, %s, %s%s, %s%s%s\n", larecomp_version(),
            si.os[0] ? si.os : "unknown OS", si.cpu[0] ? si.cpu : "unknown CPU", cores, ram,
            si.package ? ", " : "", si.package ? si.package : "");
}

static void cli_progress(double frac, const char *step) {
    static double last_frac = -1.0;
    static char last_step[128] = "";
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    const char *s = (step && *step) ? step : "";
    double diff = frac - last_frac;
    if (diff < 0.0) diff = -diff;
    bool step_chg = strcmp(s, last_step) != 0;
    bool done_dup = (frac >= 1.0 && last_frac >= 1.0 && !step_chg);
    if (done_dup || (last_frac >= 0.0 && diff < 0.001 && !step_chg && frac < 1.0)) return;
    last_frac = frac;
    snprintf(last_step, sizeof last_step, "%s", s);
    printf("progress %.3f %s\n", frac, s);
    fflush(stdout);
}

static bool has_cpk_icase(const char *romfs_dir, const char *upper_name, const char *lower_name) {
    char p[1200];
    snprintf(p, sizeof p, "%s/%s", romfs_dir, upper_name);
    if (plat_path_exists(p)) return true;
    snprintf(p, sizeof p, "%s/%s", romfs_dir, lower_name);
    return plat_path_exists(p);
}

static int run_build_only(const char *root) {
    char rom_dir[1100];
    const GameVersion *gv = game_version_of(root);
    if (!gv) {
        fprintf(stderr, "[build-only] unsupported or missing exefs/main in: %s\n", root);
        return 1;
    }
    if (!rt_find_subdir_icase(root, "romfs", rom_dir, sizeof rom_dir)) {
        fprintf(stderr, "[build-only] missing romfs directory in: %s\n", root);
        return 1;
    }
    if (!strcmp(gv->key, "v13_asia_100")) {
        if (!has_cpk_icase(rom_dir, "ROBO_CN.CPK", "robo_cn.cpk")) {
            fprintf(stderr, "[build-only] missing core archive ROBO_CN.CPK in: %s\n", rom_dir);
            return 1;
        }
    } else if (!strcmp(gv->key, "v13_us_100")) {
        if (!has_cpk_icase(rom_dir, "ROBO_US.CPK", "robo_us.cpk")) {
            fprintf(stderr, "[build-only] missing core archive ROBO_US.CPK in: %s\n", rom_dir);
            return 1;
        }
    } else if (!strcmp(gv->key, "v13_us_110")) {
        if (!has_cpk_icase(rom_dir, "ROBO_US.CPK", "robo_us.cpk") ||
            !has_cpk_icase(rom_dir, "ROBO_PATCH_US.CPK", "robo_patch_us.cpk")) {
            fprintf(stderr, "[build-only] incomplete US/EU 1.10 romfs (ROBO_US.CPK / ROBO_PATCH_US.CPK) in: %s\n", rom_dir);
            return 1;
        }
    }
    cli_progress(0.0, "Validating game files...");
    if (gamelib_present(root)) {
        cli_progress(1.0, "Already built");
        return 0;
    }
    char cache_buf[1100], why[512] = "";
    const char *cache_dir = gamebuild_cache_dir(root, cache_buf, sizeof cache_buf);
    int jobs = plat_cpu_count();
    if (jobs < 1) jobs = 4;
    if (jobs > 16) jobs = 16;
    GameBuildOpts opts = {.progress = cli_progress, .jobs = jobs, .cancelled = NULL};
    if (!gamebuild_run_into(root, cache_dir, &opts, why, sizeof why)) {
        fprintf(stderr, "[build-only] build failed: %s\n", why[0] ? why : "unknown error");
        return 1;
    }
    return 0;
}

static const char *pick_game_root(const char *arg_root, bool has_arg_root) {
    if (has_arg_root) return arg_root;
    if (plat_is_dir("extracted")) return "extracted";
    if (g_settings.game_dir[0]) return g_settings.game_dir;
    return "extracted";
}

int main(int argc, char **argv) {
    plat_init();
    log_startup_info();
    if (getenv("LARECOMP_REEXEC")) {  // (reserve_guest_space started this process again)
        const char *why = getenv("LARECOMP_REEXEC_WHY"), *n = getenv("LARECOMP_REEXEC");
        if (!why) LOG("[boot] started again (try %s) (LARECOMP_REEXEC set by the caller)\n", n);
        else if (!strcmp(why, REEXEC_FORCED)) LOG("[boot] started again (try %s): %s\n", n, why);
        else LOG("[boot] started again (try %s): the guest address range was in use%s%s\n", n, *why ? ": " : "", why);
    }
#ifdef _WIN32
    setvbuf(stderr, NULL, _IONBF, 0);  // the Windows CRT treats _IOLBF as full buffering
#else
    setvbuf(stderr, NULL, _IOLBF, 0);
#endif
    // LARECOMP_NO_LAUNCHER=1 (tools/game_slot.sh sets it for every test run): never the start screen - it can
    // delete game files, and a test's extracted/ may be a link to someone else's. Where it would come up (a forced
    // start screen, "Title screen" in game) the app exits instead. =0: off (start screen tests, scratch data root).
    const char *nl = getenv("LARECOMP_NO_LAUNCHER");
    bool no_launcher = nl && *nl && strcmp(nl, "0");
    bool has_input = getenv("LARECOMP_INPUT") != NULL;
    bool play_flag = false, build_only = false, has_arg_root = false;
    bool env_force_launcher = getenv("LARECOMP_FORCE_LAUNCHER") != NULL;
    static char arg_root[1024];
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--play")) play_flag = true;
        else if (!strcmp(argv[i], "--build-only")) build_only = true;
        else if (!strcmp(argv[i], "--game-dir") && i + 1 < argc) {
            const char *val = argv[++i];
            has_arg_root = true;
            if (!realpath(val, arg_root)) snprintf(arg_root, sizeof arg_root, "%s", val);
        } else if (!strncmp(argv[i], "--game-dir=", 11)) {
            const char *val = argv[i] + 11;
            has_arg_root = true;
            if (!realpath(val, arg_root)) snprintf(arg_root, sizeof arg_root, "%s", val);
        } else if (argv[i][0] != '-') {
            has_arg_root = true;
            if (!realpath(argv[i], arg_root)) snprintf(arg_root, sizeof arg_root, "%s", argv[i]);
        }
    }
    if (build_only) rt_set_headless(true);
    rt_paths_init();  // the data root becomes the working directory (runtime/paths.h)
    settings_load();  // loads game_dir and skip_launcher before launcher selection
    if (build_only) {
        return run_build_only(pick_game_root(arg_root, has_arg_root));
    }
    bool shift_force = !no_launcher && !play_flag && !has_input && plat_shift_held();
    bool skip_launcher = !env_force_launcher && !shift_force && (no_launcher || has_input || play_flag);
    if (!skip_launcher && !env_force_launcher && !shift_force && g_settings.skip_launcher) {
        const char *cand = pick_game_root(arg_root, has_arg_root);
        char rom_tmp[1100];
        if (game_version_of(cand) && gamelib_present(cand) && rt_find_subdir_icase(cand, "romfs", rom_tmp, sizeof rom_tmp))
            skip_launcher = true;
    }
    const bool allow_loading_splash = !no_launcher && !has_input && !play_flag && !getenv("LARECOMP_HIDDEN");
    for (bool title = !skip_launcher;;) {
        const char *root = pick_game_root(arg_root, has_arg_root);
        if (title && no_launcher) {
            fprintf(stderr, "[main] LARECOMP_NO_LAUNCHER is set: not showing the start screen - exiting\n");
            exit(3);
        }
        if (title) {  // start screen (runtime/ui/launcher.mm): game files setup, settings, and animated loading
            extern void launcher_run(const char *root, bool *out_prepared);
            bool prepared = false;
            launcher_run(root, &prepared);
            root = pick_game_root(arg_root, has_arg_root);
            s_interactive_loading = allow_loading_splash;
            if (prepared) boot_launch_guest(NULL);
            else boot(root);
        } else if (allow_loading_splash) {
            extern void launcher_loading_boot(const char *root);
            s_interactive_loading = true;
            launcher_loading_boot(root);
            boot_launch_guest(NULL);
        } else {
            s_interactive_loading = false;
            boot(root);
        }
        int kind = ui_run();  // window and input on the main thread; returns when a reboot is requested
        teardown();
        title = kind == RT_REBOOT_TITLE;
    }
}
