// In-game overlay menu built with Dear ImGui (Metal renderer on macOS, Vulkan elsewhere; input fed from SDL events).
//
// Threads: SDL events arrive on the main thread and are queued; the menu is laid out and drawn on the
// render thread inside backend_present, after the game image, into the same drawable.
#ifdef __APPLE__
#import <Metal/Metal.h>
#endif
#include <SDL3/SDL.h>
#include <math.h>
#include <pthread.h>
#include <atomic>
#include <string>
#include <vector>

#include "imgui.h"
#include "option_rows.h"
#include "imgui_internal.h"  // (NavCursorVisible)
#include "osk.h"
#include "pad_nav.h"
#include "ui_font.h"
#include "ui_i18n.h"
#ifdef __APPLE__
#include "imgui_impl_metal.h"
#else
#include "imgui_impl_vulkan.h"
extern "C" bool vk_imgui_backend_init(void);  // runtime/nvn/vulkan/imgui_glue.cpp
extern "C" void vk_imgui_fonts_rebuild(void);
// runtime/nvn/vulkan/device.c: the command buffer ImGui records into (a secondary one the recorder thread executes in
// the window image's commands, or that buffer itself after the recorder caught up), then handed back
extern "C" void *vk_overlay_begin(void *window_cmd);
extern "C" void vk_overlay_end(void *window_cmd, void *cmd);
#endif

extern "C" {
#include "../hle.h"
#include "../settings.h"
#include "menu.h"
void input_reset(void);
void save_editor_draw(float scale);  // save_editor.mm
extern "C" int game_in_play(void);  // hle/warp.c
void save_editor_on_open(void);
void rando_tab_draw(float scale);
bool warp_tab_draw(float scale);  // warp_tab.mm: true when a warp started
extern "C" void cheats_draw(float scale);  // cheats.mm
extern "C" void difficulty_draw(float scale);  // difficulty.mm
void controls_tab_draw(float scale);  // controls_ui.mm
bool controls_ui_took_back(void);
bool rando_tab_took_back(void);  // rando_tab.mm: B went back from the Triforce hunt's count
#include "../input_bind.h"
#include "../paths.h"
#include "../platform.h"
#include "../game_addresses.h"
#include "../nvn/renderer.h"
void ui_window_apply_pending(void);
void ui_window_set_title(const char *title);
}
#include <time.h>

static bool is_v13_ui(void) {
    const GameVersion *v = game_version();
    if (v) return strncmp(v->key, "v13_", 4) == 0;
    return !plat_is_dir("extracted/romfs/region_common");
}

// ------------------------------------------------------------------ state shared with the main thread
static std::atomic<bool> g_open{false};
static std::atomic<bool> g_text_busy{false};  // a text field / the on-screen keyboard has the keys (render thread: ui_text_busy)
static std::atomic<bool> g_want_fullscreen_apply{false};

struct QEvent {
    enum { KEY, ANALOG, MOUSE_POS, MOUSE_BUTTON, WHEEL, TEXT } kind;
    ImGuiKey key;
    bool down;
    float value, x, y;
    int button;
    char text[32];
};
static pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;
static std::vector<QEvent> q_events;
static float g_pixel_density = 1;

static void push(const QEvent &e) {
    pthread_mutex_lock(&q_lock);
    q_events.push_back(e);
    pthread_mutex_unlock(&q_lock);
}

bool menu_is_open(void) { return g_open.load(); }
static void set_open(bool open);
extern "C" void menu_test_open(void) {
    set_open(true);
    if (getenv("LARECOMP_MENU_TAB")) { extern int g_tab_request_ext; g_tab_request_ext = atoi(getenv("LARECOMP_MENU_TAB")); }
}

void menu_tabs_focus(bool first_tab);
static void set_open(bool open) {
    if (open == g_open.load()) return;
    g_open.store(open);
    input_reset();  // nothing held while the menu had focus leaks into the game
    bind_capture_cancel();
    if (open) {
        save_editor_on_open();
        static bool opened;  // the controller's cursor: the first tab (Controls) the first time this session, later the
        menu_tabs_focus(!opened);  // selected one
        opened = true;
    }
    else { settings_save(); osk_cancel(); g_text_busy = false; }
}

static ImGuiKey key_from_scancode(SDL_Scancode sc) {
    switch (sc) {
    case SDL_SCANCODE_UP: return ImGuiKey_UpArrow;
    case SDL_SCANCODE_DOWN: return ImGuiKey_DownArrow;
    case SDL_SCANCODE_LEFT: return ImGuiKey_LeftArrow;
    case SDL_SCANCODE_RIGHT: return ImGuiKey_RightArrow;
    case SDL_SCANCODE_RETURN: return ImGuiKey_Enter;
    case SDL_SCANCODE_KP_ENTER: return ImGuiKey_KeypadEnter;
    case SDL_SCANCODE_SPACE: return ImGuiKey_Space;
    case SDL_SCANCODE_TAB: return ImGuiKey_Tab;
    case SDL_SCANCODE_BACKSPACE: return ImGuiKey_Backspace;
    case SDL_SCANCODE_DELETE: return ImGuiKey_Delete;
    case SDL_SCANCODE_HOME: return ImGuiKey_Home;
    case SDL_SCANCODE_END: return ImGuiKey_End;
    case SDL_SCANCODE_PAGEUP: return ImGuiKey_PageUp;
    case SDL_SCANCODE_PAGEDOWN: return ImGuiKey_PageDown;
    case SDL_SCANCODE_LSHIFT: return ImGuiKey_LeftShift;
    case SDL_SCANCODE_RSHIFT: return ImGuiKey_RightShift;
    case SDL_SCANCODE_LCTRL: return ImGuiKey_LeftCtrl;
    case SDL_SCANCODE_RCTRL: return ImGuiKey_RightCtrl;
    case SDL_SCANCODE_LGUI: return ImGuiKey_LeftSuper;
    case SDL_SCANCODE_RGUI: return ImGuiKey_RightSuper;
    case SDL_SCANCODE_A: return ImGuiKey_A;
    case SDL_SCANCODE_C: return ImGuiKey_C;
    case SDL_SCANCODE_V: return ImGuiKey_V;
    case SDL_SCANCODE_X: return ImGuiKey_X;
    case SDL_SCANCODE_Z: return ImGuiKey_Z;
    case SDL_SCANCODE_Q: return ImGuiKey_Q;  // previous tab
    case SDL_SCANCODE_E: return ImGuiKey_E;  // next tab
    default: return ImGuiKey_None;
    }
}


// ---- software keyboard (runtime/hle/swkbd.c: the game's name entry etc.) --------------------------------------
// The game thread waits in menu_keyboard_run (re-presenting the last frame); this dialog takes the text: typed
// directly when the player last used a keyboard, an on-screen keyboard (d-pad + confirm, back deletes, Start / + OK; buttons as in pad_nav.h) when
// they last used a controller.
static std::atomic<int> g_kbd{0};  // 1 open, 2 confirmed, 3 cancelled
static std::atomic<bool> g_last_pad{false};
static char g_kbd_text[96];
static int g_kbd_max = 8;
static bool capturing() { return g_open.load() || g_kbd.load() == 1; }
// The game's name limit counts characters as the console's keyboard does, in UTF-16 units (a character past U+FFFF
// takes two); the text is UTF-8. Cuts s after max_units of them, only between characters (a character left incomplete
// - a cut inside it, or bytes that are no character - goes too, with the rest), and returns how many units are left.
static int kbd_cut(char *s, int max_units) {
    int units = 0;
    for (unsigned char *p = (unsigned char *)s; *p;) {
        int n = *p < 0x80 ? 1 : *p >= 0xc2 && *p <= 0xdf ? 2 : *p >= 0xe0 && *p <= 0xef ? 3 : *p >= 0xf0 && *p <= 0xf4 ? 4 : 0;
        int i = 1;
        while (i < n && (p[i] & 0xc0) == 0x80) i++;
        if (i == n && n > 2 && ((*p == 0xe0 && p[1] < 0xa0) || (*p == 0xed && p[1] > 0x9f) || (*p == 0xf0 && p[1] < 0x90) ||
                                (*p == 0xf4 && p[1] > 0x8f)))
            n = 0;  // (overlong, a surrogate or past U+10FFFF: the game's UTF-8 conversion refuses those too)
        int u = n == 4 ? 2 : 1;
        if (!n || i < n || units + u > max_units) { *p = 0; break; }
        units += u, p += n;
    }
    return units;
}
// The game gets no input while this is true (runtime/hle/input.c): menu or name entry up.
extern "C" bool menu_input_captured(void) { return capturing(); }

static bool vpad_on(void);  // (the on-screen pad wants the mouse even when no menu is up: defined below)

bool menu_handle_event(const SDL_Event *e) {
    static bool back_down, start_down;
    if (g_open.load() && bind_capture_event(e)) return true;  // Controls tab waiting for a new binding: it takes the press
    if (e->type == SDL_EVENT_KEY_DOWN) g_last_pad = false;
    if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) g_last_pad = true;
    if (g_kbd.load() == 1 && e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {  // Enter confirms, Esc cancels
        if (e->key.scancode == SDL_SCANCODE_RETURN || e->key.scancode == SDL_SCANCODE_KP_ENTER) { g_kbd = 2; return true; }
        if (e->key.scancode == SDL_SCANCODE_ESCAPE) { g_kbd = 3; return true; }
    }
    if (g_kbd.load() == 1 && e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e->gbutton.button == SDL_GAMEPAD_BUTTON_START) {
        g_kbd = 2;
        return true;
    }
    // typing (a text field, the on-screen keyboard, the name entry): keys are text, not menu shortcuts
    bool typing = g_kbd.load() == 1 || (g_open.load() && g_text_busy.load());
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
        if (!e->key.repeat && !typing && g_settings.hotkey_menu > 0 &&
            e->key.scancode == (SDL_Scancode)g_settings.hotkey_menu) {
            set_open(!g_open.load());
            return true;
        }
        if (!e->key.repeat && !typing && g_settings.hotkey_hud > 0 &&
            e->key.scancode == (SDL_Scancode)g_settings.hotkey_hud) {
            g_settings.hud_mode = (g_settings.hud_mode + 1) % 3;
            g_settings.show_fps = (g_settings.hud_mode != HUD_OFF);
            settings_save();
            return true;
        }
        if (!e->key.repeat && !typing && g_settings.hotkey_speed > 0 &&
            e->key.scancode == (SDL_Scancode)g_settings.hotkey_speed) {
            uint64_t m = rt_speed_mult();
            rt_set_speed_mult(m == 1 ? 2 : m == 2 ? 3 : 1);
            return true;
        }
        if (!e->key.repeat && !typing &&
            ((g_settings.hotkey_fullscreen > 0 && e->key.scancode == (SDL_Scancode)g_settings.hotkey_fullscreen) ||
             (e->key.scancode == SDL_SCANCODE_RETURN && (e->key.mod & (SDL_KMOD_ALT | SDL_KMOD_GUI))) ||
             (e->key.scancode == SDL_SCANCODE_F && (e->key.mod & SDL_KMOD_GUI) && (e->key.mod & SDL_KMOD_CTRL)))) {
            g_settings.window_mode = (g_settings.window_mode == WIN_WINDOWED) ? WIN_BORDERLESS : WIN_WINDOWED;
            g_settings.fullscreen = (g_settings.window_mode != WIN_WINDOWED);
            g_want_fullscreen_apply.store(true);
            settings_save();
            return true;
        }
        /* fallthrough */
    case SDL_EVENT_KEY_UP: {
        if (!capturing()) return false;
        bool down = e->type == SDL_EVENT_KEY_DOWN;
        if (down && e->key.scancode == SDL_SCANCODE_ESCAPE) {  // Esc: back out of popups, else close
            QEvent q = {};
            q.kind = QEvent::KEY; q.key = ImGuiKey_Escape; q.down = true; push(q);
            q.down = false; push(q);
            return true;
        }
        ImGuiKey k = key_from_scancode(e->key.scancode);
        if (k != ImGuiKey_None) { QEvent q = {}; q.kind = QEvent::KEY; q.key = k; q.down = down; push(q); }
        return true;
    }
    case SDL_EVENT_TEXT_INPUT:
        if (!capturing()) return false;
        { QEvent q = {}; q.kind = QEvent::TEXT; strlcpy(q.text, e->text.text, sizeof q.text); push(q); }
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        bool down = e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        if (e->gbutton.button == SDL_GAMEPAD_BUTTON_BACK) back_down = down;
        if (e->gbutton.button == SDL_GAMEPAD_BUTTON_START) start_down = down;
        if (down && !typing && ((back_down && start_down) || e->gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE)) {
            set_open(!g_open.load());
            return true;
        }
        if (!capturing()) return false;
        ImGuiKey k = ui_pad_key(e->gbutton.which, e->gbutton.button, down);  // (confirm / back: pad_nav.h)
        if (k != ImGuiKey_None) { QEvent q = {}; q.kind = QEvent::KEY; q.key = k; q.down = down; push(q); }
        return true;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        if (!capturing()) return false;
        float v = e->gaxis.value / 32767.0f;
        QEvent q = {};
        q.kind = QEvent::ANALOG;
        switch (e->gaxis.axis) {
        case SDL_GAMEPAD_AXIS_LEFTX:
            q.key = ImGuiKey_GamepadLStickLeft; q.value = v < -0.3f ? -v : 0; push(q);
            q.key = ImGuiKey_GamepadLStickRight; q.value = v > 0.3f ? v : 0; push(q);
            break;
        case SDL_GAMEPAD_AXIS_LEFTY:
            q.key = ImGuiKey_GamepadLStickUp; q.value = v < -0.3f ? -v : 0; push(q);
            q.key = ImGuiKey_GamepadLStickDown; q.value = v > 0.3f ? v : 0; push(q);
            break;
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: q.key = ImGuiKey_GamepadL2; q.value = v; push(q); break;
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: q.key = ImGuiKey_GamepadR2; q.value = v; push(q); break;
        default: break;
        }
        return true;
    }
    case SDL_EVENT_MOUSE_MOTION:
        if (!capturing() && !vpad_on()) return false;
        { QEvent q = {}; q.kind = QEvent::MOUSE_POS; q.x = e->motion.x * g_pixel_density; q.y = e->motion.y * g_pixel_density; push(q); }
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
        if (!capturing() && !vpad_on()) return false;
        {
            QEvent q = {};
            q.kind = QEvent::MOUSE_BUTTON;
            q.button = e->button.button == SDL_BUTTON_LEFT ? 0 : e->button.button == SDL_BUTTON_RIGHT ? 1 : 2;
            q.down = e->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            push(q);
        }
        return true;
    case SDL_EVENT_MOUSE_WHEEL:
        if (!capturing() && !vpad_on()) return false;
        { QEvent q = {}; q.kind = QEvent::WHEEL; q.x = e->wheel.x; q.y = e->wheel.y; push(q); }
        return true;
    default:
        return false;
    }
}

extern "C" uint32_t g_window_resize_request;  // (test scripts: runtime/hle/input.c "resize")
void menu_apply_window_requests(void *win) {
    SDL_Window *w = (SDL_Window *)win;
    g_pixel_density = SDL_GetWindowPixelDensity(w);
    static bool text_input;
    bool want_text = capturing();
    if (want_text != text_input) {
        if (want_text) SDL_StartTextInput(w);
        else SDL_StopTextInput(w);
        text_input = want_text;
    }
    if (g_want_fullscreen_apply.exchange(false)) {
#ifdef __APPLE__
        SDL_SetWindowFullscreen(w, g_settings.fullscreen);
#else
        ui_request_window_apply();
#endif
    }
#ifndef __APPLE__
    ui_window_apply_pending();
#endif
    if (uint32_t r = __atomic_exchange_n(&g_window_resize_request, 0u, __ATOMIC_RELAXED))
        SDL_SetWindowSize(w, (int)(r >> 16), (int)(r & 0xffff));
}

// ------------------------------------------------------------------ render thread
static bool g_inited;
static ImGuiContext *g_ctx;  // ours: the start screen (back to it after a reboot) has its own context
static ImFont *g_font, *g_font_big;
static ImFont *g_menu_font, *g_menu_font_big;
static float g_built_font_scale = -1.0f;
static int g_built_ui_lang = -1;
static int g_tab, g_tab_request = -2;  // (-2: no request; -1 is the Prelaunch tab)
static int g_focus_tab = -99;  // put the nav cursor on this tab next frame (-100: the selected one; -99: none)
static int g_focus_frames;

// The start screen's Settings opening: the controller's cursor starts on the first tab the first time in a session,
// later on the tab that's selected
static bool g_page_fresh_seen;  // (focus_check: the menu opened)
static bool g_page_fresh;  // the page shown hasn't been entered yet: the first Down goes to its first enabled row
static bool g_page_focus_next;  // (that page has no option rows: its first control takes the cursor next frame)
static int g_shown_tab = -1000;  // the page drawn last (its id)
void menu_tabs_focus(bool first_tab) {
    g_focus_tab = first_tab ? -1 : -100, g_focus_frames = 0, g_page_fresh = g_page_fresh_seen = true;
}
int g_tab_request_ext = -1;

static void style_init(void) {
    ui_apply_sentinel_theme(1.0f);
}

static void ensure_fonts(float scale, bool allow_vk_rebuild) {
    float fsc = scale;
    if (fsc < 0.55f) fsc = 0.55f;
    if (fsc > 2.5f) fsc = 2.5f;
    int lang = ui_resolve_lang();
    if (g_built_font_scale > 0.0f && g_built_ui_lang == lang && fabsf(g_built_font_scale - fsc) <= 0.04f) return;
    if (g_inited && !allow_vk_rebuild) return;
    ui_load_i18n_fonts(&g_font, &g_menu_font, &g_font_big, 28.0f * fsc, 32.0f * fsc, 42.0f * fsc, lang, nullptr, nullptr);
    g_menu_font_big = g_font_big;
    g_built_font_scale = fsc;
    g_built_ui_lang = lang;
    if (g_inited) {
#ifndef __APPLE__
        vk_imgui_fonts_rebuild();
#endif
    }
}

#ifdef __APPLE__
static void init(id<MTLDevice> dev, float scale) {
#else
static void init(float scale) {
#endif
    IMGUI_CHECKVERSION();
    g_ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(g_ctx);
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.IniFilename = NULL;
    g_built_font_scale = -1.0f;
    ensure_fonts(scale, true);
    style_init();
#ifdef __APPLE__
    ImGui_ImplMetal_Init(dev);
#else
    vk_imgui_backend_init();
#endif
    g_inited = true;
}

static void drain_events(ImGuiIO &io) {
    pthread_mutex_lock(&q_lock);
    std::vector<QEvent> ev;
    ev.swap(q_events);
    pthread_mutex_unlock(&q_lock);
    for (const QEvent &q : ev) {
        switch (q.kind) {
        case QEvent::KEY: io.AddKeyEvent(q.key, q.down); break;
        case QEvent::ANALOG: io.AddKeyAnalogEvent(q.key, q.value > 0.1f, q.value); break;
        case QEvent::MOUSE_POS: io.AddMousePosEvent(q.x, q.y); break;
        case QEvent::MOUSE_BUTTON: io.AddMouseButtonEvent(q.button, q.down); break;
        case QEvent::WHEEL: io.AddMouseWheelEvent(q.x, q.y); break;
        case QEvent::TEXT: io.AddInputCharactersUTF8(q.text); break;
        }
    }
}

extern "C" void renderer_get_scale(float *sx, float *sy);
extern "C" int input_pad_family(void);  // runtime/hle/input.c
extern "C" double output_scale_dynamic_factor(void);

static void tab_display(void) {
    float scale = ImGui::GetFontSize() / 34;
    opt_begin("##video", scale);
    opt_section("Display");
    const char *aspects[] = {"Unlocked", "Original 16:9"};
    opt_choice("Aspect ratio", &g_settings.aspect, aspects, 2, ASPECT_UNLOCKED,
               "Unlocked: the view fills the window, so wider (or taller) windows show more of the island - up to "
               "ultrawide; houses and dungeons keep their framing. Original 16:9: the game's own framing, with bars.");
    bool fs = g_settings.fullscreen;
    if (opt_toggle("Fullscreen", &fs, false, "Fills the screen. Also: F11, Alt+Enter or Cmd+Ctrl+F.")) {
        g_settings.fullscreen = fs;
        g_settings.window_mode = fs ? WIN_BORDERLESS : WIN_WINDOWED;
        g_want_fullscreen_apply.store(true);
    }
    if (opt_toggle("Show FPS", &g_settings.show_fps, false, "Shows the frame rate in the top right corner."))
        g_settings.hud_mode = g_settings.show_fps ? HUD_FPS : HUD_OFF;

    opt_section("Rendering");
    // display order -> RES_* (the ini / enum order predates the menu order)
    // (up to 4x: 7680 x 4320, 8K - more only ran out of GPU memory, and sat next to 75% in the list)
    // (Match display in the middle: left from it 75% / 50% / 25%, right Dynamic then 1x-4x; user 2026-10-11)
    static const char *res[] = {"25%", "50%", "75%", "Match display", "Dynamic", "1x", "2x", "3x", "4x"};
    static const int res_mode[] = {RES_Q25, RES_Q50, RES_Q75, RES_MATCH, RES_DYNAMIC, 1, 2, 3, 4};
    enum { RES_MATCH_AT = 3 };
    enum { NRES = sizeof res_mode / sizeof res_mode[0] };
    {
        int ri = RES_MATCH_AT, rdef = RES_MATCH_AT;  // (rdef: the GPU's default - Dynamic on an integrated GPU)
        for (int i = 0; i < NRES; i++) {
            if (res_mode[i] == g_settings.res_mode) ri = i;
            if (res_mode[i] == g_default_res_mode) rdef = i;
        }
        float sx, sy;
        renderer_get_scale(&sx, &sy);
        char d[400];
        int n = snprintf(d, sizeof d, "How sharp the 3D image is. Match display: your screen's own resolution. Dynamic: "
                                      "as sharp as your GPU keeps at full speed. 1x-4x: the Switch's 1080p times that%s. "
                                      "75%% / 50%% / 25%%: a share of the display, for slower machines.\n\nRendering at "
                                      "%.0f x %.0f.", g_gpu_integrated ? " (1x: 720p on this GPU)" : "", 1920.0 * sx, 1080.0 * sy);
        if (g_settings.res_mode == RES_DYNAMIC)
            snprintf(d + n, sizeof d - n, " Dynamic: %.0f%% of the display right now.", output_scale_dynamic_factor() * 100);
        if (opt_choice("Internal resolution", &ri, res, NRES, rdef, d, false))
            g_settings.res_mode = res_mode[ri], g_settings.res_auto = false;
    }
    {   // each OS's own upscaler next to bilinear (UPSCALE_SHARP: MetalFX on macOS, AMD FSR 1 on Windows)
#ifdef __APPLE__
        static const char *ups[] = {"Bilinear", "MetalFX"};
        const char *ud = "How the image is scaled up to the window when it renders at a lower resolution. MetalFX: Apple's "
                         "upscaler, sharper. Bilinear: plain and soft.";
#else
        static const char *ups[] = {"Bilinear", "AMD FSR 1"};
        const char *ud = "How the image is scaled up to the window when it renders at a lower resolution. AMD FSR 1: "
                         "edge-aware upscaling and sharpening, sharper. Bilinear: plain and soft.";
#endif
        int ui = g_settings.upscaler == UPSCALE_BILINEAR ? 0 : 1;
        char udm[512];
        if (g_settings.res_mode == RES_MATCH) {  // (renders at the window's size: nothing to scale up)
            snprintf(udm, sizeof udm, "%s\n\nNot used at Match display: the game already renders at your display's resolution.", ud);
            ud = udm;
            opt_disable_next();
        }
        if (opt_choice("Upscaler", &ui, ups, 2, g_default_upscaler == UPSCALE_BILINEAR ? 0 : 1, ud))
            g_settings.upscaler = ui ? UPSCALE_SHARP : UPSCALE_BILINEAR, g_settings.upscaler_auto = false;
    }
    static const char *aa[] = {"Off", "SMAA"};
    opt_choice("Anti-aliasing", &g_settings.antialias, aa, 2, AA_OFF,
               "Smooths jagged edges of the game image before it is scaled to the window. SMAA: finds the edges and "
               "blends only along them, so textures stay sharp. Most useful at a lower internal resolution.");

    opt_section("Game");
    opt_toggle("Remove blur", &g_settings.blur_removal, false,
               "Turns off the tilt-shift blur at the top and bottom of the screen.");
    opt_toggle("Hide Hero Mode icon", &g_settings.hide_hero_icon, false,
               "Hides the Hero Mode emblem in the corner of the screen while playing a Hero Mode file.");
    opt_end();
}

extern "C" bool cosmetics_seed_on(void);  // runtime/cosmetics_seed.c

// Cosmetics: tunic / heart colours, live (runtime/nvn/texpatch.c), and recolours by draw (runtime/nvn/recolour*.c);
// a seed made with "Random cosmetics" fills the rows left on Default (shown as "Seed: <colour>")
static void tab_cosmetics(void) {
    opt_begin("##cosmetics", ImGui::GetFontSize() / 34);
    CosColour *rows[] = {&g_settings.tunic, &g_settings.hearts, &g_settings.hair, &g_settings.skin, &g_settings.shield,
                         &g_settings.leaves, &g_settings.grass, &g_settings.sand};
    if (opt_button("Randomize all", "Every color below gets a random color."))
        for (CosColour *c : rows) c->pick = 1 + rand() % (COL_RANDOM - 1);  // (a named colour: shown as what it is)
    if (opt_button("Default all", "Everything on this page back to the game's own look.")) {
        for (CosColour *c : rows) c->pick = COL_DEFAULT;
        g_settings.enemy_colours = 0;
        g_settings.screen_filter = 0;
    }
    opt_section("Colors");
    opt_colour("Tunic color", &g_settings.tunic, COL_SLOT_TUNIC,
               "Link's tunic color. Default: the game's own (or the randomizer seed's).");
    opt_colour("Heart color", &g_settings.hearts, COL_SLOT_HEARTS, "The color of the hearts on the HUD.");
    opt_colour("Hair color", &g_settings.hair, COL_SLOT_HAIR, "Link's hair color.");
    opt_colour("Body color", &g_settings.skin, COL_SLOT_SKIN, "Link's body color.");  // (larecomp.ini: skin)
    opt_colour("Shield color", &g_settings.shield, COL_SLOT_SHIELD, "The color of Link's shield.");
    opt_section("Overworld");
    {   // Season: fills Leaves, Grass and Sand at once (palette: Orange 2, Gold 3, Pink 9, White 10); "Mixed" when
        // they match none
        static const char *const names[] = {"Spring", "Autumn", "Winter", "Cherry blossom", "Mixed"};
        static const int preset[4][3] = {{COL_DEFAULT, COL_DEFAULT, COL_DEFAULT}, {2, 3, 3}, {10, 10, 10},
                                         {9, COL_DEFAULT, COL_DEFAULT}};
        int cur = 4;  // (as shown: a row on Default shows the seed's colour, if it has its own)
        for (int i = 0; i < 4; i++)
            if (col_pick(&g_settings.leaves, COL_SLOT_LEAVES) == preset[i][0] &&
                col_pick(&g_settings.grass, COL_SLOT_GRASS) == preset[i][1] &&
                col_pick(&g_settings.sand, COL_SLOT_SAND) == preset[i][2])
                cur = i;
        int v = cur;
        if (opt_choice("Season", &v, names, 5, 0, "Sets the leaves, grass and sand colors below.") && v != cur) {
            if (v == 4) v = cur == 0 ? 3 : 0;  // (Mixed is never chosen: past the ends, back round)
            g_settings.leaves.pick = preset[v][0];
            g_settings.grass.pick = preset[v][1];
            g_settings.sand.pick = preset[v][2];
        }
    }
    opt_colour("Leaves color", &g_settings.leaves, COL_SLOT_LEAVES, "The overworld's tree leaves and bushes.");
    opt_colour("Grass color", &g_settings.grass, COL_SLOT_GRASS, "The overworld's grass.");
    opt_colour("Sand color", &g_settings.sand, COL_SLOT_SAND, "The beaches, the desert and the sandy south.");
    opt_section("Enemies");
    {   // (Off in a seed with its own cosmetics: "Seed", its enemies are per seed)
        const char *modes[N_ENEMY_COLOUR_MODES];
        for (int i = 0; i < N_ENEMY_COLOUR_MODES; i++) modes[i] = g_enemy_colour_modes[i];
        if (cosmetics_seed_on()) modes[0] = "Seed";
        opt_choice("Enemy colors", &g_settings.enemy_colours, modes, N_ENEMY_COLOUR_MODES, 0,
                   "Each kind of enemy in another color. Enemies whose color shows their strength or type keep theirs.");
    }
    opt_section("Screen");
    opt_choice("Screen filter", &g_settings.screen_filter, g_screen_filters, N_SCREEN_FILTERS, 0,
               "A filter over the whole picture, the HUD included.");
    opt_end();
}

static void tab_audio(void) {
    opt_begin("##audio", ImGui::GetFontSize() / 34);
    opt_section("Sound");
    float pct = roundf(g_settings.volume * 100);
    if (opt_slider("Volume", &pct, 0, 200, 5, 100, "%g%%",
                   "The game's volume. 100% is the game's own level; above it, louder."))
        g_settings.volume = pct / 100.0f;
    opt_toggle("Room reverb", &g_settings.reverb, true,
               "The game's echo in caves, dungeons and big rooms.");
    opt_toggle("Movie audio", &g_settings.movie_audio, true,
               "Sound in the game's movies (the opening and the ending). Applies the next time a movie starts.");
    opt_end();
}

static void tab_display_v13(float scale) {
    opt_begin("##video_v13", scale);
    opt_section(ui_tr(STR_SEC_DISPLAY_WIN));
    const char *win_modes[3] = {ui_tr(STR_WIN_WINDOWED), ui_tr(STR_WIN_BORDERLESS), ui_tr(STR_WIN_EXCLUSIVE)};
    if (opt_choice(ui_tr(STR_OPT_WIN_MODE), &g_settings.window_mode, win_modes, 3, WIN_WINDOWED,
                   ui_tr(STR_OPT_WIN_MODE_DESC), false)) {
        g_settings.fullscreen = (g_settings.window_mode != WIN_WINDOWED);
        ui_request_window_apply();
        settings_save();
    }
    const char *out_res[5] = {ui_tr(STR_RES_AUTO), "720p", "1080p", "1440p", "4K"};
    if (opt_choice(ui_tr(STR_OPT_OUT_RES), &g_settings.output_res, out_res, 5, OUT_RES_AUTO,
                   ui_tr(STR_OPT_OUT_RES_DESC), false)) {
        ui_request_window_apply();
        settings_save();
    }
    const char *aspect_opts[2] = {ui_tr(STR_ASP_LOCK_16_9), ui_tr(STR_ASP_STRETCH)};
    if (opt_choice(ui_tr(STR_OPT_ASPECT), &g_settings.aspect_mode, aspect_opts, 2, ASPECT_MODE_LOCK_16_9,
                   ui_tr(STR_OPT_ASPECT_DESC), false)) {
        settings_save();
    }
    const char *vsync_opts[3] = {ui_tr(STR_VSYNC_MAILBOX), ui_tr(STR_VSYNC_FIFO), ui_tr(STR_VSYNC_IMMEDIATE)};
    if (opt_choice(ui_tr(STR_OPT_VSYNC), &g_settings.vsync_mode, vsync_opts, 3, VSYNC_MAILBOX,
                   ui_tr(STR_OPT_VSYNC_DESC), false)) {
        vk_request_swapchain_recreate();
        settings_save();
    }

    opt_section(ui_tr(STR_SEC_RENDER_SCALE));
    {
        const char *scale_labels[3] = {ui_tr(STR_SCALE_1X), ui_tr(STR_SCALE_1_5X), ui_tr(STR_SCALE_2X)};
        int si = g_settings.res_mode == 2 ? 2 : g_settings.res_mode == RES_1_5X ? 1 : 0;
        float sx = 1.0f, sy = 1.0f;
        renderer_get_scale(&sx, &sy);
        char desc[384];
        snprintf(desc, sizeof desc, ui_tr(STR_OPT_RENDER_SCALE_DESC_FMT), 1920.0 * sx, 1080.0 * sy);
        if (opt_choice(ui_tr(STR_OPT_RENDER_SCALE), &si, scale_labels, 3, 0, desc, false)) {
            g_settings.res_mode = (si == 2) ? 2 : (si == 1) ? RES_1_5X : 1;
            g_settings.res_auto = false;
            settings_save();
        }
    }
    {
#ifdef __APPLE__
        const char *up_opts[2] = {ui_tr(STR_UPSCALE_BILINEAR), "MetalFX"};
#else
        const char *up_opts[2] = {ui_tr(STR_UPSCALE_BILINEAR), "AMD FSR 1"};
#endif
        int ui = g_settings.upscaler == UPSCALE_BILINEAR ? 0 : 1;
        if (opt_choice(ui_tr(STR_OPT_UPSCALER), &ui, up_opts, 2, g_default_upscaler == UPSCALE_BILINEAR ? 0 : 1,
                       ui_tr(STR_OPT_UPSCALER_DESC))) {
            g_settings.upscaler = ui ? UPSCALE_SHARP : UPSCALE_BILINEAR;
            g_settings.upscaler_auto = false;
            settings_save();
        }
    }

    opt_section(ui_tr(STR_SEC_MONITOR_PACE));
    const char *hud_opts[3] = {ui_tr(STR_HUD_OFF), ui_tr(STR_HUD_FPS), ui_tr(STR_HUD_DETAILED)};
    if (opt_choice(ui_tr(STR_OPT_HUD), &g_settings.hud_mode, hud_opts, 3, HUD_OFF,
                   ui_tr(STR_OPT_HUD_DESC), false)) {
        g_settings.show_fps = (g_settings.hud_mode != HUD_OFF);
        settings_save();
    }
    {
        const char *spd_opts[3] = {ui_tr(STR_SPEED_1X), ui_tr(STR_SPEED_2X), ui_tr(STR_SPEED_3X)};
        uint64_t cur_spd = rt_speed_mult();
        int sp_idx = cur_spd >= 3 ? 2 : cur_spd == 2 ? 1 : 0;
        bool env_locked = rt_speed_locked_by_env();
        if (env_locked) opt_disable_next();
        if (opt_choice(ui_tr(STR_OPT_SPEED), &sp_idx, spd_opts, 3, 0,
                       env_locked ? ui_tr(STR_OPT_SPEED_DESC_LOCKED) : ui_tr(STR_OPT_SPEED_DESC), false)) {
            rt_set_speed_mult(sp_idx == 2 ? 3 : sp_idx == 1 ? 2 : 1);
        }
    }
    opt_end();
}

static void tab_audio_v13(float scale) {
    opt_begin("##audio_v13", scale);
    opt_section(ui_tr(STR_SEC_AUDIO_OUT));
    float pct = roundf(g_settings.volume * 100.0f);
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    if (opt_slider(ui_tr(STR_OPT_VOL), &pct, 0, 100, 5, 100, "%g%%", ui_tr(STR_OPT_VOL_DESC))) {
        g_settings.volume = pct / 100.0f;
        settings_save();
    }
    if (opt_toggle(ui_tr(STR_OPT_MUTE_UNFOCUSED), &g_settings.mute_unfocused, false, ui_tr(STR_OPT_MUTE_UNFOCUSED_DESC))) {
        settings_save();
    }
    const char *ch_opts[3] = {ui_tr(STR_CH_AUTO), ui_tr(STR_CH_STEREO), ui_tr(STR_CH_5_1)};
    if (opt_choice(ui_tr(STR_OPT_AUDIO_CH), &g_settings.audio_channels, ch_opts, 3, AUDIO_CH_AUTO,
                   ui_tr(STR_OPT_AUDIO_CH_DESC), false)) {
        settings_save();
    }
    opt_end();
}

static void tab_advanced_v13(float scale, bool in_game) {
    static bool show_vk_adv = false;
    static uint64_t cache_bytes = 0;
    static bool cache_measured = false;
    static char cache_msg[160];
    static char save_msg[256];
    static bool game_lang_saved_notice = false;
    static int ask = 0;  // 0: idle, 1: confirm return to launcher, 2: confirm quit
    if (!cache_measured) {
        cache_bytes = rt_tree_size("cache");
        cache_measured = true;
    }
    if (ask && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
        ask = 0;

    char cache_sz[48];
    if (cache_bytes >= (1024ull * 1024ull))
        snprintf(cache_sz, sizeof cache_sz, "%.1f MB", cache_bytes / (1024.0 * 1024.0));
    else
        snprintf(cache_sz, sizeof cache_sz, "%llu KB", (unsigned long long)(cache_bytes / 1024ull));

    bool sv_exists = false;
    uint64_t sv_size = 0;
    int64_t sv_mtime = 0;
    rt_save_info(&sv_exists, &sv_size, &sv_mtime);
    char sv_desc[320];
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
        snprintf(sv_desc, sizeof sv_desc, ui_tr(STR_SAVE_DESC_FOUND_FMT),
                 sv_size / (1024.0 * 1024.0), (unsigned long long)sv_size, tb);
    } else {
        snprintf(sv_desc, sizeof sv_desc, "%s", ui_tr(STR_SAVE_DESC_NONE));
    }

    opt_begin("##advanced_v13", scale);

    opt_section(ui_tr(STR_SEC_LANG_UI));
    {
        int ui_n = 0;
        const char *const *ui_lbls = ui_lang_selector_labels(&ui_n);
        if (opt_choice_id("opt_ui_lang", ui_tr(STR_UI_LANG_LABEL), &g_settings.ui_lang, ui_lbls, ui_n, UI_LANG_AUTO,
                          ui_tr(STR_UI_LANG_DESC), true)) {
            cache_msg[0] = 0;
            save_msg[0] = 0;
            settings_save();
        }
        int gl_n = 0;
        const char *const *gl_lbls = ui_game_lang_labels(&gl_n);
        std::string gdesc = ui_tr(in_game ? STR_GAME_LANG_DESC_INGAME : STR_GAME_LANG_DESC_LAUNCHER);
        if (in_game && game_lang_saved_notice) gdesc += std::string("\n\n") + ui_tr(STR_GAME_LANG_SAVED_HINT);
        if (in_game && game_lang_saved_notice) opt_mark_next();
        if (opt_choice(ui_tr(STR_GAME_LANG_LABEL), &g_settings.game_lang, gl_lbls, gl_n, GAME_LANG_ZH_HANS,
                       gdesc.c_str(), true)) {
            if (in_game) game_lang_saved_notice = true;
            settings_save();
        }
    }

    opt_section(ui_tr(STR_SEC_VK_SYNC));
    opt_toggle(ui_tr(STR_OPT_VK_EXPAND), &show_vk_adv, false, ui_tr(STR_OPT_VK_EXPAND_DESC));
    if (show_vk_adv) {
        opt_indent(1);
        if (opt_toggle(ui_tr(STR_OPT_VK_WAIT_PIPES), &g_settings.vk_wait_pipes, true, ui_tr(STR_OPT_VK_WAIT_PIPES_DESC)))
            settings_save();
        if (opt_toggle(ui_tr(STR_OPT_VK_COPY_VERTS), &g_settings.vk_copy_vertices, true, ui_tr(STR_OPT_VK_COPY_VERTS_DESC)))
            settings_save();
        if (opt_toggle(ui_tr(STR_OPT_VK_OVERLAP), &g_settings.vk_gpu_overlap, true, ui_tr(STR_OPT_VK_OVERLAP_DESC)))
            settings_save();
        opt_indent(-1);
    }

    opt_section(ui_tr(STR_SEC_SHADER_CACHE));
    {
        std::string desc = ui_tr(STR_OPT_CLEAR_CACHE_DESC);
        if (cache_msg[0]) desc += std::string("\n\n") + cache_msg;
        if (opt_action(ui_tr(STR_OPT_CLEAR_CACHE), cache_sz, desc.c_str(), false, cache_msg[0] != 0)) {
            int rm = vk_clear_shader_cache();
            cache_bytes = rt_tree_size("cache");
            snprintf(cache_msg, sizeof cache_msg, ui_tr(STR_CACHE_CLEARED_FMT), rm);
        }
    }

    opt_section(ui_tr(STR_CARD_SAVE_TITLE));
    if (opt_button(ui_tr(STR_BTN_OPEN_SAVE), sv_desc)) {
        const char *dir = plat_is_dir("save/save_0") ? "save/save_0" : "save";
        plat_mkdir_p("save");
        char full[1200];
        snprintf(full, sizeof full, "%s/%s", rt_data_root(), dir);
        plat_open_folder(full);
    }
    {
        std::string bdesc = std::string(sv_desc) + "\n\n" + ui_tr(STR_SAVE_BACKUP_DESC_SUFFIX);
        if (save_msg[0]) bdesc += std::string("\n\n") + save_msg;
        if (opt_action(ui_tr(STR_BTN_BACKUP_SAVE),
                       ui_tr(sv_exists ? STR_SAVE_READY : STR_SAVE_EMPTY_BADGE), bdesc.c_str(), false, save_msg[0] != 0)) {
            char rel[256], err[256];
            if (rt_save_backup(rel, sizeof rel, err, sizeof err))
                snprintf(save_msg, sizeof save_msg, "%s%s", ui_tr(STR_BACKUP_OK_PFX), rel);
            else
                snprintf(save_msg, sizeof save_msg, "%s%s", ui_tr(STR_BACKUP_FAIL_PFX), err);
        }
    }

    opt_section(ui_tr(STR_SEC_SYS_EXIT));
    if (!ask) {
        if (!in_game) opt_disable_next();
        if (opt_action(ui_tr(STR_OPT_RET_LAUNCHER), "",
                       ui_tr(in_game ? STR_OPT_RET_LAUNCHER_INGAME : STR_OPT_RET_LAUNCHER_HERE), false)) {
            ask = 1;
        }
        if (opt_action(ui_tr(STR_OPT_QUIT_GAME), "",
                       ui_tr(in_game ? STR_OPT_QUIT_INGAME : STR_OPT_QUIT_LAUNCHER), false)) {
            if (in_game) ask = 2;
            else {
                settings_save();
                SDL_Event q = {};
                q.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&q);
            }
        }
    } else {
        if (opt_action(ui_tr(ask == 1 ? STR_OPT_CONFIRM_RET : STR_OPT_CONFIRM_QUIT), "",
                       ui_tr(STR_OPT_CONFIRM_WARN), false, true)) {
            settings_save();
            if (ask == 1) {
                rt_request_reboot(2);
                set_open(false);
            } else {
                SDL_Event q = {};
                q.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&q);
            }
            ask = 0;
        }
        if (opt_action(ui_tr(STR_OPT_CANCEL), "", ui_tr(STR_OPT_CANCEL_DESC), false)) ask = 0;
    }
    opt_end();
}

// Quit: back to the start screen (in game: the game reboots to it) or out to the desktop. Each asks first (progress
// since the last save is lost); B / Esc or Cancel backs out.
extern "C" void rt_request_reboot(int kind);  // runtime/main.c (RT_REBOOT_TITLE = 2: the start screen)
static void tab_quit(float scale, bool in_game) {
    static int ask;  // 0: the two choices; 1: start screen?; 2: desktop?
    if (ask && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
        ask = 0;
    const char *lost = in_game ? "\n\nProgress since your last save is lost." : "";
    char d1[200], d2[200];
    snprintf(d1, sizeof d1, "Leaves the game and goes back to the 13SentinelsRecomp start screen.%s", lost);
    snprintf(d2, sizeof d2, "Closes 13SentinelsRecomp.%s", lost);
    opt_begin("##quit", scale);
    opt_section("Quit");
    if (!ask) {
        if (!in_game) opt_disable_next();  // (already on it)
        if (opt_action("Quit to Start Screen", "", in_game ? d1 : "You're on the start screen.")) ask = 1;
        if (opt_action("Quit to Desktop", "", d2)) ask = 2;
    } else {
        if (opt_action(ask == 1 ? "Yes, quit to Start Screen" : "Yes, quit to Desktop", "", ask == 1 ? d1 : d2)) {
            settings_save();
            if (ask == 1) {
                rt_request_reboot(2);
                set_open(false);
            } else {
                SDL_Event q = {};
                q.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&q);
            }
            ask = 0;
        }
        if (opt_action("Cancel", "", "Stays where you are.")) ask = 0;
    }
    opt_end();
}

extern "C" bool inventory_overlay_wanted(void);
extern "C" bool tracker_overlay_wanted(void);
extern "C" bool tracker_dungeon_wanted(void);
extern "C" void tracker_dungeon_draw(ImVec2 display, ImFont *font);
extern "C" void tracker_overlay_draw(ImVec2 display, ImFont *font);
extern "C" bool chaos_overlay_wanted(void);
extern "C" void chaos_overlay_draw(ImVec2 display, ImFont *font);
extern "C" void inventory_overlay_draw(ImVec2 display, ImFont *font);
extern "C" bool race_overlay_wanted(void);
extern "C" void race_overlay_draw(ImVec2 display, ImFont *font, float scale);
extern "C" bool pickup_flyin_wanted(void);  // (silent pickups: pickup_flyin.mm)
extern "C" void pickup_flyin_draw(ImVec2 display, ImFont *font, float scale);
extern "C" bool race_overlay_key(char *out, size_t cap);

// ---- reusing the overlay's last drawing (LARECOMP_OVERLAY_CACHE=0: off, a new ImGui frame every present as before)
// While only the FPS counter and / or the run timer are up (no menu, no input, nothing interactive or animated), the
// overlay draws the same thing frame after frame: ImGui's NewFrame + layout + Render then run for nothing on the
// render thread. Their drawing is described by a key (the text shown, the window size); while it stays the same, the
// last frame's draw data (ImGui keeps it until the next NewFrame) is drawn again. Only after three frames in a row
// built with that key: an auto-sized window (the FPS counter) settles over the frames after its text changes.
// The FPS counter then shows the same average as ImGui's io.Framerate (the last 60 frame times), sampled twice a
// second instead of every frame.
static int g_ov_cache = -1;
static char g_ov_key[96];
static int g_ov_same;  // frames built in a row with g_ov_key
static float g_fps_dt[60];
static int g_fps_n, g_fps_i;
static double g_fps_sum, g_fps_shown, g_fps_at;
static void fps_note(double now, float dt) {
    if (g_fps_n == 60) g_fps_sum -= g_fps_dt[g_fps_i];
    else g_fps_n++;
    g_fps_dt[g_fps_i] = dt;
    g_fps_sum += dt;
    g_fps_i = (g_fps_i + 1) % 60;
    if (now - g_fps_at >= 0.5 || now < g_fps_at) {
        g_fps_at = now;
        g_fps_shown = g_fps_sum > 0 ? g_fps_n / g_fps_sum : 0;
    }
}

// Reboot (runtime/main.c): the game is gone - menu and name dialog close; the ImGui context stays.
RT_ON_RESET(menu_reset) {
    set_open(false);
    g_kbd = 0;
    osk_cancel();
    pthread_mutex_lock(&q_lock);
    q_events.clear();
    pthread_mutex_unlock(&q_lock);
}

static void keyboard_draw(ImGuiIO &io, float scale) {
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 120));
    float w = fminf(io.DisplaySize.x * 0.9f, 1100 * scale);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, 0), ImGuiCond_Always);
    ImGui::Begin("##kbd", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PushFont(g_font_big);
    ImGui::TextColored(ImVec4(0.93f, 0.76f, 0.33f, 1), "Enter your name");
    ImGui::PopFont();
    static bool was_open, was_pad;
    bool pad = g_last_pad.load(), appearing = !was_open || pad != was_pad;  // (switching device: focus the new one)
    was_open = true, was_pad = pad;
    if (!pad) {  // keyboard: type straight into the field
        if (appearing) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1);
        ImGui::PushFont(g_font_big);
        ImGui::InputText("##name", g_kbd_text, (size_t)(g_kbd_max * 4 + 1 < (int)sizeof g_kbd_text ? g_kbd_max * 4 + 1 : sizeof g_kbd_text),
                         ImGuiInputTextFlags_CallbackCharFilter,
                         [](ImGuiInputTextCallbackData *d) -> int { return d->EventChar < 0x20 ? 1 : 0; });
        ImGui::PopFont();
        int units = kbd_cut(g_kbd_text, g_kbd_max);
        ImGui::TextDisabled("Enter  confirm      Esc  cancel      (%d / %d)", units, g_kbd_max);
    } else {  // controller: on-screen keyboard (osk.mm, shared with our menus' text fields)
        int r = osk_keys(g_kbd_text, sizeof g_kbd_text, g_kbd_max, appearing, scale, g_font_big);
        if (r) g_kbd = r > 0 ? 2 : 3;  // (Start / + confirms in menu_handle_event too)
    }
    ImGui::End();
    if (g_kbd.load() != 1) was_open = false;
}

// Game thread (runtime/hle/swkbd.c): show the dialog, re-present the last frame until it's answered.
// Returns 1 confirmed (out = text, UTF-8), 0 cancelled.
extern "C" int menu_keyboard_run(const char *initial, int max_len, char *out, int cap) {
    extern void backend_represent(void);
    snprintf(g_kbd_text, sizeof g_kbd_text, "%s", initial ? initial : "");
    g_kbd_max = max_len > 0 && max_len < (int)sizeof g_kbd_text / 4 ? max_len : 8;
    input_reset();
    g_kbd = 1;
    // (test) LARECOMP_KBD_AUTO=<text>: answer at once
    if (getenv("LARECOMP_KBD_AUTO")) { snprintf(g_kbd_text, sizeof g_kbd_text, "%s", getenv("LARECOMP_KBD_AUTO")); g_kbd = 2; }
    while (g_kbd.load() == 1 && !g_rt_unwind) {  // ~60 Hz (a reboot cancels)
        uint64_t t0 = SDL_GetTicks();
        backend_represent();
        uint64_t dt = SDL_GetTicks() - t0;
        if (dt < 16) SDL_Delay((uint32_t)(16 - dt));
    }
    kbd_cut(g_kbd_text, g_kbd_max);  // (whatever entered it: typed, on-screen keys, the test hook)
    bool ok = g_kbd.load() == 2 && g_kbd_text[0];
    if (ok) snprintf(out, cap, "%s", g_kbd_text);
    g_kbd = 0;
    input_reset();
    return ok;
}

// LARECOMP_MENU_FOCUS_LOG=1 (test; tools/menu_focus_sweep.sh): every Down / Up on a page of option rows is checked a
// few frames later against the rows' order and logged "[menu] focus check ... OK / FAIL": the first Down after a tab
// switch (however it came) or the menu opening, and Down from the tab bar, land on the page's first enabled row; Down
// goes to the next row (the last one stays), Up to the row before, and Up from the top row onto the selected tab.
static void focus_check(bool down, bool up, bool any_down, int focused_tab, int tab, const char *tab_name) {
    static int on = -1;
    if (on < 0) on = getenv("LARECOMP_MENU_FOCUS_LOG") != NULL;
    if (!on) return;
    static int shown = -1000, due_in, want, from, prev_focus = -1;  // (want: a row index, -1: the selected tab)
    static bool first_down_due;
    static const char *what;
    if (tab != shown || g_page_fresh_seen) shown = tab, first_down_due = true, g_page_fresh_seen = false, prev_focus = -1;
    int n = opt_row_count(), first = opt_first_row(), focus = opt_focused_row();
    if (any_down && !down) first_down_due = false;  // (a held Down's repeat: the first Down was checked or skipped)
    if ((down || up) && !due_in && n > 0) {
        from = prev_focus, due_in = 4;
        if (down && (first_down_due || from < 0)) want = first, what = first_down_due ? "first Down" : "Down from the tab bar";
        else if (down) want = from + 1 < n ? from + 1 : from, what = "Down";
        else if (from > 0) want = from - 1, what = "Up";
        else if (from == 0) want = -1, what = "Up from the top row";
        else due_in = 0;  // (Up on the tab bar: not a page move)
        if (down) first_down_due = false;
    } else if (down && n == 0 && first_down_due) {
        first_down_due = false;
        LOG("[menu] focus check: first Down on %s SKIP (no option rows)\n", tab_name);
    }
    prev_focus = focus;
    if (!due_in || --due_in) return;
    bool ok = want >= 0 ? focus == want : focused_tab == tab;
    char got[160];
    if (focus >= 0) snprintf(got, sizeof got, "\"%s\"", opt_row_label(focus));
    else snprintf(got, sizeof got, "tab %d", focused_tab);
    char exp[160];
    if (want >= 0) snprintf(exp, sizeof exp, "\"%s\"", opt_row_label(want));
    else snprintf(exp, sizeof exp, "tab %d", tab);
    LOG("[menu] focus check: %s on %s from %s -> %s (want %s) %s\n", what, tab_name,
        from >= 0 ? opt_row_label(from) : "the tab bar", got, exp, ok ? "OK" : "FAIL");
}

// The tab bar and its pages, into the current window: the in-game menu, and the start screen's Settings (in_game
// false: nothing of the game is loaded yet - the tabs that need it are greyed out; prelaunch: the start screen's own
// first tab, game files and fonts, drawn by launcher.mm).
static void tabs_body(float scale, bool in_game, void (*prelaunch)(float)) {
    // (id: the page; -1 = Prelaunch)
    struct Tab { const char *name; int id; bool on; };
    Tab tabs[11];
    int ntabs = 0;
    const bool v13 = is_v13_ui();
    if (v13) {
        tabs[ntabs++] = {ui_tr(STR_TAB_DISPLAY), 0, true};
        tabs[ntabs++] = {ui_tr(STR_TAB_AUDIO), 1, true};
        tabs[ntabs++] = {ui_tr(STR_TAB_CONTROLS), 8, true};
        tabs[ntabs++] = {ui_tr(STR_TAB_SYSTEM), 9, true};
    } else {
        if (prelaunch) tabs[ntabs++] = {"Prelaunch", -1, true};
        static const char *names[] = {"Video", "Audio", "Save Editor", "Randomizer", "Warp", "Cheats", "Gameplay",
                                      "Cosmetics", "Controls", "Quit"};
        static const int order[] = {8, 0, 1, 6, 7, 3, 5, 4, 2, 9};
        for (int i : order)
            if (i != 9 || !prelaunch) tabs[ntabs++] = {names[i], i, in_game || (i != 2 && i != 4 && i != 5)};
    }
    int cur = 0;  // g_tab / g_tab_request hold ids
    for (int i = 0; i < ntabs; i++) if (tabs[i].id == g_tab) cur = i;
    if (!tabs[cur].on) cur = 0;
    // L/R (or Q/E) switch tabs, skipping greyed-out ones (not while typing or capturing a binding)
    if (g_tab_request_ext >= 0) { g_tab_request = g_tab_request_ext; g_tab_request_ext = -1; }
    bool any_popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId), typing = ui_text_busy();
    bool capturing_bind = bind_capture_active(NULL, NULL, NULL);
    static int focused_tab = -99;  // the tab the controller's cursor is on (from the frame before; -99: none)
    bool switching = false;  // (L / R this frame: the page changes next frame)
    if (!any_popup && !typing && !capturing_bind) {
        int step = ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) || ImGui::IsKeyPressed(ImGuiKey_E, false) ? 1
                 : ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) || ImGui::IsKeyPressed(ImGuiKey_Q, false) ? -1 : 0;
        if (step) {
            int i = cur;
            do i = (i + step + ntabs) % ntabs; while (!tabs[i].on);
            g_tab_request = tabs[i].id;
            // the cursor goes onto the new tab (left behind on the tab bar, A on it switched straight back - user
            // 2026-10-11; left in the old page, it stayed on a row that is gone, and Down went on from where that row
            // had been)
            g_focus_tab = tabs[i].id, g_focus_frames = 0;
            switching = true;
        }
    }
    bool down = !any_popup && !typing && (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown) ||
                                          ImGui::IsKeyPressed(ImGuiKey_GamepadLStickDown) ||
                                          ImGui::IsKeyPressed(ImGuiKey_DownArrow));
    bool up = !any_popup && !typing && (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp) ||
                                        ImGui::IsKeyPressed(ImGuiKey_GamepadLStickUp) || ImGui::IsKeyPressed(ImGuiKey_UpArrow));
    // The first Down on a page just switched to (L / R, a click or A on a tab, the menu opening), or Down with the
    // cursor on the tab bar: the page's first enabled row, top to bottom - never ImGui's pick from wherever its cursor
    // was (a row of the page before, the tab bar's x). Option rows take it (opt_focus_first); a page without them gets
    // its first control (g_page_focus_next).
    if (ImGui::GetIO().MouseClicked[0]) g_page_fresh = false;  // (the mouse went into the page)
    bool down_from_tab = down && !switching &&
                         ((focused_tab != -99 && ImGui::GetCurrentContext()->NavCursorVisible) || g_page_fresh);
    if (down_from_tab) {
        ImGui::NavMoveRequestCancel();  // (ImGui's own move this frame)
        g_page_fresh = false;
        g_focus_tab = -99, g_focus_frames = 0;  // (the cursor stops going back to the tab)
    }
    opt_focus_first(down_from_tab);
    // Up / Down on a page of option rows: the next / previous row in order (opt_nav_step), not ImGui's spatial pick
    if ((down || up) && !down_from_tab && !switching && opt_nav_step(down ? 1 : -1)) ImGui::NavMoveRequestCancel();
    opt_frame_reset();
    // The cursor moved out of the page (ImGui's pick, applied this frame for last frame's press - Up from the page's
    // top control): onto the selected tab. ImGui's pick was the tab nearest the control's x (Controls, from most
    // rows), or the close button
    ImGuiContext &g = *ImGui::GetCurrentContext();
    ImGuiWindow *root = ImGui::GetCurrentWindow()->RootWindow;
    static bool nav_in_page;  // (the frame before: the cursor on a control of the page - its child windows)
    if (nav_in_page && g.NavJustMovedToId && g.NavWindow == root && !any_popup) g_focus_tab = -100, g_focus_frames = 0;
    if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_NoTabListScrollingButtons)) {
        for (int k = 0; k < ntabs; k++) {
            const Tab &t = tabs[k];
            ImGuiTabItemFlags f = g_tab_request == t.id && t.on ? ImGuiTabItemFlags_SetSelected : 0;
            char lbl[96];
            snprintf(lbl, sizeof lbl, prelaunch ? " %s ###tab_%d" : "  %s  ###tab_%d", t.name, t.id);  // (invariant id across live UI language switches)
            ImGui::BeginDisabled(!t.on);  // (out of the game: Save Editor, Warp, Cheats)
            // focus (controller / keyboard): the tab lights up a little with a gold bar along its bottom, instead of
            // ImGui's outline (it sits 4 px outside the tab and the tab bar's clipping could cut its top off on macOS)
            // (focused_tab: from the frame before - colours go in before the tab is drawn)
            bool lit = focused_tab == t.id && ImGui::GetCurrentContext()->NavCursorVisible;
            ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
            if (lit) {
                ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.16f, 0.36f, 0.48f, 1));
                ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.16f, 0.36f, 0.48f, 1));
            }
            if (g_focus_tab == -1 && !prelaunch) g_focus_tab = (g_tab_request >= 0) ? g_tab_request : (v13 ? tabs[0].id : 8);
            bool focus_here = g_focus_tab == t.id || (g_focus_tab == -100 && t.id == g_tab);
            bool open = ImGui::BeginTabItem(lbl, NULL, f);
            ImGui::PopStyleColor(lit ? 3 : 1);
            if (focus_here) {  // the controller's cursor starts on this tab (SetKeyboardFocusHere skips tabs: no tab stop)
                ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
                ImGui::SetNavCursorVisible(true);
                // a few frames: the page's child window appearing makes ImGui pick its first widget once it's laid out
                if (++g_focus_frames >= 4) g_focus_tab = -99, g_focus_frames = 0;
                else g_focus_tab = t.id;
            }
            if (lit) {
                ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                float h = 4 * scale > 2 ? 4 * scale : 2;
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(a.x + 2, b.y - h), ImVec2(b.x - 2, b.y),
                                                          IM_COL32(72, 224, 246, 255));
            }
            if (ImGui::IsItemFocused()) focused_tab = t.id;
            else if (focused_tab == t.id) focused_tab = -99;
            ImGui::EndDisabled();
            if (open) {
                if (t.id != g_shown_tab) g_shown_tab = t.id, g_page_fresh = true, g_page_focus_next = false;
                g_tab = t.id;
                ImGui::Spacing();
                ImGui::BeginChild("##page", ImVec2(0, 0), ImGuiChildFlags_None,  // (the rest of the window: no hint line)
                                  ImGuiWindowFlags_NavFlattened);
                if (g_page_focus_next) {  // (a page without option rows: its first control)
                    g_page_focus_next = false;
                    ImGui::SetKeyboardFocusHere();
                    ImGui::SetNavCursorVisible(true);
                }
                switch (t.id) {
                case -1: if (prelaunch) prelaunch(scale); break;
                case 0: v13 ? tab_display_v13(scale) : tab_display(); break;
                case 1: v13 ? tab_audio_v13(scale) : tab_audio(); break;
                case 2:
                    if (game_in_play()) save_editor_draw(scale);
                    else ImGui::TextDisabled("Load a save file and get into the game to edit it.");
                    break;
                case 3: rando_tab_draw(scale); break;
                case 4: if (warp_tab_draw(scale)) set_open(false); break;
                case 5: cheats_draw(scale); break;
                case 6: difficulty_draw(scale); break;
                case 7: tab_cosmetics(); break;
                case 8: controls_tab_draw(scale); break;
                case 9: v13 ? tab_advanced_v13(scale, in_game) : tab_quit(scale, in_game); break;
                }
                static const char *scroll_test = getenv("LARECOMP_MENU_SCROLL");  // test hook: page scroll, 0..1
                if (scroll_test) ImGui::SetScrollY(ImGui::GetScrollMaxY() * (float)atof(scroll_test));
                // no enabled option row took the cursor: the page's first control, next frame (SetKeyboardFocusHere:
                // the first item that takes the cursor, in order)
                if (opt_focus_first_left() && down_from_tab) g_page_focus_next = true;
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        g_tab_request = -2;
        opt_focus_first_left();  // (no page drawn: nothing took it)
        ImGui::EndTabBar();
    }
    nav_in_page = g.NavWindow && g.NavWindow != g.NavWindow->RootWindow && g.NavWindow->RootWindow == root;
    // (checks: fresh presses only - a held key's repeats come faster than a check's few frames)
    bool down1 = down && (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown, false) ||
                          ImGui::IsKeyPressed(ImGuiKey_GamepadLStickDown, false) || ImGui::IsKeyPressed(ImGuiKey_DownArrow, false));
    bool up1 = up && (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp, false) ||
                      ImGui::IsKeyPressed(ImGuiKey_GamepadLStickUp, false) || ImGui::IsKeyPressed(ImGuiKey_UpArrow, false));
    for (int k = 0; k < ntabs; k++)
        if (tabs[k].id == g_tab) focus_check(down1, up1, down, focused_tab, g_tab, tabs[k].name);
}

void menu_tabs_draw(float scale, ImFont *font, ImFont *font_big, bool in_game, void (*prelaunch)(float)) {
    ImFont *f0 = g_font, *f1 = g_font_big;
    g_font = font, g_font_big = font_big;
    ImGuiStyle saved = ImGui::GetStyle();  // the menu's look (the start screen has its own style)
    style_init();
    ImGui::GetStyle().ScaleAllSizes(scale);
    tabs_body(scale, in_game, prelaunch);
    ImGui::GetStyle() = saved;
    g_font = f0, g_font_big = f1;
}

// ---- on-screen pad (mouse as the controller) ---------------------------------------------------------------
// A recompiled game needs no gamepad to be played: LARECOMP_VPAD=1 (default) draws the Switch's buttons and left stick
// over the picture, and the mouse presses them / drags the stick. The state goes to the game through
// runtime/hle/input.c (rt_vpad_set), merged into what the game polls like a real pad's. LARECOMP_VPAD=0 hides it.
extern "C" void rt_vpad_set(uint32_t buttons, int32_t lx, int32_t ly, int32_t rx, int32_t ry);
enum { VP_A = 1u << 0, VP_B = 1u << 1, VP_X = 1u << 2, VP_Y = 1u << 3, VP_L = 1u << 6, VP_R = 1u << 7,
       VP_ZL = 1u << 8, VP_ZR = 1u << 9, VP_PLUS = 1u << 10, VP_MINUS = 1u << 11, VP_LEFT = 1u << 12,
       VP_UP = 1u << 13, VP_RIGHT = 1u << 14, VP_DOWN = 1u << 15 };
static int g_vpad = -1;
static uint32_t g_vpad_held;      // what the mouse holds down this frame
static int32_t g_vpad_lx, g_vpad_ly, g_vpad_rx, g_vpad_ry;

static bool vpad_on(void) {
    if (g_vpad < 0) { const char *e = getenv("LARECOMP_VPAD"); g_vpad = e && atoi(e) != 0; }
    return g_vpad != 0;
}

// One round button: the mouse over it presses it (held while the button is down). Returns whether it is down.
static void vpad_button(ImDrawList *dl, float x, float y, float r, const char *label, uint32_t bit) {
    ImGui::SetCursorScreenPos(ImVec2(x - r, y - r));
    char id[32];
    snprintf(id, sizeof id, "##vp%s", label);
    ImGui::InvisibleButton(id, ImVec2(2 * r, 2 * r));
    // IsItemClicked: a click whose press and release land in one frame (a quick tap with the mouse) never shows as
    // IsItemActive; the game still gets the one-frame press (runtime/hle/input.c keeps short presses for its poll).
    bool down = ImGui::IsItemActive() || ImGui::IsItemClicked(), hot = ImGui::IsItemHovered();
    ImU32 fill = down ? IM_COL32(250, 205, 70, 200) : hot ? IM_COL32(120, 125, 160, 150) : IM_COL32(25, 25, 35, 120);
    dl->AddCircleFilled(ImVec2(x, y), r, fill, 24);
    dl->AddCircle(ImVec2(x, y), r, IM_COL32(235, 235, 235, down ? 230 : 150), 24, 2.0f);
    ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(x - ts.x * 0.5f + 1, y - ts.y * 0.5f + 1), IM_COL32(0, 0, 0, 180), label);
    dl->AddText(ImVec2(x - ts.x * 0.5f, y - ts.y * 0.5f), IM_COL32(255, 255, 255, 240), label);
    if (down) g_vpad_held |= bit;
}

// One stick: the knob follows the mouse while the disc is held, and the offset becomes the axis value.
static void vpad_stick(ImDrawList *dl, const char *id, float x, float y, float R, float knob, int32_t *ox, int32_t *oy) {
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetCursorScreenPos(ImVec2(x - R, y - R));
    ImGui::InvisibleButton(id, ImVec2(2 * R, 2 * R));
    bool grabbing = ImGui::IsItemActive(), hot = ImGui::IsItemHovered();
    float kx = x, ky = y;
    if (grabbing) {
        float mx = io.MousePos.x - x, my = io.MousePos.y - y;
        float len = sqrtf(mx * mx + my * my);
        if (len > R) { mx *= R / len, my *= R / len; }  // (the knob stays on the rim)
        kx += mx, ky += my;
        *ox = (int32_t)(mx / R * 32767.0f);
        *oy = (int32_t)(-my / R * 32767.0f);  // (screen y is down; the pad's y is up)
    }
    dl->AddCircleFilled(ImVec2(x, y), R, IM_COL32(20, 20, 28, grabbing ? 150 : 100), 32);
    dl->AddCircle(ImVec2(x, y), R, IM_COL32(235, 235, 235, hot || grabbing ? 210 : 130), 32, 2.0f);
    dl->AddCircleFilled(ImVec2(kx, ky), knob, grabbing ? IM_COL32(250, 205, 70, 215) : IM_COL32(150, 150, 170, 180), 24);
}

static void vpad_draw(float scale) {
    if (!vpad_on()) {
        if (g_vpad_held || g_vpad_lx || g_vpad_ly || g_vpad_rx || g_vpad_ry) {
            g_vpad_held = 0, g_vpad_lx = g_vpad_ly = g_vpad_rx = g_vpad_ry = 0;
            rt_vpad_set(0, 0, 0, 0, 0);
        }
        return;
    }
    if (!SDL_CursorVisible()) SDL_ShowCursor();  // the mouse is the controller here: never hide the pointer
    ImGuiIO &io = ImGui::GetIO();
    const float w = io.DisplaySize.x, h = io.DisplaySize.y, r = 38 * scale;
    g_vpad_held = 0;
    g_vpad_lx = g_vpad_ly = g_vpad_rx = g_vpad_ry = 0;
    {   // LARECOMP_VPAD_TEST=<button bits>[:<lx>:<ly>]: hold that through the pad for its first seconds (a self test)
        static uint32_t tb = 0xffffffffu;
        static int32_t tlx, tly;
        static int tn = -1;
        if (tn < 0) {
            tn = 0;
            const char *e = getenv("LARECOMP_VPAD_TEST");
            if (e) sscanf(e, "%x:%d:%d", &tb, &tlx, &tly);
            else tb = 0;
        }
        if (tn++ < 600 && tb) { g_vpad_held |= tb; g_vpad_lx = tlx, g_vpad_ly = tly; g_vpad_rx = tlx, g_vpad_ry = tly; }
    }
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##vpad", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
                                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    // right cluster: the Switch's diamond (A right, B bottom, X top, Y left)
    const float cx = w - 175 * scale, cy = h - 190 * scale, d = 100 * scale;
    vpad_button(dl, cx + d * 0.52f, cy, r, "A", VP_A);
    vpad_button(dl, cx, cy + d * 0.52f, r, "B", VP_B);
    vpad_button(dl, cx, cy - d * 0.52f, r, "X", VP_X);
    vpad_button(dl, cx - d * 0.52f, cy, r, "Y", VP_Y);
    // d-pad (the menus want it as much as the stick)
    const float dx0 = 430 * scale, dy0 = h - 190 * scale, dd = 74 * scale;
    vpad_button(dl, dx0, dy0 - dd, r * 0.8f, "Up", VP_UP);
    vpad_button(dl, dx0, dy0 + dd, r * 0.8f, "Dn", VP_DOWN);
    vpad_button(dl, dx0 - dd, dy0, r * 0.8f, "Lt", VP_LEFT);
    vpad_button(dl, dx0 + dd, dy0, r * 0.8f, "Rt", VP_RIGHT);
    // shoulders and +/- along the top
    vpad_button(dl, 70 * scale, 46 * scale, r, "L", VP_L);
    vpad_button(dl, 70 * scale, 46 * scale + 2.3f * r, r * 0.9f, "ZL", VP_ZL);
    vpad_button(dl, w - 70 * scale, 46 * scale, r, "R", VP_R);
    vpad_button(dl, w - 70 * scale, 46 * scale + 2.3f * r, r * 0.9f, "ZR", VP_ZR);
    vpad_button(dl, w * 0.5f - 70 * scale, 46 * scale, r * 0.8f, "-", VP_MINUS);
    vpad_button(dl, w * 0.5f + 70 * scale, 46 * scale, r * 0.8f, "+", VP_PLUS);
    // the two sticks: drag from the centre (left = move, right = camera)
    const float sx = 150 * scale, sy = h - 190 * scale, Rs = 110 * scale, knob = 48 * scale;
    const float rx0 = w - 400 * scale, ry0 = h - 190 * scale, Rr = 100 * scale;
    vpad_stick(dl, "##vpstickl", sx, sy, Rs, knob, &g_vpad_lx, &g_vpad_ly);
    vpad_stick(dl, "##vpstickr", rx0, ry0, Rr, knob * 0.9f, &g_vpad_rx, &g_vpad_ry);
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    rt_vpad_set(g_vpad_held, g_vpad_lx, g_vpad_ly, g_vpad_rx, g_vpad_ry);
    static int nlog = -1;
    if (nlog < 0) nlog = getenv("LARECOMP_VPAD_LOG") ? 0 : -2;
    if (nlog >= 0 && (nlog++ % 60) == 0)
        fprintf(stderr, "[vpad] held %#x L %d,%d R %d,%d mouse %.0f,%.0f\n", g_vpad_held, g_vpad_lx, g_vpad_ly,
                g_vpad_rx, g_vpad_ry, io.MousePos.x, io.MousePos.y);
}

// Whether menu_render draws anything: the menu, the keyboard, an overlay (FPS, inventory, tracker, chaos, race, a
// pickup card) or the on-screen pad.
static bool render_needed(bool open, bool kbd, bool inv, bool trk, bool trkd, bool chaos, bool race, bool flyin) {
    return open || kbd || g_settings.hud_mode != HUD_OFF || g_settings.show_fps || rt_speed_mult() > 1 ||
           inv || trk || trkd || chaos || race || flyin || vpad_on();
}

#ifndef __APPLE__
extern "C" void menu_prepare_frame(uint32_t target_h, bool loading) {
    if (!loading) {
        bool open = g_open.load();
        bool inv = !open && inventory_overlay_wanted();
        bool trk = !open && tracker_overlay_wanted();
        bool trkd = !open && tracker_dungeon_wanted();
        bool chaos = !open && chaos_overlay_wanted();
        bool kbd = g_kbd.load() == 1;
        bool race = race_overlay_wanted();
        bool flyin = pickup_flyin_wanted() && !open;
        if (!render_needed(open, kbd, inv, trk, trkd, chaos, race, flyin)) return;
    }
    float th = target_h > 0 ? (float)target_h : 1080.0f;
    float scale = th / 1080.0f;
    if (scale < 0.45f) scale = 0.45f;
    if (!g_inited) init(scale);
    ImGui::SetCurrentContext(g_ctx);
    ensure_fonts(scale, true);
}
#endif

#ifdef __APPLE__
void menu_render(id<MTLDevice> dev, id<MTLCommandBuffer> cb, id<MTLTexture> target) {
    const double tw = target.width, th = target.height;
#else
void menu_render(void *cmd, uint32_t target_w, uint32_t target_h) {
    const double tw = target_w, th = target_h;
#endif
    bool open = g_open.load();
    bool v13 = is_v13_ui();
    bool inv = !open && !v13 && inventory_overlay_wanted();
    bool trk = !open && !v13 && tracker_overlay_wanted();
    bool trkd = !open && !v13 && tracker_dungeon_wanted();
    bool chaos = !open && !v13 && chaos_overlay_wanted();
    bool kbd = !v13 && g_kbd.load() == 1;
    bool race = !v13 && race_overlay_wanted();  // (run timer, race seed hash: race_overlay.mm)
    bool flyin = !v13 && pickup_flyin_wanted() && !open;  // (a silent pickup's card)
    static int s_win_title_lang = -1;
    int cur_ui_lang = ui_resolve_lang();
    if (v13 && s_win_title_lang != cur_ui_lang) {
        s_win_title_lang = cur_ui_lang;
        char wtitle[256];
        snprintf(wtitle, sizeof wtitle, "13SentinelsRecomp — %s", ui_tr_lang(cur_ui_lang, STR_APP_TITLE));
        ui_window_set_title(wtitle);
    }
    if (!render_needed(open, kbd, inv, trk, trkd, chaos, race, flyin)) {
        if (g_inited) { pthread_mutex_lock(&q_lock); q_events.clear(); pthread_mutex_unlock(&q_lock); }
        g_ov_same = 0;  // (shown again: built afresh first)
        return;
    }
    float scale = (float)(th / 1080.0);
    if (scale < 0.45f) scale = 0.45f;
#ifdef __APPLE__
    if (!g_inited) init(dev, scale);
#else
    if (!g_inited) init(scale);
#endif
    ImGui::SetCurrentContext(g_ctx);
    ensure_fonts(scale, false);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)tw, (float)th);
    io.DisplayFramebufferScale = ImVec2(1, 1);
    io.FontGlobalScale = 1.0f;
    static double last;
    double now = SDL_GetTicks() / 1000.0;
    io.DeltaTime = last > 0 && now > last ? (float)(now - last) : 1.0f / 60;
    last = now;
    if (g_ov_cache < 0) g_ov_cache = !getenv("LARECOMP_OVERLAY_CACHE") || atoi(getenv("LARECOMP_OVERLAY_CACHE")) != 0;
    if (vpad_on()) g_ov_cache = 0;  // (the pad follows the mouse: its draw data can't be reused from a cached frame)
    RendererHudStats hud = {};
    int hud_mode = g_settings.hud_mode != HUD_OFF ? g_settings.hud_mode : (g_settings.show_fps ? HUD_FPS : HUD_OFF);
    uint64_t spd_mult = rt_speed_mult();
    if (hud_mode != HUD_OFF) renderer_get_hud_stats(&hud);
    char key[sizeof g_ov_key];
    key[0] = 0;
    if (g_ov_cache) {
        fps_note(now, io.DeltaTime);
        char rk[64];
        // (only the HUD and speed pill; input only reaches ImGui while the menu or the name entry is up)
        if (!open && !kbd && !inv && !trk && !trkd && !chaos && !flyin && race_overlay_key(rk, sizeof rk)) {
            pthread_mutex_lock(&q_lock);
            bool events = !q_events.empty();
            pthread_mutex_unlock(&q_lock);
            if (!events) snprintf(key, sizeof key, "%.0fx%.0f hud %d %u spd %llu l%d | %s", tw, th, hud_mode,
                                  hud_mode != HUD_OFF ? hud.seq : 0u, (unsigned long long)spd_mult, ui_resolve_lang(), rk);
        }
        ImDrawData *dd = ImGui::GetDrawData();
        if (key[0] && g_ov_same >= 3 && !strcmp(key, g_ov_key) && dd && dd->Valid) {
            g_text_busy = false;  // (the menu is closed)
#ifdef __APPLE__
            MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
            rp.colorAttachments[0].texture = target;
            rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
            rp.colorAttachments[0].storeAction = MTLStoreActionStore;
            ImGui_ImplMetal_NewFrame(rp);  // (the backend's target description only)
            id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
            ImGui_ImplMetal_RenderDrawData(dd, cb, enc);
            [enc endEncoding];
#else
            void *oc = vk_overlay_begin(cmd);  // (as below)
            ImGui_ImplVulkan_RenderDrawData(dd, (VkCommandBuffer)oc);
            vk_overlay_end(cmd, oc);
#endif
            return;
        }
        if (key[0] && !strcmp(key, g_ov_key)) g_ov_same++;
        else { memcpy(g_ov_key, key, sizeof key); g_ov_same = key[0] ? 1 : 0; }
    }
    drain_events(io);
    io.FontGlobalScale = 1.0f;

#ifdef __APPLE__
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = target;
    rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
#endif
    // spacing/rounding follow the output size too (style tuned for 1080 p)
    static ImGuiStyle base;
    static float style_scale = -1;
    if (style_scale < 0) base = ImGui::GetStyle();
    if (fabsf(style_scale - scale) > 0.01f) {
        ImGui::GetStyle() = base;
        ImGui::GetStyle().ScaleAllSizes(scale);
        style_scale = scale;
    }
#ifdef __APPLE__
    ImGui_ImplMetal_NewFrame(rp);
#else
    ImGui_ImplVulkan_NewFrame();
#endif
    ImGui::NewFrame();

    if (hud_mode != HUD_OFF) {
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 16 * scale, 16 * scale), 0, ImVec2(1, 0));
        ImGui::SetNextWindowBgAlpha(0.55f);
        ImGui::Begin("##fps", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
        double dfps = hud.fps > 0 ? hud.fps : (g_ov_cache ? g_fps_shown : io.Framerate);
        if (hud_mode == HUD_FPS) {
            ImGui::Text("%.0f FPS", dfps);
        } else {
            double avg_ms = hud.frame_avg_ms > 0 ? hud.frame_avg_ms : (dfps > 0 ? 1000.0 / dfps : 0.0);
            ImGui::Text("FPS: %.1f", dfps);
            ImGui::Text(ui_tr(STR_HUD_FTIME_FMT), avg_ms, hud.frame_max_ms);
            ImGui::Text(ui_tr(STR_HUD_DRAWS_FMT), (unsigned long long)hud.draws);
            ImGui::Text(ui_tr(STR_HUD_WAIT_FMT), hud.wait_ms);
        }
        ImGui::End();
    }

    if (spd_mult > 1) {
        ImGui::SetNextWindowPos(ImVec2(16 * scale, 16 * scale), 0, ImVec2(0, 0));
        ImGui::SetNextWindowBgAlpha(0.6f);
        ImGui::Begin("##speed", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                          ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::TextColored(ImVec4(0.98f, 0.80f, 0.30f, 1.0f), ui_tr(STR_SPEED_BADGE_FMT), (unsigned long long)spd_mult);
        ImGui::End();
    }

    if (race) race_overlay_draw(io.DisplaySize, g_menu_font_big, scale);
    if (flyin) pickup_flyin_draw(io.DisplaySize, g_font_big ? g_font_big : ImGui::GetFont(), scale);
    if (trk) tracker_overlay_draw(io.DisplaySize, g_font_big ? g_font_big : ImGui::GetFont());
    if (chaos) chaos_overlay_draw(io.DisplaySize, g_font_big ? g_font_big : ImGui::GetFont());
    if (trkd) tracker_dungeon_draw(io.DisplaySize, g_font_big ? g_font_big : ImGui::GetFont());
    if (inv) {
        inventory_overlay_draw(io.DisplaySize, g_font_big ? g_font_big : ImGui::GetFont());
    }

    if (open) {
        // dim the game behind the menu
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 110));
        float w = fminf(io.DisplaySize.x * 0.9f, 1500 * scale), h = io.DisplaySize.y - 2 * 18 * scale;  // (all the height: a slim margin)
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
        ImGui::Begin("##menu", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImFont *keep_big = g_font_big;  // (the window in the menu's font; header() uses g_font_big)
        g_font_big = g_menu_font_big;
        ImGui::PushFont(g_menu_font);
        // header as on the start screen's Settings: the title, and a close X in the corner (B / Esc too). ("Title
        // screen" - rt_request_reboot(RT_REBOOT_TITLE) - is to find a new home; user 2026-10-11.)
        const bool v13 = is_v13_ui();
        ImGui::PushFont(g_font_big);
        ImGui::TextColored(ImVec4(0.95f, 0.79f, 0.36f, 1), "%s", v13 ? ui_tr(STR_SYS_SETTINGS_TITLE) : "Settings");
        ImGui::PopFont();
        {
            float bx = 44 * scale;
            if (v13) {
                const char *close_lbl = ui_tr(STR_BTN_CLOSE_MENU);
                float cbw = fmaxf(136 * scale, ImGui::CalcTextSize(close_lbl).x + 32 * scale);
                ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - bx - cbw - 14 * scale);
                if (ImGui::Button(close_lbl, ImVec2(cbw, bx))) set_open(false);
                ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - bx);
            } else {
                ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - bx);
            }
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
            bool close = ImGui::InvisibleButton("##close", ImVec2(bx, bx), ImGuiButtonFlags_EnableNav);
            ImGui::PopStyleColor();
            bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
            ImU32 col = hot ? IM_COL32(72, 224, 246, 255) : IM_COL32(215, 226, 236, 255);
            float m = bx * 0.3f, t = (hot ? 3.0f : 2.4f) * scale * 1.5f;
            ImDrawList *dl = ImGui::GetWindowDrawList();
            if (hot) dl->AddRectFilled(p, ImVec2(p.x + bx, p.y + bx), IM_COL32(72, 224, 246, 28), 8 * scale);
            dl->AddLine(ImVec2(p.x + m, p.y + m), ImVec2(p.x + bx - m, p.y + bx - m), col, t);
            dl->AddLine(ImVec2(p.x + bx - m, p.y + m), ImVec2(p.x + m, p.y + bx - m), col, t);
            if (close) set_open(false);
        }

        bool any_popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId), typing = ui_text_busy();
        bool capturing_bind = bind_capture_active(NULL, NULL, NULL);
        if (v13) {
            float foot_h = 36.0f * scale;
            ImGui::BeginChild("##menu_tabs_wrap", ImVec2(0, -foot_h), ImGuiChildFlags_None, ImGuiWindowFlags_NavFlattened);
            tabs_body(scale, true, NULL);
            ImGui::EndChild();
            ImGui::Separator();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImVec4(0.58f, 0.78f, 0.88f, 0.95f), ui_tr(STR_HINT_BAR_FMT),
                               ui_pad_confirm_label(), ui_pad_back_label());
        } else {
            tabs_body(scale, true, NULL);
        }
        if (!any_popup && !typing && !capturing_bind && !ui_text_busy() && !controls_ui_took_back() && !rando_tab_took_back() &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
            set_open(false);
        ImGui::PopFont();
        g_font_big = keep_big;
        ImGui::End();
        osk_draw(g_font_big, scale);  // (a text field activated with the controller)
    }

    if (kbd) keyboard_draw(io, scale);
    if (!open) vpad_draw(scale);  // (the on-screen pad: no gamepad needed; hidden while the menu is up)
    ImGui::Render();
    g_text_busy = open && ui_text_busy();
#ifdef __APPLE__
    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cb, enc);
    [enc endEncoding];
#else
    // ImGui records its commands itself. Not into cmd, the window image's command buffer, while the recorder thread
    // (record.c) may still be recording it (one thread at a time on a command buffer and its pool: f054d8b, the device
    // loss entering Mabe Village), and without waiting for the recorder to catch up every frame either (a full drain:
    // 20-30 ms a frame on the Steam Deck): into a secondary command buffer of device.c's, which the recorder executes
    // in cmd in order - after the uploads queued above (overlay icons). Decided right here, where the draw data is
    // final, so an overlay turning on during this function can't miss it. (Recorder off: into cmd directly.)
    void *oc = vk_overlay_begin(cmd);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), (VkCommandBuffer)oc);  // inside the window image's rendering
    vk_overlay_end(cmd, oc);
#endif
}

#ifndef __APPLE__
extern "C" void menu_render_loading(void *cmd, uint32_t target_w, uint32_t target_h, int stage) {
    const double tw = target_w, th = target_h;
    float scale = (float)(th / 1080.0);
    if (scale < 0.45f) scale = 0.45f;
    if (!g_inited) init(scale);
    ImGui::SetCurrentContext(g_ctx);
    ensure_fonts(scale, false);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)tw, (float)th);
    io.DisplayFramebufferScale = ImVec2(1, 1);
    io.FontGlobalScale = 1.0f;
    drain_events(io);

    static double t0 = -1.0, last = -1.0;
    double now = SDL_GetTicks() / 1000.0;
    if (t0 < 0.0) t0 = now;
    io.DeltaTime = (last > 0.0 && now > last) ? (float)(now - last) : (1.0f / 60.0f);
    last = now;

    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    ui_draw_loading_visual(now - t0, stage, nullptr, scale, g_menu_font_big, g_menu_font);
    ImGui::Render();

    void *oc = vk_overlay_begin(cmd);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), (VkCommandBuffer)oc);
    vk_overlay_end(cmd, oc);
}
#endif
