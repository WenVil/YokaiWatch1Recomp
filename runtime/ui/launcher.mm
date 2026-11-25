// Compliant Multi-Language HiDPI Launcher & Pre-Launch Settings UI for 13 Sentinels: Aegis Rim (M3-A2).
// Validates user-supplied extracted exefs/romfs (including Windows UTF-8 CJK paths) without copying CPKs,
// supports full controller navigation, live UI/game language switching, save backups, and smooth animated loading.
#ifdef __APPLE__
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#endif
#include <SDL3/SDL.h>
#include <ctype.h>
#include <float.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <algorithm>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"
#include "osk.h"
#include "pad_nav.h"
#include "ui_font.h"
#include "ui_i18n.h"
#ifdef __APPLE__
#include "imgui_impl_metal.h"
#else
#include "imgui_impl_vulkan.h"
#endif
#include "imgui_impl_sdl3.h"

extern "C" {
#include "../platform.h"
#include "../paths.h"
#include "../game_addresses.h"
#include "../gamebuild.h"
#include "../gamelib.h"
#include "../settings.h"
#include "../input_bind.h"
bool controls_ui_took_back(void);
void ui_window_init(void);
void ui_window_set_title(const char *title);
SDL_Window *ui_window(void);
void boot_prepare(const char *root, int *stage);
#ifdef __APPLE__
void *ui_metal_layer(void);
void *ui_metal_device(void);
#else
bool vk_frame_begin(const float clear[4]);
void vk_frame_end(const char *shot_path);
void *vk_frame_cmd(void);
bool vk_imgui_backend_init(void);
void vk_imgui_fonts_rebuild(void);
void vk_imgui_backend_shutdown(void);
#endif
}

#ifdef __APPLE__
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
static void save_png(id<MTLTexture> tex, const char *path) {
    size_t w = tex.width, h = tex.height;
    std::vector<uint8_t> px(w * h * 4);
    [tex getBytes:px.data() bytesPerRow:w * 4 fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(px.data(), w, h, 8, w * 4, cs,
                                             kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path, strlen(path), false);
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL(url, (__bridge CFStringRef)UTTypePNG.identifier, 1, NULL);
    if (dest) {
        CGImageDestinationAddImage(dest, img, NULL);
        CGImageDestinationFinalize(dest);
        CFRelease(dest);
    }
    CFRelease(url), CGImageRelease(img), CGContextRelease(ctx), CGColorSpaceRelease(cs);
}
#endif

#define LARECOMP_VERSION "v1.0.0-beta1"
extern "C" const char *larecomp_version(void) { return LARECOMP_VERSION; }

extern "C" int g_start_screen;
extern int g_tab_request_ext;
extern "C" void menu_apply_window_requests(void *sdl_window);
extern "C" void input_cursor_event(const SDL_Event *e);
extern "C" bool input_cursor_hidden(void);
void menu_tabs_draw(float scale, ImFont *font, ImFont *font_big, bool in_game, void (*prelaunch)(float));
void menu_tabs_focus(bool first_tab);

static bool g_play_requested = false;
extern "C" void launcher_play_seed(const char *seed) {
    (void)seed;
    g_play_requested = true;
}

// ------------------------------------------------------------------ gamepad forwarding
static void launcher_pad_open_all(void) {
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    for (int i = 0; ids && i < n; i++) SDL_OpenGamepad(ids[i]);
    SDL_free(ids);
}

static bool launcher_pad_event(const SDL_Event &e) {
    ImGuiIO &io = ImGui::GetIO();
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
        SDL_OpenGamepad(e.gdevice.which);
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        return true;
    case SDL_EVENT_GAMEPAD_REMOVED:
        if (!SDL_HasGamepad()) io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        bool down = (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
        ImGuiKey k = ui_pad_key(e.gbutton.which, e.gbutton.button, down);
        if (k != ImGuiKey_None) io.AddKeyEvent(k, down);
        return true;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        float v = e.gaxis.value / 32767.0f;
        const float dz = 0.25f;
        auto analog_val = [&](float x) -> float { return x > dz ? (x - dz) / (1.0f - dz) : 0.0f; };
        switch (e.gaxis.axis) {
        case SDL_GAMEPAD_AXIS_LEFTX:
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, v < -dz, analog_val(-v));
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, v > dz, analog_val(v));
            break;
        case SDL_GAMEPAD_AXIS_LEFTY:
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, v < -dz, analog_val(-v));
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, v > dz, analog_val(v));
            break;
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
            io.AddKeyAnalogEvent(ImGuiKey_GamepadL2, v > 0.2f, v);
            break;
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
            io.AddKeyAnalogEvent(ImGuiKey_GamepadR2, v > 0.2f, v);
            break;
        default:
            break;
        }
        return true;
    }
    default:
        return false;
    }
}

// ------------------------------------------------------------------ 13 Sentinels resource validation
enum {
    V13_VER_NONE = 0,
    V13_VER_ASIA_100 = 1,
    V13_VER_US_100 = 2,
    V13_VER_US_110 = 3,
};

struct V13KnownBuild {
    int kind;
    const char *build_id;
    const char *region_tag;
};

static const V13KnownBuild k_v13_builds[] = {
    {V13_VER_ASIA_100, "311947020F64F5742CE3C16162C4D87F92C68313", "Asia"},
    {V13_VER_US_100,   "57212E16CB83BB553B7C08493B9484FB836E56C8", "US/EU"},
    {V13_VER_US_110,   "4B632573CA5C160EABB70519FB622718956D70EA", "US/EU"},
};

struct V13CheckResult {
    bool configured = false;
    bool has_images = false;
    bool exefs_ok = false;
    bool build_id_ok = false;
    bool romfs_ok = false;
    bool dll_ok = false;
    bool ready = false;
    int version_kind = V13_VER_NONE;
    bool has_tw_cpk = false;
    bool has_ko_cpk = false;
    bool has_patch_us = false;
    std::string resolved_root;
    std::string exefs_dir;
    std::string romfs_dir;
    std::string found_build_id;
    std::string nacp_version;
    std::string supported_langs;
    std::string headline;
    std::string detail;
};

static bool has_image_ext(const std::string &name) {
    size_t dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot);
    for (char &c : ext) c = (char)tolower((unsigned char)c);
    return ext == ".nsp" || ext == ".xci" || ext == ".nca" || ext == ".nsz" || ext == ".xcz";
}

static bool scan_has_images(const std::string &dir, int depth) {
    if (depth > 1) return false;
    PlatDir *d = plat_opendir(dir.c_str());
    if (!d) return false;
    char name[512];
    bool is_d = false;
    std::vector<std::string> sub;
    bool found = false;
    while (plat_readdir(d, name, sizeof name, &is_d, nullptr)) {
        if (name[0] == '.') continue;
        if (!is_d && has_image_ext(name)) { found = true; break; }
        if (is_d && depth == 0) sub.push_back(dir + "/" + name);
    }
    plat_closedir(d);
    if (found) return true;
    for (const auto &s : sub)
        if (scan_has_images(s, depth + 1)) return true;
    return false;
}

static bool resolve_v13_pair(const std::string &base, std::string &out_root, std::string &out_exefs, std::string &out_romfs) {
    char ex[1200], rm[1200];
    if (rt_find_subdir_icase(base.c_str(), "exefs", ex, sizeof ex) &&
        rt_find_subdir_icase(base.c_str(), "romfs", rm, sizeof rm)) {
        out_root = base;
        out_exefs = ex;
        out_romfs = rm;
        return true;
    }
    PlatDir *d = plat_opendir(base.c_str());
    if (!d) return false;
    char name[512];
    bool is_d = false;
    std::vector<std::string> sub;
    while (plat_readdir(d, name, sizeof name, &is_d, nullptr)) {
        if (is_d && name[0] != '.') sub.push_back(base + "/" + name);
    }
    plat_closedir(d);
    std::sort(sub.begin(), sub.end());
    for (const auto &s : sub) {
        if (rt_find_subdir_icase(s.c_str(), "exefs", ex, sizeof ex) &&
            rt_find_subdir_icase(s.c_str(), "romfs", rm, sizeof rm)) {
            out_root = s;
            out_exefs = ex;
            out_romfs = rm;
            return true;
        }
    }
    return false;
}

static bool check_magic4(const std::string &path, const char magic[4], unsigned char *hdr60_out) {
    FILE *f = plat_fopen(path.c_str(), "rb");
    if (!f) return false;
    unsigned char buf[0x60];
    size_t need = hdr60_out ? 0x60 : 4;
    size_t got = fread(buf, 1, need, f);
    fclose(f);
    if (got < need || memcmp(buf, magic, 4) != 0) return false;
    if (hdr60_out) memcpy(hdr60_out, buf, 0x60);
    return true;
}

static std::string read_nacp_display_ver(const std::string &root) {
    FILE *f = plat_fopen((root + "/control.nacp").c_str(), "rb");
    if (!f) return "";
    char ver[17] = {};
    if (fseek(f, 0x3060, SEEK_SET) == 0) {
        fread(ver, 1, 16, f);
    }
    fclose(f);
    ver[16] = 0;
    return ver;
}

static bool is_game_lang_mismatch(int version_kind, int game_lang) {
    if (game_lang == GAME_LANG_AUTO || version_kind == V13_VER_NONE) return false;
    if (version_kind == V13_VER_ASIA_100) {
        return game_lang != GAME_LANG_ZH_HANS && game_lang != GAME_LANG_ZH_HANT && game_lang != GAME_LANG_KO;
    }
    return game_lang != GAME_LANG_EN_US && game_lang != GAME_LANG_FR && game_lang != GAME_LANG_DE &&
           game_lang != GAME_LANG_IT && game_lang != GAME_LANG_ES;
}

static V13CheckResult validate_game_dir(const std::string &raw_dir) {
    V13CheckResult r;
    if (raw_dir.empty()) {
        r.headline = ui_tr(STR_VAL_EMPTY_HEAD);
        r.detail = ui_tr(STR_VAL_EMPTY_DET);
        return r;
    }
    r.configured = true;
    if (!plat_is_dir(raw_dir.c_str())) {
        r.headline = ui_tr(STR_VAL_NODIR_HEAD);
        r.detail = std::string(ui_tr(STR_VAL_NODIR_DET)) + raw_dir;
        return r;
    }
    r.has_images = scan_has_images(raw_dir, 0);

    if (!resolve_v13_pair(raw_dir, r.resolved_root, r.exefs_dir, r.romfs_dir)) {
        r.resolved_root = raw_dir;
        if (r.has_images) {
            r.headline = ui_tr(STR_VAL_IMG_HEAD);
            r.detail = ui_tr(STR_VAL_IMG_DET);
        } else {
            r.headline = ui_tr(STR_VAL_NO_EXEROM_HEAD);
            r.detail = ui_tr(STR_VAL_NO_EXEROM_DET);
        }
        return r;
    }

    static const struct { const char *name; const char *magic; } k_exefs_req[] = {
        {"main", "NSO0"},
        {"main.npdm", "META"},
        {"rtld", "NSO0"},
        {"sdk", "NSO0"},
        {"subsdk0", "NSO0"},
    };
    std::vector<std::string> missing_exe, bad_exe;
    unsigned char main_hdr[0x60] = {};
    bool have_main_hdr = false;
    for (const auto &req : k_exefs_req) {
        std::string fp = r.exefs_dir + "/" + req.name;
        if (!plat_path_exists(fp.c_str())) {
            missing_exe.emplace_back(req.name);
        } else if (!check_magic4(fp, req.magic, strcmp(req.name, "main") == 0 ? main_hdr : nullptr)) {
            bad_exe.emplace_back(req.name);
        } else if (strcmp(req.name, "main") == 0) {
            have_main_hdr = true;
        }
    }

    if (have_main_hdr) {
        char hex[41];
        for (int i = 0; i < 20; i++) snprintf(hex + i * 2, 3, "%02X", main_hdr[0x40 + i]);
        hex[40] = 0;
        r.found_build_id = hex;
        for (const auto &kb : k_v13_builds) {
            if (strcmp(hex, kb.build_id) == 0) {
                r.version_kind = kb.kind;
                r.build_id_ok = true;
                break;
            }
        }
    }

    r.exefs_ok = missing_exe.empty() && bad_exe.empty() && r.build_id_ok;
    r.nacp_version = read_nacp_display_ver(r.resolved_root);

    auto find_cpk = [&](const char *upper_name, const char *lower_name) -> bool {
        return plat_path_exists((r.romfs_dir + "/" + upper_name).c_str()) ||
               plat_path_exists((r.romfs_dir + "/" + lower_name).c_str());
    };

    bool has_cn = false, has_us = false;
    bool has_fr = false, has_ge = false, has_it = false, has_sp = false;
    bool missing_us_core = false, missing_us_patch = false;
    if (r.version_kind == V13_VER_ASIA_100) {
        has_cn = find_cpk("ROBO_CN.CPK", "robo_cn.cpk");
        r.has_tw_cpk = find_cpk("ROBO_TW.CPK", "robo_tw.cpk");
        r.has_ko_cpk = find_cpk("ROBO_KO.CPK", "robo_ko.cpk");
        r.romfs_ok = has_cn;
        r.supported_langs = "简体中文 · 繁體中文 · 한국어";
    } else if (r.version_kind == V13_VER_US_100 || r.version_kind == V13_VER_US_110) {
        has_us = find_cpk("ROBO_US.CPK", "robo_us.cpk");
        has_fr = find_cpk("ROBO_FR.CPK", "robo_fr.cpk");
        has_ge = find_cpk("ROBO_GE.CPK", "robo_ge.cpk");
        has_it = find_cpk("ROBO_IT.CPK", "robo_it.cpk");
        has_sp = find_cpk("ROBO_SP.CPK", "robo_sp.cpk");
        r.has_patch_us = find_cpk("ROBO_PATCH_US.CPK", "robo_patch_us.cpk");
        missing_us_core = !has_us;
        missing_us_patch = (r.version_kind == V13_VER_US_110 && !r.has_patch_us);
        r.romfs_ok = !missing_us_core && !missing_us_patch;
        r.supported_langs = "English";
        if (has_fr) r.supported_langs += " · Français";
        if (has_ge) r.supported_langs += " · Deutsch";
        if (has_it) r.supported_langs += " · Italiano";
        if (has_sp) r.supported_langs += " · Español";
    }

    r.dll_ok = gamelib_present(r.resolved_root.c_str());

    if (!missing_exe.empty() || !bad_exe.empty()) {
        r.headline = ui_tr(STR_VAL_EXE_BAD_HEAD);
        std::string msg;
        if (!missing_exe.empty()) {
            msg += ui_tr(STR_VAL_MISSING_PFX);
            for (size_t i = 0; i < missing_exe.size(); i++) {
                if (i) msg += ui_tr(STR_VAL_LIST_SEP);
                msg += "exefs/" + missing_exe[i];
            }
        }
        if (!bad_exe.empty()) {
            if (!msg.empty()) msg += "\n";
            msg += ui_tr(STR_VAL_CORRUPT_PFX);
            for (size_t i = 0; i < bad_exe.size(); i++) {
                if (i) msg += ui_tr(STR_VAL_LIST_SEP);
                msg += "exefs/" + bad_exe[i];
            }
        }
        r.detail = msg;
    } else if (!r.build_id_ok) {
        r.headline = ui_tr(STR_VAL_BUILDID_HEAD);
        char det[512];
        snprintf(det, sizeof det, ui_tr(STR_VAL_BUILDID_DET_FMT), r.found_build_id.c_str());
        r.detail = det;
    } else if (!r.romfs_ok) {
        if (missing_us_patch) {
            r.headline = ui_tr(STR_VAL_NOPATCH_US_HEAD);
            r.detail = ui_tr(STR_VAL_NOPATCH_US_DET);
        } else if (missing_us_core || r.version_kind != V13_VER_ASIA_100) {
            r.headline = ui_tr(STR_VAL_NOROM_US_HEAD);
            r.detail = ui_tr(STR_VAL_NOROM_US_DET);
        } else {
            r.headline = ui_tr(STR_VAL_NOROM_HEAD);
            r.detail = ui_tr(STR_VAL_NOROM_DET);
        }
    } else {
        r.ready = r.dll_ok;
        if (r.version_kind == V13_VER_ASIA_100) {
            r.headline = ui_tr(STR_VAL_OK_HEAD);
            r.detail = std::string(ui_tr(STR_VAL_DETECTED_PFX)) + "ROBO_CN.CPK";
            if (r.has_tw_cpk) r.detail += " · ROBO_TW.CPK";
            if (r.has_ko_cpk) r.detail += " · ROBO_KO.CPK";
        } else if (r.version_kind == V13_VER_US_100) {
            r.headline = ui_tr(STR_VAL_OK_US100_HEAD);
            r.detail = std::string(ui_tr(STR_VAL_DETECTED_PFX)) + "ROBO_US.CPK";
            if (has_fr) r.detail += " · FR";
            if (has_ge) r.detail += " · GE";
            if (has_it) r.detail += " · IT";
            if (has_sp) r.detail += " · SP";
        } else {
            r.headline = ui_tr(STR_VAL_OK_US110_HEAD);
            r.detail = std::string(ui_tr(STR_VAL_DETECTED_PFX)) + "ROBO_US.CPK · ROBO_PATCH_US.CPK";
            if (has_fr) r.detail += " · FR";
            if (has_ge) r.detail += " · GE";
            if (has_it) r.detail += " · IT";
            if (has_sp) r.detail += " · SP";
        }
        if (!r.supported_langs.empty()) {
            r.detail += "\n" + std::string(ui_tr(STR_VAL_SUPPORTED_LANGS_PFX)) + r.supported_langs;
        }
    }
    return r;
}

// ------------------------------------------------------------------ NSP/XCI + Update importer (nx_extract)
static pthread_mutex_t g_imp_mu = PTHREAD_MUTEX_INITIALIZER;
struct ImportState {
    bool open_popup = false;
    bool running = false;
    bool finished = false;
    bool cancel_req = false;
    int exit_code = 0;
    PlatProc proc = 0;
    pthread_t tid = 0;
    std::string nx_exe;
    std::string base_path;
    std::string upd_path;
    std::string keys_path;
    std::string out_dir;
    std::string log_path;
    std::string pick_base;
    std::string pick_upd;
    std::string pick_keys;
    std::string pick_out;
    std::string status_msg;
    bool status_is_err = false;
} g_imp;

static std::string canon_path(const std::string &p) {
#ifdef _WIN32
    char full[1024];
    if (_fullpath(full, p.c_str(), sizeof full)) return full;
#else
    char full[1024];
    if (realpath(p.c_str(), full)) return full;
#endif
    return p;
}

static std::string dirname_of(const std::string &p) {
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? "" : p.substr(0, s);
}

static std::string find_nx_extract_exe(void) {
    const char *env = getenv("LARECOMP_NX_EXTRACT");
    if (env && *env && plat_path_exists(env)) return canon_path(env);
    char exe_buf[1024] = {};
    std::string ed = plat_exe_path(exe_buf, sizeof exe_buf) ? dirname_of(exe_buf) : ".";
    const std::string cands[] = {
        ed + "/nx_extract/nx_extract" PLAT_EXE_SUFFIX,
        ed + "/nx_extract" PLAT_EXE_SUFFIX,
        "nx_extract/nx_extract" PLAT_EXE_SUFFIX,
#ifndef LARECOMP_ZIP_BUILD
        ed + "/../../work/nx_extract/bin/Release/net9.0/nx_extract" PLAT_EXE_SUFFIX,
        ed + "/../../../work/nx_extract/bin/Release/net9.0/nx_extract" PLAT_EXE_SUFFIX,
        "work/nx_extract/bin/Release/net9.0/nx_extract" PLAT_EXE_SUFFIX,
        "../work/nx_extract/bin/Release/net9.0/nx_extract" PLAT_EXE_SUFFIX,
#endif
    };
    for (const auto &c : cands)
        if (plat_path_exists(c.c_str())) return canon_path(c);
    return "";
}

static std::string find_default_keys(const std::string &base_pkg) {
    char exe_buf[1024] = {};
    std::string ed = plat_exe_path(exe_buf, sizeof exe_buf) ? dirname_of(exe_buf) : ".";
    std::vector<std::string> cands = {
        "prod.keys",
        ed + "/prod.keys",
        ed + "/../../../prod.keys",
    };
    if (!base_pkg.empty()) {
        std::string bd = dirname_of(base_pkg);
        if (!bd.empty()) {
            cands.push_back(bd + "/prod.keys");
            std::string bdp = dirname_of(bd);
            if (!bdp.empty()) cands.push_back(bdp + "/prod.keys");
        }
    }
    if (const char *up = getenv("USERPROFILE")) cands.push_back(std::string(up) + "/.switch/prod.keys");
    if (const char *hm = getenv("HOME")) cands.push_back(std::string(hm) + "/.switch/prod.keys");
    for (const auto &c : cands)
        if (plat_path_exists(c.c_str())) return canon_path(c);
    return "";
}

static void SDLCALL on_imp_base_picked(void *, const char *const *files, int) {
    if (!files || !files[0] || !files[0][0]) return;
    pthread_mutex_lock(&g_imp_mu);
    g_imp.pick_base = files[0];
    pthread_mutex_unlock(&g_imp_mu);
}
static void SDLCALL on_imp_upd_picked(void *, const char *const *files, int) {
    if (!files || !files[0] || !files[0][0]) return;
    pthread_mutex_lock(&g_imp_mu);
    g_imp.pick_upd = files[0];
    pthread_mutex_unlock(&g_imp_mu);
}
static void SDLCALL on_imp_keys_picked(void *, const char *const *files, int) {
    if (!files || !files[0] || !files[0][0]) return;
    pthread_mutex_lock(&g_imp_mu);
    g_imp.pick_keys = files[0];
    pthread_mutex_unlock(&g_imp_mu);
}
static void SDLCALL on_imp_out_picked(void *, const char *const *files, int) {
    if (!files || !files[0] || !files[0][0]) return;
    pthread_mutex_lock(&g_imp_mu);
    g_imp.pick_out = files[0];
    pthread_mutex_unlock(&g_imp_mu);
}

static void *import_worker_fn(void *) {
    pthread_mutex_lock(&g_imp_mu);
    std::string exe = g_imp.nx_exe;
    std::string keys = g_imp.keys_path;
    std::string base = g_imp.base_path;
    std::string upd = g_imp.upd_path.empty() ? "-" : g_imp.upd_path;
    std::string out = g_imp.out_dir;
    std::string logp = g_imp.log_path;
    pthread_mutex_unlock(&g_imp_mu);

    plat_mkdir_p(out.c_str());
    const char *argv[6] = {exe.c_str(), keys.c_str(), base.c_str(), upd.c_str(), out.c_str(), NULL};
    PlatProc p = plat_spawn(argv, logp.c_str(), false);
    if (!p) {
        pthread_mutex_lock(&g_imp_mu);
        g_imp.exit_code = 255;
        g_imp.running = false;
        g_imp.finished = true;
        pthread_mutex_unlock(&g_imp_mu);
        return NULL;
    }
    pthread_mutex_lock(&g_imp_mu);
    g_imp.proc = p;
    pthread_mutex_unlock(&g_imp_mu);

    int rc = -1;
    while (true) {
        rc = plat_proc_wait(p, false);
        if (rc >= 0) break;
        pthread_mutex_lock(&g_imp_mu);
        bool cancel = g_imp.cancel_req;
        pthread_mutex_unlock(&g_imp_mu);
        if (cancel) {
            plat_proc_kill(p);
            rc = 130;
            break;
        }
        plat_sleep_until_ns(plat_now_ns() + 80000000ull);
    }
    pthread_mutex_lock(&g_imp_mu);
    g_imp.proc = 0;
    g_imp.exit_code = rc;
    g_imp.running = false;
    g_imp.finished = true;
    pthread_mutex_unlock(&g_imp_mu);
    return NULL;
}

static std::string read_tail_text(const std::string &path, size_t max_chars) {
    FILE *f = plat_fopen(path.c_str(), "rb");
    if (!f) return "";
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return ""; }
    long start = sz > (long)max_chars ? sz - (long)max_chars : 0;
    fseek(f, start, SEEK_SET);
    std::string s((size_t)(sz - start), '\0');
    fread(&s[0], 1, s.size(), f);
    fclose(f);
    return s;
}

// ------------------------------------------------------------------ folder picker & async boot worker
static pthread_mutex_t g_pick_mu = PTHREAD_MUTEX_INITIALIZER;
static std::string g_picked_dir;
static std::string g_pick_err;
static bool g_has_pick = false;

static void SDLCALL on_folder_picked(void *, const char *const *files, int) {
    pthread_mutex_lock(&g_pick_mu);
    if (!files) {
        g_pick_err = ui_tr(STR_ERR_PICKER_OPEN);
    } else if (files[0] && files[0][0]) {
        g_picked_dir = files[0];
        g_pick_err.clear();
        g_has_pick = true;
    }
    pthread_mutex_unlock(&g_pick_mu);
}

struct BootWorkerArgs {
    std::string root;
    int stage = 0;
    std::atomic<bool> done{false};
};

static pthread_mutex_t g_bld_mu = PTHREAD_MUTEX_INITIALIZER;
struct BuildState {
    bool running = false;
    bool finished = false;
    bool ok = false;
    bool cancel_req = false;
    bool auto_launch = false;
    double frac = 0.0;
    std::string step;
    std::string target_dir;
    std::string err_msg;
    std::string status_notice;
    bool notice_is_err = false;
    pthread_t tid = 0;
} g_bld;

static void on_gamebuild_progress(double frac, const char *step) {
    pthread_mutex_lock(&g_bld_mu);
    g_bld.frac = frac < 0.0 ? 0.0 : frac > 1.0 ? 1.0 : frac;
    if (step && *step) g_bld.step = step;
    pthread_mutex_unlock(&g_bld_mu);
}

static bool on_gamebuild_cancelled(void) {
    pthread_mutex_lock(&g_bld_mu);
    bool c = g_bld.cancel_req;
    pthread_mutex_unlock(&g_bld_mu);
    return c;
}

static void *build_worker_fn(void *) {
    pthread_mutex_lock(&g_bld_mu);
    std::string dir = g_bld.target_dir;
    pthread_mutex_unlock(&g_bld_mu);
    char cache_buf[1100], why[512] = "";
    const char *cache_dir = gamebuild_cache_dir(dir.c_str(), cache_buf, sizeof cache_buf);
    int jobs = plat_cpu_count();
    if (jobs < 1) jobs = 4;
    if (jobs > 16) jobs = 16;
    GameBuildOpts opts = {on_gamebuild_progress, jobs, on_gamebuild_cancelled};
    bool ok = gamebuild_run_into(dir.c_str(), cache_dir, &opts, why, sizeof why);
    pthread_mutex_lock(&g_bld_mu);
    g_bld.ok = ok;
    g_bld.err_msg = why;
    g_bld.running = false;
    g_bld.finished = true;
    pthread_mutex_unlock(&g_bld_mu);
    return NULL;
}

static void *boot_worker_fn(void *arg) {
    BootWorkerArgs *wa = (BootWorkerArgs *)arg;
    boot_prepare(wa->root.c_str(), &wa->stage);
    wa->done.store(true, std::memory_order_release);
    return NULL;
}

// ------------------------------------------------------------------ launcher entry
static void compute_hidpi_metrics(SDL_Window *win, float *out_sc, float *out_pix_density, float *out_raster_scale) {
    int win_w = 1280, win_h = 720, pix_w = 1280, pix_h = 720;
    SDL_GetWindowSize(win, &win_w, &win_h);
    SDL_GetWindowSizeInPixels(win, &pix_w, &pix_h);
    float disp_scale = SDL_GetWindowDisplayScale(win);
    if (disp_scale < 1.0f) disp_scale = 1.0f;
    float pix_density = (win_w > 0 && pix_w > 0) ? (float)pix_w / (float)win_w : 1.0f;
    float sc = fmaxf(0.65f, fminf(2.4f, (float)(win_h > 0 ? win_h : 720) / 1080.0f));
    if (out_sc) *out_sc = sc;
    if (out_pix_density) *out_pix_density = pix_density;
    if (out_raster_scale) *out_raster_scale = sc * pix_density;
}

extern "C" void launcher_run(const char *initial_root, bool *out_prepared) {
    if (out_prepared) *out_prepared = false;
    g_start_screen = 1;
    g_play_requested = false;

    ui_window_init();
    SDL_Window *win = ui_window();

    IMGUI_CHECKVERSION();
    ImGuiContext *prev_ctx = ImGui::GetCurrentContext();
    ImGuiContext *ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = NULL;

    float sc = 1.0f, pix_density = 1.0f, raster_scale = 1.0f;
    compute_hidpi_metrics(win, &sc, &pix_density, &raster_scale);
    int built_lang = ui_resolve_lang();
    float built_raster = raster_scale;

    ImFont *f_small = nullptr, *f_menu = nullptr, *f_title = nullptr;
    ui_load_i18n_fonts(&f_small, &f_menu, &f_title,
                        28.0f * raster_scale, 33.0f * raster_scale, 38.0f * raster_scale,
                        built_lang, nullptr, getenv("LARECOMP_LAUNCHER_FONT"));
    ui_apply_sentinel_theme(sc);

#ifdef __APPLE__
    CAMetalLayer *layer = (__bridge CAMetalLayer *)ui_metal_layer();
    id<MTLDevice> dev = (__bridge id<MTLDevice>)ui_metal_device();
    id<MTLCommandQueue> queue = [dev newCommandQueue];
    ImGui_ImplSDL3_InitForMetal(win);
    ImGui_ImplMetal_Init(dev);
#else
    ImGui_ImplSDL3_InitForVulkan(win);
    vk_imgui_backend_init();
#endif
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, NULL, 0);
    launcher_pad_open_all();
    if (SDL_HasGamepad()) io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

    bool dev_extracted = plat_is_dir("extracted");
    std::string active_dir;
    if (initial_root && *initial_root && strcmp(initial_root, "extracted") != 0) {
        active_dir = initial_root;
    } else if (dev_extracted) {
        active_dir = "extracted";
    } else if (g_settings.game_dir[0]) {
        active_dir = g_settings.game_dir;
    }

    int last_win_title_lang = -1;
    auto sync_win_title = [&](int rl) {
        if (last_win_title_lang != rl) {
            last_win_title_lang = rl;
            char wt[256];
            snprintf(wt, sizeof wt, "13SentinelsRecomp — %s", ui_tr_lang(rl, STR_APP_TITLE));
            ui_window_set_title(wt);
        }
    };
    sync_win_title(built_lang);

    V13CheckResult chk = validate_game_dir(active_dir);
    int chk_lang = built_lang;
    std::string backup_msg;
    bool backup_err = false;
    std::string picker_notice;

    enum { VIEW_HOME = 0, VIEW_SETTINGS = 1, VIEW_LOADING = 2 } view = VIEW_HOME;
    if (const char *lv = getenv("LARECOMP_LAUNCHER_VIEW")) {
        if (strcmp(lv, "settings") == 0) {
            view = VIEW_SETTINGS;
            menu_tabs_focus(true);
            if (const char *mt = getenv("LARECOMP_MENU_TAB")) g_tab_request_ext = atoi(mt);
        } else if (strcmp(lv, "loading") == 0) {
            view = VIEW_LOADING;
        }
    }

    long shot_at = getenv("LARECOMP_LAUNCHER_SHOT") ? atol(getenv("LARECOMP_LAUNCHER_SHOT")) : 0, frame = 0;
    bool open_lang_popup = getenv("LARECOMP_OPEN_LANG_POPUP") != NULL;
    if (getenv("LARECOMP_OPEN_IMPORT_MODAL")) {
        pthread_mutex_lock(&g_imp_mu);
        g_imp.open_popup = true;
        if (g_imp.nx_exe.empty()) g_imp.nx_exe = find_nx_extract_exe();
        if (g_imp.keys_path.empty()) g_imp.keys_path = find_default_keys(g_imp.base_path);
        pthread_mutex_unlock(&g_imp_mu);
    }
    bool autoplay = getenv("LARECOMP_LAUNCHER_AUTOPLAY") != NULL && chk.ready;
    std::string built_dir;

    BootWorkerArgs boot_args;
    pthread_t boot_tid = 0;
    bool boot_started = false;
    double boot_t0 = 0.0;
    float style_built_sc = sc;

    while (true) {
        // Trigger async boot when Play is requested
        if ((g_play_requested || autoplay) && !boot_started && chk.ready) {
            g_settings.aspect_asked = true;
            g_settings.menu_tip_seen = true;
            if (!dev_extracted && chk.exefs_ok && chk.romfs_ok) {
                strlcpy(g_settings.game_dir, chk.resolved_root.c_str(), sizeof g_settings.game_dir);
            }
            settings_save();
            boot_args.root = chk.resolved_root.empty() ? active_dir : chk.resolved_root;
            boot_args.stage = 0;
            boot_args.done.store(false, std::memory_order_release);
            view = VIEW_LOADING;
            boot_t0 = SDL_GetTicks() / 1000.0;
            boot_started = (pthread_create(&boot_tid, NULL, boot_worker_fn, &boot_args) == 0);
            if (!boot_started) break;
        }
        if (boot_started && boot_args.done.load(std::memory_order_acquire)) {
            double elapsed = (SDL_GetTicks() / 1000.0) - boot_t0;
            if (elapsed >= 0.35) {
                pthread_join(boot_tid, NULL);
                if (out_prepared) *out_prepared = true;
                break;
            }
        }

        // Handle picked directory synchronously before frame starts
        {
            pthread_mutex_lock(&g_pick_mu);
            bool picked = g_has_pick;
            std::string pdir = g_picked_dir;
            std::string perr = g_pick_err;
            g_has_pick = false;
            g_pick_err.clear();
            pthread_mutex_unlock(&g_pick_mu);

            if (!perr.empty()) picker_notice = perr;
            if (picked && !pdir.empty()) {
                picker_notice.clear();
                active_dir = pdir;
                chk = validate_game_dir(active_dir);
                chk_lang = ui_resolve_lang();
                if (!dev_extracted && chk.exefs_ok && chk.romfs_ok) {
                    strlcpy(g_settings.game_dir, chk.resolved_root.c_str(), sizeof g_settings.game_dir);
                    active_dir = chk.resolved_root;
                    settings_save();
                } else {
                    strlcpy(g_settings.game_dir, active_dir.c_str(), sizeof g_settings.game_dir);
                    settings_save();
                }
            }
        }

        // Handle import dialog file picks & async worker completion before frame starts
        std::string extra_font_paths = active_dir;
        {
            pthread_mutex_lock(&g_imp_mu);
            if (!g_imp.pick_base.empty()) {
                g_imp.base_path = g_imp.pick_base;
                g_imp.pick_base.clear();
                if (g_imp.keys_path.empty()) g_imp.keys_path = find_default_keys(g_imp.base_path);
                if (g_imp.out_dir.empty() && !g_imp.base_path.empty()) {
                    std::string bd = dirname_of(g_imp.base_path);
                    g_imp.out_dir = (bd.empty() ? "." : bd) + "/extracted_game";
                }
            }
            if (!g_imp.pick_upd.empty()) { g_imp.upd_path = g_imp.pick_upd; g_imp.pick_upd.clear(); }
            if (!g_imp.pick_keys.empty()) { g_imp.keys_path = g_imp.pick_keys; g_imp.pick_keys.clear(); }
            if (!g_imp.pick_out.empty()) { g_imp.out_dir = g_imp.pick_out; g_imp.pick_out.clear(); }
            if (g_imp.finished) {
                g_imp.finished = false;
                pthread_t tid = g_imp.tid;
                g_imp.tid = 0;
                int rc = g_imp.exit_code;
                bool was_cancel = g_imp.cancel_req;
                std::string outd = g_imp.out_dir;
                std::string tail = read_tail_text(g_imp.log_path, 400);
                pthread_mutex_unlock(&g_imp_mu);
                if (tid) pthread_join(tid, NULL);

                pthread_mutex_lock(&g_imp_mu);
                if (was_cancel) {
                    g_imp.status_msg = ui_tr(STR_IMP_CANCELLED);
                    g_imp.status_is_err = true;
                } else if (rc == 0) {
                    g_imp.status_msg = ui_tr(STR_IMP_DONE_OK);
                    g_imp.status_is_err = false;
                } else {
                    char head[256];
                    snprintf(head, sizeof head, ui_tr(STR_IMP_DONE_FAIL_FMT), rc);
                    g_imp.status_msg = std::string(head) + (tail.empty() ? "" : ("\n" + tail));
                    g_imp.status_is_err = true;
                }
                pthread_mutex_unlock(&g_imp_mu);

                if (!was_cancel && rc == 0 && !outd.empty()) {
                    if (!dev_extracted) {
                        active_dir = outd;
                        strlcpy(g_settings.game_dir, active_dir.c_str(), sizeof g_settings.game_dir);
                        settings_save();
                    }
                    chk = validate_game_dir(active_dir);
                    chk_lang = ui_resolve_lang();
                }
            } else {
                extra_font_paths += "\n" + g_imp.base_path + "\n" + g_imp.upd_path + "\n" + g_imp.keys_path + "\n" + g_imp.out_dir;
                pthread_mutex_unlock(&g_imp_mu);
            }
        }

        // Handle async game-module self-build completion
        {
            pthread_mutex_lock(&g_bld_mu);
            bool bfin = g_bld.finished;
            if (bfin) {
                g_bld.finished = false;
                pthread_t tid = g_bld.tid;
                g_bld.tid = 0;
                bool ok = g_bld.ok;
                bool auto_go = g_bld.auto_launch;
                std::string err = g_bld.err_msg;
                pthread_mutex_unlock(&g_bld_mu);
                if (tid) pthread_join(tid, NULL);

                chk = validate_game_dir(active_dir);
                chk_lang = ui_resolve_lang();
                pthread_mutex_lock(&g_bld_mu);
                if (ok && chk.ready) {
                    g_bld.status_notice = ui_tr(STR_BUILD_DONE_OK);
                    g_bld.notice_is_err = false;
                    if (auto_go) g_play_requested = true;
                } else if (ok) {
                    g_bld.status_notice = ui_tr(STR_BUILD_DONE_OK);
                    g_bld.notice_is_err = false;
                } else {
                    g_bld.status_notice = std::string(ui_tr(STR_BUILD_DONE_FAIL_PFX)) + err;
                    g_bld.notice_is_err = true;
                }
                g_bld.auto_launch = false;
                pthread_mutex_unlock(&g_bld_mu);
            } else {
                pthread_mutex_unlock(&g_bld_mu);
            }
        }

        menu_apply_window_requests(win);

        // HiDPI & live UI language font rebuild cleanly BEFORE starting GPU frame
        compute_hidpi_metrics(win, &sc, &pix_density, &raster_scale);
        int cur_ui_lang = ui_resolve_lang();
        bool path_missing = (extra_font_paths != built_dir && !extra_font_paths.empty() && f_small &&
                             ui_font_text_missing(f_small, extra_font_paths.c_str()));
        if (cur_ui_lang != built_lang || fabsf(raster_scale - built_raster) > 0.04f || path_missing) {
#ifndef __APPLE__
            ui_load_i18n_fonts(&f_small, &f_menu, &f_title,
                                28.0f * raster_scale, 33.0f * raster_scale, 38.0f * raster_scale,
                                cur_ui_lang, extra_font_paths.c_str(), getenv("LARECOMP_LAUNCHER_FONT"));
            vk_imgui_fonts_rebuild();
#endif
            built_lang = cur_ui_lang;
            built_raster = raster_scale;
            built_dir = extra_font_paths;
            style_built_sc = -1.0f;
        }
        if (chk_lang != cur_ui_lang) {
            chk = validate_game_dir(active_dir);
            chk_lang = cur_ui_lang;
        }
        if (fabsf(style_built_sc - sc) > 0.01f) {
            ui_apply_sentinel_theme(sc);
            style_built_sc = sc;
        }

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            input_cursor_event(&e);
            if (view == VIEW_SETTINGS && bind_capture_event(&e)) continue;
            if (!launcher_pad_event(e)) ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) {
                if (boot_started) pthread_join(boot_tid, NULL);
                exit(0);
            }
        }

#ifdef __APPLE__
        int w, h;
        SDL_GetWindowSizeInPixels(win, &w, &h);
        layer.drawableSize = CGSizeMake(w, h);
        @autoreleasepool {
            id<CAMetalDrawable> drawable = [layer nextDrawable];
            if (!drawable) { SDL_Delay(16); continue; }
            MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
            rp.colorAttachments[0].texture = drawable.texture;
            rp.colorAttachments[0].loadAction = MTLLoadActionClear;
            rp.colorAttachments[0].clearColor = MTLClearColorMake(0.04, 0.06, 0.09, 1);
            rp.colorAttachments[0].storeAction = MTLStoreActionStore;
            id<MTLCommandBuffer> cb = [queue commandBuffer];
            id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
            ImGui_ImplMetal_NewFrame(rp);
#else
        {
            static uint64_t next_frame;
            const SDL_DisplayMode *dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(win));
            uint64_t period = (uint64_t)(1e9 / (dm && dm->refresh_rate > 1 ? dm->refresh_rate : 60.0f));
            uint64_t now = plat_now_ns();
            bool on_time = next_frame > now && next_frame - now <= period;
            if (on_time) plat_sleep_until_ns(next_frame);
            next_frame = (on_time ? next_frame : now) + period;
            static const float clear_col[4] = {0.035f, 0.055f, 0.088f, 1.0f};
            if (!vk_frame_begin(clear_col)) { SDL_Delay(16); continue; }
            ImGui_ImplVulkan_NewFrame();
#endif
            if (input_cursor_hidden()) io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
            else io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
            ImGui_ImplSDL3_NewFrame();
            if (SDL_HasGamepad()) io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
            ImGui::NewFrame();

            // Fonts are already rasterized at physical pixels (sc * pix_density); only invert pix_density for logical points
            io.FontGlobalScale = 1.0f / pix_density;
            ImVec2 ds = io.DisplaySize;

            if (view == VIEW_LOADING) {
                double el = (SDL_GetTicks() / 1000.0) - boot_t0;
                int stg = __atomic_load_n(&boot_args.stage, __ATOMIC_ACQUIRE);
                ui_draw_loading_visual(el, stg, nullptr, sc, f_title, f_menu);
            } else {
                // Deep Sentinel background gradient
                ImDrawList *bg = ImGui::GetBackgroundDrawList();
                bg->AddRectFilledMultiColor(ImVec2(0, 0), ds,
                                            IM_COL32(8, 14, 24, 255), IM_COL32(14, 26, 40, 255),
                                            IM_COL32(8, 12, 20, 255), IM_COL32(5, 8, 14, 255));

                const ImVec4 gold(0.95f, 0.79f, 0.36f, 1.0f);
                const ImVec4 cyan(0.32f, 0.88f, 0.96f, 1.0f);
                const ImVec4 green(0.42f, 0.90f, 0.58f, 1.0f);
                const ImVec4 red(1.0f, 0.48f, 0.44f, 1.0f);

                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(ds);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
                ImGui::PushFont(f_small);
                ImGui::Begin("##launcher_main", NULL,
                             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

                float margin = 32.0f * sc;
                float content_w = fminf(ds.x - margin * 2.0f, 1520.0f * sc);
                float start_x = (ds.x - content_w) * 0.5f;
                ImGui::SetCursorPos(ImVec2(start_x, 22.0f * sc));

                // ---- Top Header Bar (Title left | UI Language + Nav buttons right via BeginTable)
                if (ImGui::BeginTable("##topbar_tbl", 2, ImGuiTableFlags_SizingStretchProp, ImVec2(content_w, 0))) {
                    ImGui::TableSetupColumn("title", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    ImGui::TableSetupColumn("actions", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::PushFont(f_title);
                    ImGui::TextColored(gold, "%s", ui_tr(STR_APP_TITLE));
                    ImGui::PopFont();
                    ImGui::TextDisabled("13SentinelsRecomp %s  ·  %s", LARECOMP_VERSION, ui_tr(STR_APP_SUBTITLE));
                    sync_win_title(cur_ui_lang);

                    ImGui::TableNextColumn();
                    int ui_n = 0;
                    const char *const *ui_lbls = ui_lang_selector_labels(&ui_n);
                    float top_btn_h = 40.0f * sc;
                    float combo_w = 172.0f * sc;
                    float btn_w = fmaxf(106.0f * sc, 5.8f * ImGui::GetFontSize());
                    float exit_w = fmaxf(90.0f * sc, 5.0f * ImGui::GetFontSize());
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0f * sc);

                    ImGui::SetNextItemWidth(combo_w);
                    if (ImGui::Combo("##top_ui_lang", &g_settings.ui_lang, ui_lbls, ui_n)) {
                        settings_save();
                    }
                    if (open_lang_popup && frame >= 2) {
                        ImGui::OpenPopupEx(ImHashStr("##ComboPopup", 0, ImGui::GetItemID()), ImGuiPopupFlags_None);
                        open_lang_popup = false;
                    }
                    ImGui::SameLine(0, 8.0f * sc);

                    auto nav_btn = [&](const char *lbl, bool active) -> bool {
                        if (active) {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.36f, 0.48f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_Border, cyan);
                            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                        }
                        bool hit = ImGui::Button(lbl, ImVec2(btn_w, top_btn_h));
                        if (active) { ImGui::PopStyleVar(); ImGui::PopStyleColor(2); }
                        return hit;
                    };
                    if (nav_btn(ui_tr(STR_NAV_HOME), view == VIEW_HOME)) {
                        bind_capture_cancel();
                        settings_save();
                        view = VIEW_HOME;
                    }
                    ImGui::SameLine(0, 8.0f * sc);
                    if (nav_btn(ui_tr(STR_NAV_SETTINGS), view == VIEW_SETTINGS)) {
                        static bool opened_once = false;
                        menu_tabs_focus(!opened_once);
                        opened_once = true;
                        view = VIEW_SETTINGS;
                    }
                    ImGui::SameLine(0, 8.0f * sc);
                    if (ImGui::Button(ui_tr(STR_NAV_EXIT), ImVec2(exit_w, top_btn_h))) {
                        settings_save();
                        exit(0);
                    }
                    ImGui::EndTable();
                }

                ImGui::SetCursorPosX(start_x);
                ImGui::Separator();
                ImGui::Spacing();

                float footer_h = 34.0f * sc;
                float body_h = fmaxf(120.0f * sc, ImGui::GetContentRegionAvail().y - footer_h - ImGui::GetStyle().ItemSpacing.y);

                if (view == VIEW_HOME) {
                    bool any_popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
                    if (!any_popup) {
                        if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) {
                            menu_tabs_focus(false);
                            g_tab_request_ext = 0;
                            view = VIEW_SETTINGS;
                        } else if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) {
                            menu_tabs_focus(false);
                            g_tab_request_ext = 9;
                            view = VIEW_SETTINGS;
                        }
                    }

                    ImGui::SetCursorPosX(start_x);
                    ImGui::BeginChild("##home_scroll", ImVec2(content_w, body_h), ImGuiChildFlags_None);

                    float col_gap = 20.0f * sc;
                    float left_w = content_w * 0.57f;
                    float right_w = content_w - left_w - col_gap;

                    // ---- Left Column: Resource Validation + Game Lang + Play CTA
                    ImGui::BeginChild("##left_col", ImVec2(left_w, 0), ImGuiChildFlags_None);

                    ImGui::BeginChild("##res_card", ImVec2(left_w, 0),
                                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
                    ImGui::PushFont(f_menu);
                    ImGui::TextColored(gold, "%s", ui_tr(STR_CARD_RES_TITLE));
                    ImGui::PopFont();
                    ImGui::Separator();
                    ImGui::Spacing();

                    ImGui::PushTextWrapPos(0);
                    ImGui::TextDisabled("%s%s", ui_tr(STR_LBL_WORKSPACE), rt_data_root());
                    if (dev_extracted) {
                        ImGui::TextColored(green, "%s%s", ui_tr(STR_LBL_PORTABLE_ACTIVE), active_dir.c_str());
                    } else {
                        ImGui::Text("%s%s", ui_tr(STR_LBL_GAME_DIR),
                                    active_dir.empty() ? ui_tr(STR_NOT_SET) : active_dir.c_str());
                    }
                    ImGui::PopTextWrapPos();
                    ImGui::Spacing();

                    bool bld_running = false, bld_notice_err = false;
                    double bld_frac = 0.0;
                    std::string bld_step, bld_notice;
                    {
                        pthread_mutex_lock(&g_bld_mu);
                        bld_running = g_bld.running;
                        bld_frac = g_bld.frac;
                        bld_step = g_bld.step;
                        bld_notice = g_bld.status_notice;
                        bld_notice_err = g_bld.notice_is_err;
                        pthread_mutex_unlock(&g_bld_mu);
                    }
                    bool imp_busy = false;
                    {
                        pthread_mutex_lock(&g_imp_mu);
                        imp_busy = g_imp.running;
                        pthread_mutex_unlock(&g_imp_mu);
                    }
                    auto start_local_build = [&](bool auto_launch) {
                        std::string dir = chk.resolved_root.empty() ? active_dir : chk.resolved_root;
                        if (dir.empty()) return;
                        pthread_mutex_lock(&g_bld_mu);
                        if (!g_bld.running) {
                            g_bld.target_dir = dir;
                            g_bld.cancel_req = false;
                            g_bld.auto_launch = auto_launch;
                            g_bld.frac = 0.0;
                            g_bld.step.clear();
                            g_bld.err_msg.clear();
                            g_bld.status_notice.clear();
                            g_bld.notice_is_err = false;
                            g_bld.finished = false;
                            g_bld.running = (pthread_create(&g_bld.tid, NULL, build_worker_fn, NULL) == 0);
                            if (g_bld.running) bld_running = true;
                        }
                        pthread_mutex_unlock(&g_bld_mu);
                    };

                    if (ImGui::BeginTable("##dir_btns_tbl", active_dir.empty() ? 2 : 3, ImGuiTableFlags_SizingStretchSame)) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::BeginDisabled(dev_extracted || bld_running || imp_busy);
                        if (ImGui::Button(ui_tr(STR_BTN_PICK_DIR), ImVec2(-FLT_MIN, 0))) {
                            SDL_ShowOpenFolderDialog(on_folder_picked, NULL, win,
                                                     active_dir.empty() ? NULL : active_dir.c_str(), false);
                        }
                        ImGui::EndDisabled();
                        ImGui::TableNextColumn();
                        ImGui::BeginDisabled(bld_running || imp_busy);
                        if (ImGui::Button(ui_tr(STR_BTN_REVALIDATE), ImVec2(-FLT_MIN, 0))) {
                            chk = validate_game_dir(active_dir);
                            chk_lang = cur_ui_lang;
                        }
                        ImGui::EndDisabled();
                        if (!active_dir.empty()) {
                            ImGui::TableNextColumn();
                            if (ImGui::Button(ui_tr(STR_BTN_OPEN_DIR), ImVec2(-FLT_MIN, 0))) {
                                plat_open_folder(active_dir.c_str());
                            }
                        }
                        ImGui::EndTable();
                    }

                    static bool s_nx_checked = false;
                    static bool s_has_nx = false;
                    if (!s_nx_checked) {
                        s_has_nx = !find_nx_extract_exe().empty();
                        s_nx_checked = true;
                    }
                    if (s_has_nx) {
                        ImGui::BeginDisabled(bld_running);
                        if (ImGui::Button(ui_tr(STR_BTN_IMPORT_PKG), ImVec2(-FLT_MIN, 0))) {
                            pthread_mutex_lock(&g_imp_mu);
                            if (g_imp.nx_exe.empty()) g_imp.nx_exe = find_nx_extract_exe();
                            if (g_imp.keys_path.empty()) g_imp.keys_path = find_default_keys(g_imp.base_path);
                            if (g_imp.out_dir.empty() && !active_dir.empty() && !chk.has_images) g_imp.out_dir = active_dir;
                            g_imp.open_popup = true;
                            pthread_mutex_unlock(&g_imp_mu);
                        }
                        ImGui::EndDisabled();
                    } else {
                        ImGui::PushTextWrapPos(0);
                        ImGui::TextDisabled("%s", ui_tr(STR_IMP_DIRECT_HINT));
                        ImGui::PopTextWrapPos();
                    }

                    if (dev_extracted) {
                        ImGui::PushTextWrapPos(0);
                        ImGui::TextDisabled("%s", ui_tr(STR_HINT_PORTABLE));
                        ImGui::PopTextWrapPos();
                    }
                    if (!picker_notice.empty()) {
                        ImGui::PushTextWrapPos(0);
                        ImGui::TextColored(red, "%s", picker_notice.c_str());
                        ImGui::PopTextWrapPos();
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    bool files_ok = chk.exefs_ok && chk.romfs_ok;
                    ImGui::TextColored(files_ok ? green : red, "%s %s", files_ok ? "✓" : "×", chk.headline.c_str());
                    if (!chk.detail.empty()) {
                        ImGui::PushTextWrapPos(0);
                        ImGui::TextColored(files_ok ? ImVec4(0.84f, 0.90f, 0.94f, 1.0f) : ImVec4(0.96f, 0.78f, 0.74f, 1.0f),
                                           "%s", chk.detail.c_str());
                        ImGui::PopTextWrapPos();
                    }
                    if (files_ok && chk.version_kind == V13_VER_US_100) {
                        ImGui::PushTextWrapPos(0);
                        ImGui::TextColored(ImVec4(0.95f, 0.79f, 0.36f, 1.0f), "%s", ui_tr(STR_VAL_US100_UPD_HINT));
                        ImGui::PopTextWrapPos();
                    }

                    ImGui::Spacing();
                    bool tools_avail = gamebuild_available();
                    if (chk.dll_ok) {
                        ImGui::TextColored(green, "%s", ui_tr(STR_DLL_OK));
                    } else if (bld_running) {
                        ImGui::PushTextWrapPos(0);
                        ImGui::TextColored(cyan, "%s%s (%.0f%%)", ui_tr(STR_BUILD_RUNNING_PFX),
                                           bld_step.empty() ? "..." : bld_step.c_str(), bld_frac * 100.0);
                        ImGui::PopTextWrapPos();
                        ImGui::ProgressBar((float)bld_frac, ImVec2(-FLT_MIN, 24.0f * sc));
                        if (ImGui::Button(ui_tr(STR_BUILD_CANCEL_BTN), ImVec2(-FLT_MIN, 0))) {
                            pthread_mutex_lock(&g_bld_mu);
                            g_bld.cancel_req = true;
                            pthread_mutex_unlock(&g_bld_mu);
                        }
                    } else {
                        ImGui::TextColored(red, "%s", ui_tr(STR_DLL_BAD));
                        ImGui::PushTextWrapPos(0);
                        ImGui::TextDisabled("%s", tools_avail ? ui_tr(STR_DLL_BAD_HINT) : ui_tr(STR_BUILD_TOOLS_MISS));
                        ImGui::PopTextWrapPos();
                        ImGui::BeginDisabled(!files_ok || !tools_avail || imp_busy);
                        if (ImGui::Button(ui_tr(STR_BTN_M3B_BUILD), ImVec2(-FLT_MIN, 0))) {
                            start_local_build(false);
                        }
                        ImGui::EndDisabled();
                    }
                    if (!bld_running && !bld_notice.empty()) {
                        ImGui::PushTextWrapPos(0);
                        ImGui::TextColored(bld_notice_err ? red : green, "%s", bld_notice.c_str());
                        ImGui::PopTextWrapPos();
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    // Game Language aligned selector row
                    if (ImGui::BeginTable("##home_lang_tbl", 2, ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn("lbl", ImGuiTableColumnFlags_WidthStretch, 0.42f);
                        ImGui::TableSetupColumn("ctrl", ImGuiTableColumnFlags_WidthStretch, 0.58f);
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextColored(cyan, "%s", ui_tr(STR_GAME_LANG_LABEL));
                        ImGui::TableNextColumn();
                        int gl_n = 0;
                        const char *const *gl_lbls = ui_game_lang_labels(&gl_n);
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::Combo("##home_game_lang", &g_settings.game_lang, gl_lbls, gl_n)) {
                            settings_save();
                        }
                        ImGui::EndTable();
                    }
                    if (is_game_lang_mismatch(chk.version_kind, g_settings.game_lang)) {
                        ImGui::PushTextWrapPos(0);
                        ImGui::TextColored(ImVec4(0.96f, 0.76f, 0.32f, 1.0f), "%s", ui_tr(STR_GAME_LANG_MISMATCH_WARN));
                        ImGui::PopTextWrapPos();
                    }
                    ImGui::EndChild();  // res_card

                    ImGui::Spacing();

                    // Card 2: Play CTA
                    bool can_start = files_ok && !bld_running && !imp_busy && (chk.ready || tools_avail);
                    ImGui::BeginChild("##play_card", ImVec2(left_w, 0),
                                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
                    ImGui::BeginDisabled(!can_start);
                    ImGui::PushStyleColor(ImGuiCol_Button, can_start ? ImVec4(0.14f, 0.46f, 0.56f, 1.0f) : ImVec4(0.18f, 0.20f, 0.24f, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.58f, 0.70f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Border, can_start ? cyan : ImVec4(0.3f, 0.35f, 0.4f, 0.5f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, can_start ? 2.0f : 1.0f);
                    ImGui::PushFont(f_menu);
                    if (ImGui::Button(ui_tr(STR_BTN_START_GAME), ImVec2(-FLT_MIN, 60.0f * sc))) {
                        if (chk.ready) g_play_requested = true;
                        else start_local_build(true);
                    }
                    ImGui::PopFont();
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(3);
                    ImGui::EndDisabled();
                    if (!can_start && !bld_running) {
                        ImGui::PushTextWrapPos(0);
                        ImGui::TextDisabled("%s", ui_tr(STR_START_REQ_HINT));
                        ImGui::PopTextWrapPos();
                    }
                    ImGui::Spacing();
                    if (ImGui::BeginTable("##skip_row", 2, ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn("chk", ImGuiTableColumnFlags_WidthFixed);
                        ImGui::TableSetupColumn("note", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        if (ImGui::Checkbox(ui_tr(STR_CHK_SKIP_LAUNCHER), &g_settings.skip_launcher)) {
                            settings_save();
                        }
                        ImGui::TableNextColumn();
                        ImGui::AlignTextToFramePadding();
                        ImGui::PushTextWrapPos(0);
                        ImGui::TextDisabled("%s", ui_tr(STR_HINT_SHIFT_OVERRIDE));
                        ImGui::PopTextWrapPos();
                        ImGui::EndTable();
                    }
                    ImGui::EndChild();  // play_card

                    ImGui::EndChild();  // left_col

                    ImGui::SameLine(0, col_gap);

                    // ---- Right Column: Save Card + Pre-launch Quick Settings
                    ImGui::BeginChild("##right_col", ImVec2(right_w, 0), ImGuiChildFlags_None);

                    ImGui::BeginChild("##save_card", ImVec2(right_w, 0),
                                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
                    ImGui::PushFont(f_menu);
                    ImGui::TextColored(gold, "%s", ui_tr(STR_CARD_SAVE_TITLE));
                    ImGui::PopFont();
                    ImGui::Separator();
                    ImGui::Spacing();

                    bool sv_exists = false;
                    uint64_t sv_size = 0;
                    int64_t sv_mtime = 0;
                    rt_save_info(&sv_exists, &sv_size, &sv_mtime);
                    if (sv_exists) {
                        time_t t = (time_t)sv_mtime;
                        struct tm tmv;
#ifdef _WIN32
                        localtime_s(&tmv, &t);
#else
                        localtime_r(&t, &tmv);
#endif
                        char tb[48];
                        strftime(tb, sizeof tb, "%Y-%m-%d %H:%M:%S", &tmv);
                        ImGui::TextColored(green, "%s", ui_tr(STR_SAVE_FOUND));
                        ImGui::Text(ui_tr(STR_SAVE_SIZE_FMT), sv_size / (1024.0 * 1024.0), (unsigned long long)sv_size);
                        ImGui::Text(ui_tr(STR_SAVE_MTIME_FMT), tb);
                    } else {
                        ImGui::TextDisabled("%s", ui_tr(STR_SAVE_NONE));
                        ImGui::PushTextWrapPos(0);
                        ImGui::TextDisabled("%s", ui_tr(STR_SAVE_NONE_HINT));
                        ImGui::PopTextWrapPos();
                    }
                    ImGui::Spacing();
                    if (ImGui::BeginTable("##save_btns_tbl", 2, ImGuiTableFlags_SizingStretchSame)) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        if (ImGui::Button(ui_tr(STR_BTN_OPEN_SAVE), ImVec2(-FLT_MIN, 0))) {
                            const char *dir = plat_is_dir("save/save_0") ? "save/save_0" : "save";
                            plat_mkdir_p("save");
                            char full[1200];
                            snprintf(full, sizeof full, "%s/%s", rt_data_root(), dir);
                            plat_open_folder(full);
                        }
                        ImGui::TableNextColumn();
                        ImGui::BeginDisabled(!sv_exists);
                        if (ImGui::Button(ui_tr(STR_BTN_BACKUP_SAVE), ImVec2(-FLT_MIN, 0))) {
                            char rel[256], err[256];
                            if (rt_save_backup(rel, sizeof rel, err, sizeof err)) {
                                backup_msg = std::string(ui_tr(STR_BACKUP_OK_PFX)) + rel;
                                backup_err = false;
                            } else {
                                backup_msg = std::string(ui_tr(STR_BACKUP_FAIL_PFX)) + err;
                                backup_err = true;
                            }
                        }
                        ImGui::EndDisabled();
                        ImGui::EndTable();
                    }
                    if (!backup_msg.empty()) {
                        ImGui::Spacing();
                        ImGui::PushTextWrapPos(0);
                        ImGui::TextColored(backup_err ? red : green, "%s", backup_msg.c_str());
                        ImGui::PopTextWrapPos();
                    }
                    ImGui::EndChild();  // save_card

                    ImGui::Spacing();

                    ImGui::BeginChild("##quickset_card", ImVec2(right_w, 0),
                                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
                    ImGui::PushFont(f_menu);
                    ImGui::TextColored(gold, "%s", ui_tr(STR_CARD_QUICK_TITLE));
                    ImGui::PopFont();
                    ImGui::Separator();
                    ImGui::Spacing();
                    ImGui::PushTextWrapPos(0);
                    ImGui::TextWrapped("%s", ui_tr(STR_CARD_QUICK_DESC));
                    ImGui::PopTextWrapPos();
                    ImGui::Spacing();

                    if (ImGui::BeginTable("##quick_grid", 2, ImGuiTableFlags_SizingStretchSame)) {
                        auto open_tab = [&](const char *lbl, int tab_id) {
                            ImGui::TableNextColumn();
                            if (ImGui::Button(lbl, ImVec2(-FLT_MIN, 42.0f * sc))) {
                                menu_tabs_focus(false);
                                g_tab_request_ext = tab_id;
                                view = VIEW_SETTINGS;
                            }
                        };
                        ImGui::TableNextRow();
                        open_tab(ui_tr(STR_BTN_QUICK_DISPLAY), 0);
                        open_tab(ui_tr(STR_BTN_QUICK_AUDIO), 1);
                        ImGui::TableNextRow();
                        open_tab(ui_tr(STR_BTN_QUICK_CONTROLS), 8);
                        open_tab(ui_tr(STR_BTN_QUICK_SYSTEM), 9);
                        ImGui::EndTable();
                    }
                    ImGui::EndChild();  // quickset_card

                    ImGui::EndChild();  // right_col
                    ImGui::EndChild();  // home_scroll
                } else {
                    // VIEW_SETTINGS: Shared 4-tab menu window
                    ImGui::SetCursorPosX(start_x);
                    ImGui::BeginChild("##launcher_settings", ImVec2(content_w, body_h),
                                      ImGuiChildFlags_Borders, ImGuiWindowFlags_NavFlattened);
                    bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
                    bool capturing_bind = bind_capture_active(NULL, NULL, NULL);
                    menu_tabs_draw(sc, f_small, f_menu, false, NULL);
                    if (!popup && !capturing_bind && !controls_ui_took_back() &&
                        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                        (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
                        settings_save();
                        view = VIEW_HOME;
                    }
                    ImGui::EndChild();
                }

                // ---- Bottom Shortcut Hint Bar
                ImGui::SetCursorPosX(start_x);
                ImGui::Separator();
                ImGui::SetCursorPosX(start_x);
                ImGui::TextColored(ImVec4(0.58f, 0.78f, 0.88f, 0.95f), ui_tr(STR_HINT_BAR_FMT),
                                   ui_pad_confirm_label(), ui_pad_back_label());

                // ---- NSP/XCI & Update Importer Modal
                {
                    bool want_open = false;
                    pthread_mutex_lock(&g_imp_mu);
                    if (g_imp.open_popup) { want_open = true; g_imp.open_popup = false; }
                    pthread_mutex_unlock(&g_imp_mu);
                    if (want_open) ImGui::OpenPopup("##import_pkg_modal");

                    ImGui::SetNextWindowSize(ImVec2(fminf(ds.x * 0.88f, 960.0f * sc), 0), ImGuiCond_Always);
                    if (ImGui::BeginPopupModal("##import_pkg_modal", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize)) {
                        pthread_mutex_lock(&g_imp_mu);
                        bool running = g_imp.running;
                        std::string nx_exe = g_imp.nx_exe;
                        std::string base_p = g_imp.base_path;
                        std::string upd_p = g_imp.upd_path;
                        std::string keys_p = g_imp.keys_path;
                        std::string out_d = g_imp.out_dir;
                        std::string msg = g_imp.status_msg;
                        bool msg_err = g_imp.status_is_err;
                        pthread_mutex_unlock(&g_imp_mu);

                        ImGui::PushFont(f_menu);
                        ImGui::TextColored(gold, "%s", ui_tr(STR_IMP_TITLE));
                        ImGui::PopFont();
                        ImGui::Separator();
                        ImGui::PushTextWrapPos(0);
                        ImGui::TextWrapped("%s", ui_tr(STR_IMP_DESC));
                        ImGui::Spacing();

                        if (!nx_exe.empty()) {
                            ImGui::TextColored(green, "%s%s", ui_tr(STR_IMP_TOOL_OK), nx_exe.c_str());
                        } else {
                            ImGui::TextColored(red, "%s", ui_tr(STR_IMP_TOOL_MISS));
                        }
                        ImGui::PopTextWrapPos();
                        ImGui::Spacing();

                        static const SDL_DialogFileFilter k_img_filters[] = {{"Switch NSP/XCI", "nsp;xci;nsz;xcz"}};
                        static const SDL_DialogFileFilter k_upd_filters[] = {{"Switch Update NSP", "nsp;nsz"}};
                        static const SDL_DialogFileFilter k_key_filters[] = {{"Switch Keys", "keys"}};

                        ImGui::BeginDisabled(running);
                        if (ImGui::BeginTable("##imp_rows", 3, ImGuiTableFlags_SizingStretchProp)) {
                            ImGui::TableSetupColumn("lbl", ImGuiTableColumnFlags_WidthStretch, 0.37f);
                            ImGui::TableSetupColumn("val", ImGuiTableColumnFlags_WidthStretch, 0.43f);
                            ImGui::TableSetupColumn("btn", ImGuiTableColumnFlags_WidthFixed, 182.0f * sc);

                            auto row_browse = [&](const char *id, UiStr lbl, const std::string &val, const char *empty_hint, auto &&on_browse, const std::function<void()> *on_clear) {
                                ImGui::TableNextRow();
                                ImGui::TableNextColumn();
                                ImGui::AlignTextToFramePadding();
                                ImGui::PushTextWrapPos(0);
                                ImGui::TextColored(cyan, "%s", ui_tr(lbl));
                                ImGui::PopTextWrapPos();
                                ImGui::TableNextColumn();
                                ImGui::AlignTextToFramePadding();
                                ImGui::PushTextWrapPos(0);
                                if (val.empty()) ImGui::TextDisabled("%s", empty_hint);
                                else ImGui::Text("%s", val.c_str());
                                ImGui::PopTextWrapPos();
                                ImGui::TableNextColumn();
                                std::string bid = std::string(ui_tr(STR_IMP_BTN_BROWSE)) + "##" + id;
                                if (on_clear) {
                                    float half = (172.0f * sc) * 0.54f;
                                    if (ImGui::Button(bid.c_str(), ImVec2(half, 0))) on_browse();
                                    ImGui::SameLine(0, 6.0f * sc);
                                    std::string cid = std::string(ui_tr(STR_IMP_BTN_CLEAR)) + "##" + id;
                                    if (ImGui::Button(cid.c_str(), ImVec2(172.0f * sc - half - 6.0f * sc, 0))) (*on_clear)();
                                } else {
                                    if (ImGui::Button(bid.c_str(), ImVec2(172.0f * sc, 0))) on_browse();
                                }
                            };

                            row_browse("base", STR_IMP_BASE_LBL, base_p, ui_tr(STR_NOT_SET), [&]() {
                                SDL_ShowOpenFileDialog(on_imp_base_picked, NULL, win, k_img_filters, 1,
                                                       base_p.empty() ? NULL : base_p.c_str(), false);
                            }, nullptr);

                            std::function<void()> clr_upd = [&]() {
                                pthread_mutex_lock(&g_imp_mu);
                                g_imp.upd_path.clear();
                                pthread_mutex_unlock(&g_imp_mu);
                            };
                            row_browse("upd", STR_IMP_UPD_LBL, upd_p, ui_tr(STR_IMP_OPT_NONE), [&]() {
                                SDL_ShowOpenFileDialog(on_imp_upd_picked, NULL, win, k_upd_filters, 1,
                                                       upd_p.empty() ? (base_p.empty() ? NULL : dirname_of(base_p).c_str()) : upd_p.c_str(), false);
                            }, &clr_upd);

                            row_browse("keys", STR_IMP_KEYS_LBL, keys_p, ui_tr(STR_NOT_SET), [&]() {
                                SDL_ShowOpenFileDialog(on_imp_keys_picked, NULL, win, k_key_filters, 1,
                                                       keys_p.empty() ? NULL : keys_p.c_str(), false);
                            }, nullptr);

                            row_browse("out", STR_IMP_OUT_LBL, out_d, ui_tr(STR_NOT_SET), [&]() {
                                SDL_ShowOpenFolderDialog(on_imp_out_picked, NULL, win,
                                                         out_d.empty() ? NULL : out_d.c_str(), false);
                            }, nullptr);

                            ImGui::EndTable();
                        }
                        ImGui::EndDisabled();

                        ImGui::Spacing();
                        if (running) {
                            ImGui::TextColored(cyan, "%s", ui_tr(STR_IMP_RUNNING));
                        } else if (!msg.empty()) {
                            ImGui::PushTextWrapPos(0);
                            ImGui::TextColored(msg_err ? red : green, "%s", msg.c_str());
                            ImGui::PopTextWrapPos();
                        }
                        ImGui::Spacing();

                        bool can_run = !running && !nx_exe.empty() && !base_p.empty() && !keys_p.empty() && !out_d.empty();
                        if (running) {
                            if (ImGui::Button(ui_tr(STR_IMP_BTN_CANCEL), ImVec2(180.0f * sc, 42.0f * sc))) {
                                pthread_mutex_lock(&g_imp_mu);
                                g_imp.cancel_req = true;
                                pthread_mutex_unlock(&g_imp_mu);
                            }
                        } else {
                            ImGui::BeginDisabled(!can_run);
                            ImGui::PushStyleColor(ImGuiCol_Button, can_run ? ImVec4(0.14f, 0.46f, 0.56f, 1.0f) : ImVec4(0.18f, 0.20f, 0.24f, 0.7f));
                            if (ImGui::Button(ui_tr(STR_IMP_BTN_RUN), ImVec2(260.0f * sc, 42.0f * sc))) {
                                pthread_mutex_lock(&g_imp_mu);
                                g_imp.running = true;
                                g_imp.finished = false;
                                g_imp.cancel_req = false;
                                g_imp.status_msg.clear();
                                g_imp.log_path = std::string(rt_data_root()) + "/nx_import.log";
                                g_imp.running = (pthread_create(&g_imp.tid, NULL, import_worker_fn, NULL) == 0);
                                if (!g_imp.running) {
                                    g_imp.status_msg = ui_tr(STR_IMP_ERR_REQ);
                                    g_imp.status_is_err = true;
                                }
                                pthread_mutex_unlock(&g_imp_mu);
                            }
                            ImGui::PopStyleColor();
                            ImGui::EndDisabled();
                            ImGui::SameLine(0, 12.0f * sc);
                            if (ImGui::Button(ui_tr(STR_IMP_BTN_CLOSE), ImVec2(140.0f * sc, 42.0f * sc))) {
                                ImGui::CloseCurrentPopup();
                            }
                        }
                        ImGui::EndPopup();
                    }
                }

                ImGui::End();
                ImGui::PopFont();
                ImGui::PopStyleColor();
                osk_draw(f_menu, sc);
            }

            ImGui::Render();
#ifdef __APPLE__
            ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cb, enc);
            [enc endEncoding];
            id<MTLTexture> shot = nil;
            if (shot_at && ++frame == shot_at) {
                MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:drawable.texture.pixelFormat
                                             width:drawable.texture.width height:drawable.texture.height mipmapped:NO];
                td.storageMode = MTLStorageModeShared;
                shot = [dev newTextureWithDescriptor:td];
                id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
                [bl copyFromTexture:drawable.texture toTexture:shot];
                [bl endEncoding];
            }
            [cb presentDrawable:drawable];
            [cb commit];
            if (shot) {
                [cb waitUntilCompleted];
                save_png(shot, getenv("LARECOMP_SHOT_PATH") ? getenv("LARECOMP_SHOT_PATH") : "build/launcher.png");
                exit(0);
            }
#else
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), (VkCommandBuffer)vk_frame_cmd());
            bool shot = shot_at && ++frame == shot_at;
            vk_frame_end(shot ? (getenv("LARECOMP_SHOT_PATH") ? getenv("LARECOMP_SHOT_PATH") : "build/launcher.png") : NULL);
            if (shot) exit(0);
#endif
        }
    }

#ifdef __APPLE__
    ImGui_ImplMetal_Shutdown();
#else
    vk_imgui_backend_shutdown();
#endif
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(ctx);
    ImGui::SetCurrentContext(prev_ctx);
    g_start_screen = 0;
}

// Stage-A animated loading splash for `skip_launcher` interactive startups
extern "C" void launcher_loading_boot(const char *root) {
    ui_window_init();
    SDL_Window *win = ui_window();

    IMGUI_CHECKVERSION();
    ImGuiContext *prev_ctx = ImGui::GetCurrentContext();
    ImGuiContext *ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;

    float sc = 1.0f, pix_density = 1.0f, raster_scale = 1.0f;
    compute_hidpi_metrics(win, &sc, &pix_density, &raster_scale);
    ImFont *f_small = nullptr, *f_menu = nullptr, *f_title = nullptr;
    ui_load_i18n_fonts(&f_small, &f_menu, &f_title,
                        28.0f * raster_scale, 33.0f * raster_scale, 44.0f * raster_scale,
                        ui_resolve_lang(), nullptr, getenv("LARECOMP_LAUNCHER_FONT"));
    ui_apply_sentinel_theme(sc);

#ifdef __APPLE__
    CAMetalLayer *layer = (__bridge CAMetalLayer *)ui_metal_layer();
    id<MTLDevice> dev = (__bridge id<MTLDevice>)ui_metal_device();
    id<MTLCommandQueue> queue = [dev newCommandQueue];
    ImGui_ImplSDL3_InitForMetal(win);
    ImGui_ImplMetal_Init(dev);
#else
    ImGui_ImplSDL3_InitForVulkan(win);
    vk_imgui_backend_init();
#endif

    BootWorkerArgs boot_args;
    boot_args.root = root ? root : "extracted";
    boot_args.stage = 0;
    boot_args.done.store(false, std::memory_order_release);
    pthread_t boot_tid = 0;
    double boot_t0 = SDL_GetTicks() / 1000.0;
    bool boot_started = (pthread_create(&boot_tid, NULL, boot_worker_fn, &boot_args) == 0);
    if (!boot_started) {
        boot_prepare(boot_args.root.c_str(), NULL);
        boot_args.done.store(true, std::memory_order_release);
    }

    while (!boot_args.done.load(std::memory_order_acquire) || (SDL_GetTicks() / 1000.0) - boot_t0 < 0.25) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                if (boot_started) pthread_join(boot_tid, NULL);
                exit(0);
            }
        }
#ifdef __APPLE__
        break;
#else
        static const float clear_col[4] = {0.035f, 0.055f, 0.088f, 1.0f};
        if (!vk_frame_begin(clear_col)) { SDL_Delay(16); continue; }
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        io.FontGlobalScale = 1.0f / pix_density;
        double el = (SDL_GetTicks() / 1000.0) - boot_t0;
        int stg = __atomic_load_n(&boot_args.stage, __ATOMIC_ACQUIRE);
        ui_draw_loading_visual(el, stg, nullptr, sc, f_title, f_menu);
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), (VkCommandBuffer)vk_frame_cmd());
        vk_frame_end(NULL);
        SDL_Delay(16);
#endif
    }
    if (boot_started) pthread_join(boot_tid, NULL);

#ifdef __APPLE__
    ImGui_ImplMetal_Shutdown();
#else
    vk_imgui_backend_shutdown();
#endif
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(ctx);
    ImGui::SetCurrentContext(prev_ctx);
}
