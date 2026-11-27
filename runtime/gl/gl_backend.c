// Native OpenGL 4.5 Compatibility window, context handoff, event pump, and frame dump backend.
#include "gl_backend.h"
#include "gl_imgui.h"

#include <SDL3/SDL.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "../cpu.h"
#include "../game_addresses.h"
#include "../paths.h"
#include "../platform.h"
#include "../rt.h"
#include "../settings.h"

void input_apply_background(void);
void input_load_user_mappings(void);
void input_cursor_event(const SDL_Event *e);
void input_handle_event(const SDL_Event *e);
void input_rumble_poll(void);
void input_rumble_stop(void);
void input_led_poll(void);
void input_led_stop(void);
bool input_user_blocked(const SDL_Event *e);

static SDL_Window *s_window;
static SDL_GLContext s_gl_ctx;
static atomic_bool s_win_focused = true;
static atomic_bool s_quitting;
static atomic_uint_fast64_t s_frames;
static int s_swap_interval = 1;
static atomic_flag s_quit_once = ATOMIC_FLAG_INIT;
static atomic_int s_win_w = 1280;
static atomic_int s_win_h = 720;
static atomic_int s_pix_w = 1280;
static atomic_int s_pix_h = 720;
static atomic_int s_density_x1000 = 1000;
static atomic_int s_disp_scale_x1000 = 1000;
static atomic_bool s_loading = false;
static atomic_int s_pending_swap_interval = -99;

extern bool menu_handle_event(const SDL_Event *e);
extern void menu_apply_window_requests(void *sdl_window);
extern int rt_reboot_kind(void);
extern bool movie_is_playing(void);
extern volatile uint64_t g_vsync_count;

#define YW1_JP130_PACING_OFF 0x8c3d60ull

static bool yw1_is_jp_130(bool warn_once) {
    const GameVersion *gv = game_version();
    if (gv && gv->key && strcmp(gv->key, "vyw1_jp_v1_3_0") == 0 &&
        gv->build_id && strncmp(gv->build_id, "B6E172353E696E65", 16) == 0) {
        return true;
    }
    if (warn_once) {
        static atomic_flag s_warned = ATOMIC_FLAG_INIT;
        if (!atomic_flag_test_and_set(&s_warned)) {
            LOG("[60fps] build_id guard: skipping 0x70008c3d60 access for key=%s build_id=%s (expected vyw1_jp_v1_3_0 / B6E172353E696E65)\n",
                (gv && gv->key) ? gv->key : "null",
                (gv && gv->build_id) ? gv->build_id : "null");
        }
    }
    return false;
}

static volatile uint8_t *yw1_v130_pacing_base(bool warn_unsupported) {
    if (!yw1_is_jp_130(warn_unsupported)) return NULL;
    uint64_t lo = 0, hi = 0;
    if (!plat_guest_bounds(&lo, &hi)) return NULL;
    uint64_t addr = MAIN_BASE + YW1_JP130_PACING_OFF;
    if (addr < lo || addr + 0x1338ull > hi) return NULL;
    uint64_t vt = *(const volatile uint64_t *)(uintptr_t)addr;
    if (vt < MAIN_BASE + 0x600000ull || vt >= MAIN_BASE + 0x2000000ull) return NULL;
    return (volatile uint8_t *)(uintptr_t)addr;
}

static bool exp_60fps_enabled(void) {
    static int s_exp = -1;
    if (s_exp < 0) {
        const char *env = getenv("YW1_EXP_60FPS");
        s_exp = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
    }
    return s_exp == 1;
}

static bool probe_60fps_enabled(void) {
    static int s_probe = -1;
    if (s_probe < 0) {
        const char *env = getenv("YW1_60FPS_PROBE");
        s_probe = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
    }
    return s_probe == 1;
}

static uint64_t s_probe_t0_ns;
static uint64_t s_probe_last_t_ns;
static uint64_t s_probe_movie_start_frame;
static double s_probe_movie_start_ms = -1.0;
static double s_probe_movie_end_ms = -1.0;
static bool s_probe_prev_movie;
static uint64_t s_probe_logic_frames;
static uint32_t s_probe_last_prev_vsync = UINT32_MAX;
static uint32_t s_probe_last_tgt = UINT32_MAX;
static uint32_t s_probe_last_base = UINT32_MAX;
static int s_probe_edge_logs;
static uint64_t s_exp_60fps_overrides;

static void probe_log_snapshot(uint64_t frame, uint64_t now_ns, const char *reason, volatile uint8_t *base) {
    double t_ms = s_probe_t0_ns ? (double)(now_ns - s_probe_t0_ns) / 1e6 : 0.0;
    int mov = movie_is_playing() ? 1 : 0;
    uint64_t vs = __atomic_load_n(&g_vsync_count, __ATOMIC_RELAXED);
    if (base) {
        uint32_t cur = *(const volatile uint32_t *)(base + 0x7c8);
        uint32_t prev = *(const volatile uint32_t *)(base + 0x7cc);
        uint32_t base_v = *(const volatile uint32_t *)(base + 0x7d0);
        uint32_t tgt_v = *(const volatile uint32_t *)(base + 0x7d4);
        uint32_t base_fps = *(const volatile uint32_t *)(base + 0x7d8);
        float ratio = *(const volatile float *)(base + 0x7dc);
        uint8_t flag = *(const volatile uint8_t *)(base + 0x7e0);
        float dt_out = *(const volatile float *)(base + 0x131c);
        float time_scale = *(const volatile float *)(base + 0x1328);
        LOG("[60fps-probe] tag=%s frame=%llu logic=%llu t_ms=%.1f vsync=%llu movie=%d base=%#llx "
            "cur(+7c8)=%u prev(+7cc)=%u base_vsync(+7d0)=%u target_vsync(+7d4)=%u base_fps(+7d8)=%u "
            "ratio(+7dc)=%.3f flag(+7e0)=%u dt(+131c)=%.3f mul(+1328)=%.3f overrides=%llu\n",
            reason, (unsigned long long)frame, (unsigned long long)s_probe_logic_frames, t_ms,
            (unsigned long long)vs, mov, (unsigned long long)(uintptr_t)base,
            cur, prev, base_v, tgt_v, base_fps,
            (double)ratio, (unsigned)flag, (double)dt_out, (double)time_scale,
            (unsigned long long)s_exp_60fps_overrides);
    } else {
        LOG("[60fps-probe] tag=%s frame=%llu t_ms=%.1f vsync=%llu movie=%d base=unready\n",
            reason, (unsigned long long)frame, t_ms, (unsigned long long)vs, mov);
    }
}

static int vsync_interval_from_settings(void) {
    switch (g_settings.vsync_mode) {
    case VSYNC_FIFO: return 1;
    case VSYNC_IMMEDIATE: return 0;
    case VSYNC_MAILBOX:
    default: return -1;
    }
}

static void target_size_from_settings(int *w_out, int *h_out) {
    switch (g_settings.output_res) {
    case OUT_RES_720P:  *w_out = 1280; *h_out = 720;  return;
    case OUT_RES_1080P: *w_out = 1920; *h_out = 1080; return;
    case OUT_RES_1440P: *w_out = 2560; *h_out = 1440; return;
    case OUT_RES_4K:    *w_out = 3840; *h_out = 2160; return;
    case OUT_RES_AUTO:
    default:            *w_out = 1280; *h_out = 720;  return;
    }
}

static void update_cached_metrics_main_thread(SDL_Window *win) {
    if (!win) return;
    int ww = 1280, wh = 720, pw = 1280, ph = 720;
    SDL_GetWindowSize(win, &ww, &wh);
    SDL_GetWindowSizeInPixels(win, &pw, &ph);
    float pd = SDL_GetWindowPixelDensity(win);
    float ds = SDL_GetWindowDisplayScale(win);
    if (ww <= 0) ww = 1280;
    if (wh <= 0) wh = 720;
    if (pw <= 0) pw = ww;
    if (ph <= 0) ph = wh;
    if (pd < 0.5f || pd > 8.0f) pd = (float)pw / (float)ww;
    if (pd < 0.5f) pd = 1.0f;
    if (ds < 0.5f || ds > 8.0f) ds = pd;
    atomic_store_explicit(&s_win_w, ww, memory_order_relaxed);
    atomic_store_explicit(&s_win_h, wh, memory_order_relaxed);
    atomic_store_explicit(&s_pix_w, pw, memory_order_relaxed);
    atomic_store_explicit(&s_pix_h, ph, memory_order_relaxed);
    atomic_store_explicit(&s_density_x1000, (int)(pd * 1000.0f + 0.5f), memory_order_relaxed);
    atomic_store_explicit(&s_disp_scale_x1000, (int)(ds * 1000.0f + 0.5f), memory_order_relaxed);
}

void gl_backend_refresh_metrics(void) {
    update_cached_metrics_main_thread(s_window);
}

float gl_backend_display_scale(void) {
    return (float)atomic_load_explicit(&s_disp_scale_x1000, memory_order_relaxed) / 1000.0f;
}

bool gl_backend_is_active(void) {
    const char *env = getenv("YW1_GL_BACKEND");
    if (env && *env && strcmp(env, "0") != 0) return true;
    const GameVersion *gv = game_version();
    return gv && strncmp(gv->key, "vyw1_", 5) == 0;
}

static bool frame_should_dump(uint64_t frame) {
    const char *env = getenv("YW1_DUMP_FRAMES");
    if (!env || !*env) return false;
    const char *p = env;
    while (*p) {
        char *end = NULL;
        unsigned long long target = strtoull(p, &end, 10);
        if (end == p) break;
        if (target == frame) return true;
        p = (*end == ',') ? end + 1 : end;
    }
    return false;
}

static void dump_frame_bmp(uint64_t frame) {
    if (!s_window) return;
    int pw = 1920, ph = 1080;
    gl_hle_get_virtual_fbo_size(&pw, &ph);

    int row_stride = (pw * 3 + 3) & ~3;
    size_t img_bytes = (size_t)row_stride * (size_t)ph;
    uint8_t *bgr = malloc(img_bytes);
    if (!bgr) return;
    memset(bgr, 0, img_bytes);
    gl_hle_read_virtual_fbo_rgb(pw, ph, bgr);

    const char *dir = getenv("YW1_DUMP_DIR");
    if (!dir || !*dir) dir = "screenshots";
    plat_mkdir_p(dir);

    char path[1200];
    snprintf(path, sizeof path, "%s/frame_%llu.bmp", dir, (unsigned long long)frame);
    FILE *f = plat_fopen(path, "wb");
    if (f) {
        uint32_t file_size = 54u + (uint32_t)img_bytes;
        uint8_t hdr[54] = {0};
        hdr[0] = 'B';
        hdr[1] = 'M';
        hdr[2] = (uint8_t)(file_size);
        hdr[3] = (uint8_t)(file_size >> 8);
        hdr[4] = (uint8_t)(file_size >> 16);
        hdr[5] = (uint8_t)(file_size >> 24);
        hdr[10] = 54;
        hdr[14] = 40;
        hdr[18] = (uint8_t)(pw);
        hdr[19] = (uint8_t)(pw >> 8);
        hdr[20] = (uint8_t)(pw >> 16);
        hdr[21] = (uint8_t)(pw >> 24);
        hdr[22] = (uint8_t)(ph);
        hdr[23] = (uint8_t)(ph >> 8);
        hdr[24] = (uint8_t)(ph >> 16);
        hdr[25] = (uint8_t)(ph >> 24);
        hdr[26] = 1;
        hdr[28] = 24;
        fwrite(hdr, 1, sizeof hdr, f);
        fwrite(bgr, 1, img_bytes, f);
        fclose(f);
        LOG("[gl] dumped frame %llu -> %s (%dx%d)\n", (unsigned long long)frame, path, pw, ph);
    } else {
        LOG("[gl] failed to open %s for frame dump\n", path);
    }
    free(bgr);
}

static void gl_quick_exit_handler(void) {
    gl_backend_quit();
    fflush(stdout);
    fflush(stderr);
#if defined(_WIN32) || defined(__linux__)
    plat_terminate();
#endif
}

void gl_backend_window_init(void) {
    if (s_window) return;
    atexit(gl_quick_exit_handler);
    settings_load();
    rt_set_speed_pct((uint32_t)(g_settings.speed_pct >= 100 ? g_settings.speed_pct : 100));
    input_apply_background();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) FATAL("SDL_Init: %s\n", SDL_GetError());
    input_load_user_mappings();

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    int win_w = 1280, win_h = 720;
    target_size_from_settings(&win_w, &win_h);
    if (getenv("LARECOMP_WINDOW")) sscanf(getenv("LARECOMP_WINDOW"), "%dx%d", &win_w, &win_h);

    SDL_WindowFlags wflags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (getenv("LARECOMP_HIDDEN")) wflags |= SDL_WINDOW_HIDDEN;

    s_window = SDL_CreateWindow("Yo-kai Watch 1 (OpenGL 4.5)", win_w, win_h, wflags);
    if (!s_window) FATAL("SDL_CreateWindow (OPENGL): %s\n", SDL_GetError());
    rt_window_icon(s_window);
    if (!getenv("LARECOMP_WINDOW") && !getenv("LARECOMP_HIDDEN")) {
        gl_backend_apply_window_settings();
    } else {
        atomic_store_explicit(&s_pending_swap_interval, vsync_interval_from_settings(), memory_order_release);
        update_cached_metrics_main_thread(s_window);
    }
    atomic_store_explicit(&s_win_focused, (SDL_GetWindowFlags(s_window) & SDL_WINDOW_INPUT_FOCUS) != 0,
                          memory_order_relaxed);

    s_gl_ctx = SDL_GL_CreateContext(s_window);
    if (!s_gl_ctx) FATAL("SDL_GL_CreateContext (4.5 Compatibility): %s\n", SDL_GetError());
    gl_backend_apply_pending_vsync();

    // Pre-resolve all host GL procedures while the context is temporarily current on the main thread,
    // so guest eglGetProcAddress calls succeed even before eglMakeCurrent on the render thread.
    gl_hle_init_procs();

    // Unbind from main thread immediately so the guest render thread can bind it exclusively.
    if (!SDL_GL_MakeCurrent(s_window, NULL)) {
        LOG("[gl] warning: SDL_GL_MakeCurrent(NULL) failed: %s\n", SDL_GetError());
    }
    LOG("[gl] SDL3 OpenGL window ready (%dx%d), context unbound from main thread\n",
        atomic_load_explicit(&s_pix_w, memory_order_relaxed),
        atomic_load_explicit(&s_pix_h, memory_order_relaxed));
}

void gl_backend_init(void) { gl_backend_window_init(); }
void gl_backend_reset(void) {
    atomic_store_explicit(&s_frames, 0, memory_order_release);
    s_probe_t0_ns = 0;
    s_probe_last_t_ns = 0;
    s_probe_movie_start_frame = 0;
    s_probe_movie_start_ms = -1.0;
    s_probe_movie_end_ms = -1.0;
    s_probe_prev_movie = false;
    s_probe_logic_frames = 0;
    s_probe_last_prev_vsync = UINT32_MAX;
    s_probe_last_tgt = UINT32_MAX;
    s_probe_last_base = UINT32_MAX;
    s_probe_edge_logs = 0;
    s_exp_60fps_overrides = 0;
}
void gl_backend_guest_memory_reset(void) {}
void gl_backend_clear_window(void) {}

void gl_backend_quit(void) {
    if (atomic_flag_test_and_set(&s_quit_once)) return;
    atomic_store_explicit(&s_quitting, true, memory_order_release);
    if (probe_60fps_enabled()) {
        uint64_t total_frames = atomic_load_explicit(&s_frames, memory_order_acquire);
        double elapsed_s = (s_probe_t0_ns && s_probe_last_t_ns > s_probe_t0_ns)
                               ? (double)(s_probe_last_t_ns - s_probe_t0_ns) / 1e9
                               : 0.0;
        double swap_fps = (elapsed_s > 0.0 && total_frames > 0) ? (double)total_frames / elapsed_s : 0.0;
        double logic_fps = (elapsed_s > 0.0 && s_probe_logic_frames > 0) ? (double)s_probe_logic_frames / elapsed_s : 0.0;
        LOG("[60fps-probe] SUMMARY exp_60fps=%d total_swaps=%llu logic_frames=%llu elapsed_s=%.3f "
            "swap_fps=%.2f logic_fps=%.2f overrides=%llu swap_interval=%d "
            "movie_start_frame=%llu movie_start_ms=%.1f movie_end_ms=%.1f\n",
            exp_60fps_enabled() ? 1 : 0, (unsigned long long)total_frames,
            (unsigned long long)s_probe_logic_frames, elapsed_s, swap_fps, logic_fps,
            (unsigned long long)s_exp_60fps_overrides, s_swap_interval,
            (unsigned long long)s_probe_movie_start_frame, s_probe_movie_start_ms, s_probe_movie_end_ms);
    }
    input_shutdown();
    gl_hle_log_summary();
    gl_imgui_overlay_log_summary();
}

SDL_Window *gl_backend_window(void) { return s_window; }
void *gl_backend_context(void) { return (void *)s_gl_ctx; }
bool gl_backend_window_focused(void) { return atomic_load_explicit(&s_win_focused, memory_order_relaxed); }

void gl_backend_get_cached_metrics(int *win_w, int *win_h, int *pix_w, int *pix_h, float *density) {
    if (win_w) *win_w = atomic_load_explicit(&s_win_w, memory_order_relaxed);
    if (win_h) *win_h = atomic_load_explicit(&s_win_h, memory_order_relaxed);
    if (pix_w) *pix_w = atomic_load_explicit(&s_pix_w, memory_order_relaxed);
    if (pix_h) *pix_h = atomic_load_explicit(&s_pix_h, memory_order_relaxed);
    if (density) *density = (float)atomic_load_explicit(&s_density_x1000, memory_order_relaxed) / 1000.0f;
}

void gl_backend_set_loading(bool loading) { atomic_store_explicit(&s_loading, loading, memory_order_release); }
bool gl_backend_is_loading(void) { return atomic_load_explicit(&s_loading, memory_order_acquire); }

void gl_backend_apply_pending_vsync(void) {
    int want = atomic_exchange_explicit(&s_pending_swap_interval, -99, memory_order_acq_rel);
    if (want == -99) return;
    if (want < 0) {
        if (!SDL_GL_SetSwapInterval(-1)) {
            LOG("[gl] adaptive vsync (-1) unsupported, falling back to vsync=1\n");
            SDL_GL_SetSwapInterval(1);
            s_swap_interval = 1;
        } else {
            s_swap_interval = -1;
        }
    } else {
        SDL_GL_SetSwapInterval(want);
        s_swap_interval = want;
    }
}

void gl_backend_apply_window_settings(void) {
    if (!s_window) return;
    int req_w = 1280, req_h = 720;
    target_size_from_settings(&req_w, &req_h);
    int mode = g_settings.window_mode;
    if (mode == WIN_BORDERLESS) {
        g_settings.fullscreen = true;
        SDL_SetWindowFullscreenMode(s_window, NULL);
        SDL_SetWindowFullscreen(s_window, true);
    } else if (mode == WIN_EXCLUSIVE) {
        g_settings.fullscreen = true;
        SDL_DisplayID did = SDL_GetDisplayForWindow(s_window);
        const SDL_DisplayMode *picked = NULL;
        int nmodes = 0;
        SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(did, &nmodes);
        if (modes && nmodes > 0) {
            for (int i = 0; i < nmodes; i++) {
                if (modes[i] && modes[i]->w == req_w && modes[i]->h == req_h) {
                    if (!picked || modes[i]->refresh_rate > picked->refresh_rate) picked = modes[i];
                }
            }
            if (!picked) picked = modes[0];
        }
        SDL_SetWindowFullscreenMode(s_window, picked);
        SDL_SetWindowFullscreen(s_window, true);
        if (modes) SDL_free(modes);
    } else {
        g_settings.fullscreen = false;
        SDL_SetWindowFullscreen(s_window, false);
        SDL_SetWindowSize(s_window, req_w, req_h);
    }
    SDL_SyncWindow(s_window);
    atomic_store_explicit(&s_pending_swap_interval, vsync_interval_from_settings(), memory_order_release);
    update_cached_metrics_main_thread(s_window);
}
void gl_backend_window_set_title(const char *title) {
    if (s_window && title && *title) SDL_SetWindowTitle(s_window, title);
}
uint64_t gl_backend_present_count(void) { return atomic_load_explicit(&s_frames, memory_order_acquire); }

void *gl_backend_get_proc_address(const char *name) {
    if (!name || !*name) return NULL;
    void *p = (void *)SDL_GL_GetProcAddress(name);
#ifdef _WIN32
    if (!p) {
        static HMODULE gl32;
        if (!gl32) gl32 = GetModuleHandleA("opengl32.dll");
        if (!gl32) gl32 = LoadLibraryA("opengl32.dll");
        if (gl32) p = (void *)GetProcAddress(gl32, name);
    }
#endif
    return p;
}

bool gl_backend_make_current(bool make_null) {
    if (!s_window || !s_gl_ctx) {
        LOG("[gl] gl_backend_make_current called before window/context init\n");
        return false;
    }
    if (make_null) {
        if (!SDL_GL_MakeCurrent(s_window, NULL)) {
            LOG("[gl] SDL_GL_MakeCurrent(NULL) failed: %s\n", SDL_GetError());
            return false;
        }
        return true;
    }
    if (!SDL_GL_MakeCurrent(s_window, s_gl_ctx)) {
        LOG("[gl] SDL_GL_MakeCurrent(ctx) failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_SetSwapInterval(s_swap_interval);
    gl_backend_apply_pending_vsync();
    gl_hle_init_procs();
    gl_hle_ensure_virtual_fbo();
    return true;
}

void gl_backend_set_swap_interval(int interval) {
    s_swap_interval = interval;
    if (s_window && SDL_GL_GetCurrentContext()) SDL_GL_SetSwapInterval(interval);
}

void gl_backend_swap_buffers(void) {
    static bool s_speed_vsync_overridden;
    static uint64_t s_last_swap_ns;
    if (!s_window || atomic_load_explicit(&s_quitting, memory_order_acquire)) return;
    if (rt_reboot_requested()) {
        gl_imgui_overlay_shutdown();
        SDL_GL_MakeCurrent(s_window, NULL);
        return;
    }
    uint64_t frame = atomic_fetch_add_explicit(&s_frames, 1, memory_order_acq_rel) + 1;
    uint64_t now_swap_ns = plat_now_ns();
    if (!s_probe_t0_ns) s_probe_t0_ns = now_swap_ns;
    s_probe_last_t_ns = now_swap_ns;
    if (frame <= 10 || frame % 60 == 0) {
        LOG("[gl] eglSwapBuffers frame=%llu\n", (unsigned long long)frame);
    }
    bool want_exp_60fps = exp_60fps_enabled();
    bool need_probe = probe_60fps_enabled();
    if (want_exp_60fps || need_probe) {
        volatile uint8_t *pbase = yw1_v130_pacing_base(frame == 1);
        uint32_t pre_tgt = 0;
        uint32_t pre_base = 0;
        if (pbase) {
            pre_base = *(const volatile uint32_t *)(pbase + 0x7d0);
            pre_tgt = *(const volatile uint32_t *)(pbase + 0x7d4);
            if (want_exp_60fps && pre_tgt == 2 && pre_base >= 1 && pre_base <= 6) {
                *(volatile uint32_t *)(pbase + 0x7d4) = 1u;
                *(volatile float *)(pbase + 0x7dc) = 1.0f / (float)pre_base;
                s_exp_60fps_overrides++;
                if (s_exp_60fps_overrides <= 10) {
                    LOG("[60fps] override #%llu at frame=%llu: target_vsync(+7d4) 2->1 base(+7d0)=%u ratio(+7dc)->%.3f\n",
                        (unsigned long long)s_exp_60fps_overrides, (unsigned long long)frame,
                        pre_base, (double)(1.0f / (float)pre_base));
                }
            }
        }
        if (need_probe) {
            bool edge_change = false;
            if (pbase) {
                uint32_t cur_prev = *(const volatile uint32_t *)(pbase + 0x7cc);
                if (cur_prev != s_probe_last_prev_vsync) {
                    s_probe_logic_frames++;
                    s_probe_last_prev_vsync = cur_prev;
                }
                uint32_t cur_base = *(const volatile uint32_t *)(pbase + 0x7d0);
                uint32_t cur_tgt = *(const volatile uint32_t *)(pbase + 0x7d4);
                if (cur_base != s_probe_last_base || cur_tgt != s_probe_last_tgt || pre_tgt != cur_tgt) {
                    s_probe_last_base = cur_base;
                    s_probe_last_tgt = cur_tgt;
                    if (s_probe_edge_logs < 24) {
                        s_probe_edge_logs++;
                        edge_change = true;
                    }
                }
            }
            bool mov_now = movie_is_playing();
            if (frame == 1 || frame == 30 || frame == 120 || frame == 450 || frame == 700 ||
                frame == 1000 || frame == 1400) {
                probe_log_snapshot(frame, now_swap_ns, "tick", pbase);
            } else if (edge_change) {
                probe_log_snapshot(frame, now_swap_ns, "pacing_change", pbase);
            }
            if (mov_now && !s_probe_prev_movie) {
                s_probe_movie_start_frame = frame;
                s_probe_movie_start_ms = (double)(now_swap_ns - s_probe_t0_ns) / 1e6;
                probe_log_snapshot(frame, now_swap_ns, "movie_start", pbase);
            } else if (!mov_now && s_probe_prev_movie) {
                s_probe_movie_end_ms = (double)(now_swap_ns - s_probe_t0_ns) / 1e6;
                probe_log_snapshot(frame, now_swap_ns, "movie_end", pbase);
            }
            s_probe_prev_movie = mov_now;
        }
    }
    if (frame_should_dump(frame)) dump_frame_bmp(frame);
    int win_w = 1280, win_h = 720, pw = 1280, ph = 720;
    float density = 1.0f;
    gl_backend_get_cached_metrics(&win_w, &win_h, &pw, &ph, &density);
    uint32_t eff_pct = rt_effective_speed_pct();
    if (eff_pct > 100) {
        if (!s_speed_vsync_overridden || s_swap_interval != 0) {
            SDL_GL_SetSwapInterval(0);
            s_swap_interval = 0;
            s_speed_vsync_overridden = true;
        }
        uint64_t base_frame_ns = want_exp_60fps ? 16666667ull : 33333333ull;
        uint64_t target_frame_ns = (base_frame_ns * 100ull) / eff_pct;
        uint64_t now_ns = plat_now_ns();
        if (s_last_swap_ns > 0 && now_ns - s_last_swap_ns < target_frame_ns) {
            plat_sleep_until_ns(s_last_swap_ns + target_frame_ns);
            now_ns = plat_now_ns();
        }
        s_last_swap_ns = now_ns;
    } else {
        s_last_swap_ns = 0;
        if (s_speed_vsync_overridden) {
            s_speed_vsync_overridden = false;
            atomic_store_explicit(&s_pending_swap_interval, vsync_interval_from_settings(), memory_order_release);
        }
        gl_backend_apply_pending_vsync();
    }
    gl_hle_blit_virtual_fbo(pw, ph);
    gl_imgui_render_in_game_overlay(frame, win_w, win_h, pw, ph, density);
    SDL_GL_SwapWindow(s_window);
    gl_hle_check_post_swap_errors(frame);
    input_frame(frame);
    static int64_t s_stop_after_ms = -2;
    if (s_stop_after_ms == -2) {
        const char *se = getenv("YW1_STOP_AFTER_MS");
        s_stop_after_ms = (se && *se) ? strtoll(se, NULL, 10) : -1;
    }
    bool time_stop = (s_stop_after_ms > 0 && (double)(plat_now_ns() - s_probe_t0_ns) / 1e6 >= (double)s_stop_after_ms);
    if (input_should_stop(frame) || time_stop) {
        LOG("[replay] graceful stop at frame=%llu (reason=%s)\n",
            (unsigned long long)frame, time_stop ? "time_limit" : "replay_stop");
        gl_backend_quit();
        fflush(stdout);
        fflush(stderr);
        exit(0);
    }
}

int gl_backend_run(void) {
    gl_backend_window_init();
    for (;;) {
        menu_apply_window_requests(s_window);
        if (gl_imgui_consume_window_apply()) gl_backend_apply_window_settings();
        if (rt_reboot_requested()) return rt_reboot_requested();
        SDL_Event e;
        while (SDL_WaitEventTimeout(&e, 16)) {
            input_rumble_poll();
            if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
                atomic_store_explicit(&s_win_focused, true, memory_order_relaxed);
            else if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST || e.type == SDL_EVENT_WINDOW_MINIMIZED)
                atomic_store_explicit(&s_win_focused, false, memory_order_relaxed);
            else if (e.type == SDL_EVENT_WINDOW_RESTORED) {
                atomic_store_explicit(&s_win_focused, (SDL_GetWindowFlags(s_window) & SDL_WINDOW_INPUT_FOCUS) != 0,
                                      memory_order_relaxed);
                update_cached_metrics_main_thread(s_window);
            } else if (e.type == SDL_EVENT_WINDOW_RESIZED ||
                       e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                       e.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
                       e.type == SDL_EVENT_WINDOW_MOVED ||
                       e.type == SDL_EVENT_WINDOW_MAXIMIZED ||
                       e.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN ||
                       e.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) {
                update_cached_metrics_main_thread(s_window);
            }
            if (e.type == SDL_EVENT_QUIT) {
                input_rumble_stop();
                input_led_stop();
                LOG("[gl] window closed\n");
                gl_backend_quit();
                exit(0);
            }
            if (menu_handle_event(&e)) continue;
            if (input_user_blocked(&e)) continue;
            input_cursor_event(&e);
            input_handle_event(&e);
        }
        menu_apply_window_requests(s_window);
        if (gl_imgui_consume_window_apply()) gl_backend_apply_window_settings();
        input_rumble_poll();
        input_led_poll();
    }
}
