// In-game overlay menu built with Dear ImGui + OpenGL 3.2+ (input queued from SDL3 events on main thread).
//
// Threads: SDL events arrive on the main thread and are queued; the menu is laid out and drawn on the
// guest render thread inside gl_backend_swap_buffers(), after the game image, onto FBO 0.
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

extern "C" {
#include "../hle.h"
#include "../settings.h"
#include "../gl/gl_backend.h"
#include "../gl/gl_imgui.h"
#include "menu.h"
void input_reset(void);
bool input_verify_window_to_touch(int *out_tx, int *out_ty);
void controls_tab_draw(float scale);  // controls_ui.mm
bool controls_ui_took_back(void);
#include "../input_bind.h"
#include "../paths.h"
#include "../platform.h"
#include "../game_addresses.h"
#include "../romfs_build.h"
}
#include <time.h>

// ------------------------------------------------------------------ state shared with the main thread
static std::atomic<bool> g_open{false};
static std::atomic<bool> g_text_busy{false};  // a text field / the on-screen keyboard has the keys (render thread: ui_text_busy)
static std::atomic<bool> g_want_fullscreen_apply{false};
static std::atomic<int> g_want_title_lang{0};

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

static int s_btest_enabled = -1;
static int s_btest_step = 0;  // 0=wait_open, 1=def_capture, 2=def_hover_click, 3=tabs, 4=wait_resize, 5=res_capture, 6=res_hover_click, 7=done
static uint64_t s_btest_step_frame = 0;
static float s_btest_tx = 0.0f, s_btest_ty = 0.0f;
static bool s_btest_rect_ready = false;
static bool s_btest_def_hov = false, s_btest_def_clk = false;
static bool s_btest_res_hov = false, s_btest_res_clk = false;
static bool s_btest_down_sent = false;
static bool s_btest_up_sent = false;
static bool s_btest_touch_ok = false;
static int s_btest_touch_x = -1, s_btest_touch_y = -1;

static void btest_push_sdl_motion(float x, float y) {
    SDL_Window *w = gl_backend_window();
    if (!w) return;
    SDL_Event ev = {};
    ev.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.windowID = SDL_GetWindowID(w);
    ev.motion.x = x;
    ev.motion.y = y;
    SDL_PushEvent(&ev);
}

static void btest_push_sdl_btn(bool down, float x, float y) {
    SDL_Window *w = gl_backend_window();
    if (!w) return;
    SDL_Event ev = {};
    ev.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    ev.button.windowID = SDL_GetWindowID(w);
    ev.button.button = SDL_BUTTON_LEFT;
    ev.button.down = down;
    ev.button.clicks = 1;
    ev.button.x = x;
    ev.button.y = y;
    SDL_PushEvent(&ev);
}

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

extern "C" void menu_set_open_for_test(bool open) {
    if (open == g_open.load()) return;
    g_open.store(open);
    input_reset();
    bind_capture_cancel();
    if (!open) { osk_cancel(); g_text_busy = false; }
}

extern "C" void menu_set_tab_for_test(int tab_idx) {
    extern int g_tab_request_ext;
    g_tab_request_ext = tab_idx;
}

void menu_tabs_focus(bool first_tab);
static void set_open(bool open) {
    if (open == g_open.load()) return;
    g_open.store(open);
    input_reset();  // nothing held while the menu had focus leaks into the game
    bind_capture_cancel();
    if (open) {
        static bool opened;  // the controller's cursor: the first tab (Display) the first time this session, later the
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

static void cycle_game_speed(void) {
    if (rt_speed_locked_by_env()) return;
    uint32_t pct = rt_speed_pct();
    uint32_t next = (pct < 150) ? 150 : (pct < 200) ? 200 : (pct < 300) ? 300 : 100;
    rt_set_speed_pct(next);
    g_settings.speed_pct = (int)next;
    settings_save();
}

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
            cycle_game_speed();
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
        if (down && !typing && back_down && e->gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) {
            cycle_game_speed();
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
        { QEvent q = {}; q.kind = QEvent::MOUSE_POS; q.x = e->motion.x; q.y = e->motion.y; push(q); }
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
    static bool text_input;
    bool want_text = capturing();
    if (w && want_text != text_input) {
        if (want_text) SDL_StartTextInput(w);
        else SDL_StopTextInput(w);
        text_input = want_text;
    }
    int want_lang = g_want_title_lang.exchange(0, std::memory_order_acq_rel);
    if (want_lang > 0) {
        char wtitle[256];
        snprintf(wtitle, sizeof wtitle, "YokaiWatch1Recomp — %s", ui_tr_lang(want_lang, STR_APP_TITLE));
        gl_backend_window_set_title(wtitle);
    }
    if (g_want_fullscreen_apply.exchange(false)) {
        g_settings.fullscreen = (g_settings.window_mode != WIN_WINDOWED);
        gl_backend_apply_window_settings();
    }
    if (uint32_t r = __atomic_exchange_n(&g_window_resize_request, 0u, __ATOMIC_RELAXED)) {
        if (w) {
            SDL_SetWindowSize(w, (int)(r >> 16), (int)(r & 0xffff));
            SDL_SyncWindow(w);
            gl_backend_refresh_metrics();
        }
    }
}

// ------------------------------------------------------------------ render thread
static bool g_inited;
static ImGuiContext *g_ctx;  // ours: the start screen (back to it after a reboot) has its own context
static ImFont *g_font, *g_font_big;
static ImFont *g_menu_font, *g_menu_font_big;
static float g_built_raster_scale = -1.0f;
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
    ui_apply_yw1_theme(1.0f);
}

static void ensure_fonts(float scale, float pix_density, bool allow_gl_rebuild) {
    float pd = (pix_density >= 0.5f && pix_density <= 8.0f) ? pix_density : 1.0f;
    float fsc = scale * pd;
    if (fsc < 0.55f) fsc = 0.55f;
    if (fsc > 5.0f) fsc = 5.0f;
    int lang = ui_resolve_lang();
    if (g_built_raster_scale > 0.0f && g_built_ui_lang == lang && fabsf(g_built_raster_scale - fsc) <= 0.04f) return;
    if (g_inited && !allow_gl_rebuild) return;
    ui_load_i18n_fonts(&g_font, &g_menu_font, &g_font_big, 28.0f * fsc, 32.0f * fsc, 42.0f * fsc, lang, nullptr, nullptr);
    g_menu_font_big = g_font_big;
    g_built_raster_scale = fsc;
    g_built_ui_lang = lang;
    if (g_inited) {
        gl_imgui_fonts_rebuild();
    }
}

static void init(float scale, float pix_density) {
    IMGUI_CHECKVERSION();
    g_ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(g_ctx);
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.IniFilename = NULL;
    g_built_raster_scale = -1.0f;
    ensure_fonts(scale, pix_density, true);
    style_init();
    gl_imgui_overlay_init();
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

extern "C" int input_pad_family(void);  // runtime/hle/input.c
extern "C" void rt_request_reboot(int kind);  // runtime/main.c (RT_REBOOT_TITLE = 2: the start screen)

static void tab_display(float scale) {
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
    const char *res_scales[3] = {ui_tr(STR_RES_SCALE_1X), ui_tr(STR_RES_SCALE_1_5X), ui_tr(STR_RES_SCALE_2X)};
    if (opt_choice(ui_tr(STR_OPT_RES_SCALE), &g_settings.res_scale, res_scales, 3, RES_SCALE_1X,
                   ui_tr(STR_OPT_RES_SCALE_DESC), true)) {
        settings_save();
    }
    const char *vsync_opts[3] = {ui_tr(STR_VSYNC_MAILBOX), ui_tr(STR_VSYNC_FIFO), ui_tr(STR_VSYNC_IMMEDIATE)};
    if (opt_choice(ui_tr(STR_OPT_VSYNC), &g_settings.vsync_mode, vsync_opts, 3, VSYNC_MAILBOX,
                   ui_tr(STR_OPT_VSYNC_DESC), false)) {
        ui_request_window_apply();
        settings_save();
    }

    opt_section(ui_tr(STR_SEC_MONITOR_PACE));
    const char *hud_opts[3] = {ui_tr(STR_HUD_OFF), ui_tr(STR_HUD_FPS), ui_tr(STR_HUD_DETAILED)};
    if (opt_choice(ui_tr(STR_OPT_HUD), &g_settings.hud_mode, hud_opts, 3, HUD_OFF,
                   ui_tr(STR_OPT_HUD_DESC), false)) {
        g_settings.show_fps = (g_settings.hud_mode != HUD_OFF);
        settings_save();
    }
    {
        const char *fps_opts[2] = {ui_tr(STR_FPS_30), ui_tr(STR_FPS_60)};
        bool env_locked = gl_backend_fps_locked_by_env();
        bool supported = gl_backend_60fps_supported();
        const char *env_fps = getenv("YW1_EXP_60FPS");
        int shown = (env_locked && env_fps)
                        ? (strcmp(env_fps, "0") == 0 ? FPS_MODE_30 : FPS_MODE_60)
                        : (!supported ? FPS_MODE_30
                                      : __atomic_load_n(&g_settings.fps_mode, __ATOMIC_RELAXED));
        if (shown < 0 || shown > 1) shown = FPS_MODE_60;
        const char *desc = env_locked ? ui_tr(STR_OPT_FPS_MODE_DESC_LOCKED)
                                      : (!supported ? ui_tr(STR_OPT_FPS_MODE_DESC_UNSUPPORTED)
                                                    : ui_tr(STR_OPT_FPS_MODE_DESC));
        if (env_locked || !supported) opt_disable_next();
        if (opt_choice(ui_tr(STR_OPT_FPS_MODE), &shown, fps_opts, 2, FPS_MODE_60, desc, false)) {
            __atomic_store_n(&g_settings.fps_mode, shown, __ATOMIC_RELAXED);
            settings_save();
        }
    }
    {
        const char *spd_opts[4] = {
            ui_tr(STR_SPEED_1X), ui_tr(STR_SPEED_1_5X), ui_tr(STR_SPEED_2X), ui_tr(STR_SPEED_3X)
        };
        static const int k_pcts[4] = {100, 150, 200, 300};
        uint32_t cur_pct = rt_speed_pct();
        int sp_idx = cur_pct >= 300 ? 3 : cur_pct >= 200 ? 2 : cur_pct >= 150 ? 1 : 0;
        bool env_locked = rt_speed_locked_by_env();
        if (env_locked) opt_disable_next();
        if (opt_choice(ui_tr(STR_OPT_SPEED), &sp_idx, spd_opts, 4, 0,
                       env_locked ? ui_tr(STR_OPT_SPEED_DESC_LOCKED) : ui_tr(STR_OPT_SPEED_DESC), false)) {
            int np = k_pcts[sp_idx >= 0 && sp_idx < 4 ? sp_idx : 0];
            rt_set_speed_pct((uint32_t)np);
            g_settings.speed_pct = np;
            settings_save();
        }
    }
    opt_end();
}

static void tab_audio(float scale) {
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

static void tab_system(float scale, bool in_game) {
    static char save_msg[256];
    static bool game_lang_saved_notice = false;
    static int ask = 0;  // 0: idle, 1: confirm return to launcher, 2: confirm quit
    if (ask && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
        ask = 0;

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

    opt_section(ui_tr(STR_SEC_MODS));
    {
        int active_mods = fs_mods_count();
        char mod_stat[256];
        if (g_settings.mods_enabled)
            snprintf(mod_stat, sizeof mod_stat, ui_tr(STR_MODS_STATUS_ACTIVE_FMT), active_mods);
        else
            snprintf(mod_stat, sizeof mod_stat, "%s", ui_tr(STR_MODS_STATUS_DISABLED));
        std::string mdesc = std::string(ui_tr(STR_OPT_MODS_ENABLE_DESC)) + "\n\n" + mod_stat;
        if (opt_toggle(ui_tr(STR_OPT_MODS_ENABLE), &g_settings.mods_enabled, true, mdesc.c_str())) {
            settings_save();
        }
        char mdir[1100];
        fs_mods_dir(mdir, sizeof mdir);
        std::string open_desc = std::string(mdir) + "\n\n" + mod_stat;
        if (opt_button(ui_tr(STR_BTN_OPEN_MODS), open_desc.c_str())) {
            plat_mkdir_p(mdir);
            plat_open_folder(mdir);
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
    snprintf(g_kbd_text, sizeof g_kbd_text, "%s", initial ? initial : "");
    g_kbd_max = max_len > 0 && max_len < (int)sizeof g_kbd_text / 4 ? max_len : 8;
    input_reset();
    g_kbd = 1;
    // (test) LARECOMP_KBD_AUTO=<text>: answer at once
    if (getenv("LARECOMP_KBD_AUTO")) { snprintf(g_kbd_text, sizeof g_kbd_text, "%s", getenv("LARECOMP_KBD_AUTO")); g_kbd = 2; }
    while (g_kbd.load() == 1 && !g_rt_unwind) {  // ~60 Hz (a reboot cancels)
        uint64_t t0 = SDL_GetTicks();
        if (SDL_GL_GetCurrentContext()) {
            int ww = 1280, wh = 720, pw = 1280, ph = 720;
            float dens = 1.0f;
            gl_backend_get_cached_metrics(&ww, &wh, &pw, &ph, &dens);
            gl_hle_blit_virtual_fbo(pw, ph);
            gl_imgui_render_in_game_overlay(gl_backend_present_count(), ww, wh, pw, ph, dens);
            if (SDL_Window *w = gl_backend_window()) SDL_GL_SwapWindow(w);
        }
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
    Tab tabs[4];
    int ntabs = 0;
    tabs[ntabs++] = {ui_tr(STR_TAB_DISPLAY), 0, true};
    tabs[ntabs++] = {ui_tr(STR_TAB_AUDIO), 1, true};
    tabs[ntabs++] = {ui_tr(STR_TAB_CONTROLS), 8, true};
    tabs[ntabs++] = {ui_tr(STR_TAB_SYSTEM), 9, true};
    (void)prelaunch;
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
            if (g_focus_tab == -1 && !prelaunch) g_focus_tab = (g_tab_request >= 0) ? g_tab_request : tabs[0].id;
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
                case 0: tab_display(scale); break;
                case 1: tab_audio(scale); break;
                case 8: controls_tab_draw(scale); break;
                case 9: tab_system(scale, in_game); break;
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
static bool render_needed(bool open, bool kbd) {
    return open || kbd || g_settings.hud_mode != HUD_OFF || g_settings.show_fps || rt_speed_pct() > 100 || vpad_on();
}

extern "C" bool menu_prepare_frame(uint32_t target_h, float pixel_density, bool loading) {
    if (s_btest_enabled < 0) {
        const char *env = getenv("YW1_OVERLAY_TEST");
        s_btest_enabled = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
    }
    if (s_btest_enabled == 1 && !loading) {
        uint64_t f = gl_backend_present_count();
        if (s_btest_step == 0 && f >= 480) {
            g_settings.hud_mode = HUD_DETAILED;
            g_settings.show_fps = true;
            menu_set_open_for_test(true);
            menu_set_tab_for_test(0);
            s_btest_rect_ready = false;
            s_btest_step = 1;
            s_btest_step_frame = f;
            LOG("[ui-test] frame=%llu YW1_OVERLAY_TEST opened overlay (tab=0 Display) + HUD_DETAILED\n",
                (unsigned long long)f);
        } else if (s_btest_step == 3) {
            uint64_t dt = f - s_btest_step_frame;
            if (dt == 1) {
                menu_set_open_for_test(true);
                menu_set_tab_for_test(1);
                LOG("[ui-test] frame=%llu YW1_OVERLAY_TEST switched tab=1 (Audio)\n", (unsigned long long)f);
            } else if (dt == 3) {
                menu_set_tab_for_test(8);
                LOG("[ui-test] frame=%llu YW1_OVERLAY_TEST switched tab=8 (Controls)\n", (unsigned long long)f);
            } else if (dt == 5) {
                menu_set_tab_for_test(9);
                LOG("[ui-test] frame=%llu YW1_OVERLAY_TEST switched tab=9 (System)\n", (unsigned long long)f);
            } else if (dt >= 7) {
                menu_set_open_for_test(true);
                menu_set_tab_for_test(0);
                g_window_resize_request = (1600u << 16) | 900u;
                s_btest_rect_ready = false;
                s_btest_step = 4;
                s_btest_step_frame = f;
            }
        }
    }
    if (!loading) {
        bool open = g_open.load();
        bool kbd = g_kbd.load() == 1;
        if (!render_needed(open, kbd)) {
            if (g_inited) { pthread_mutex_lock(&q_lock); q_events.clear(); pthread_mutex_unlock(&q_lock); }
            g_ov_same = 0;
            return false;
        }
    }
    float th = target_h > 0 ? (float)target_h : 720.0f;
    float scale = th / 1080.0f;
    if (scale < 0.45f) scale = 0.45f;
    if (scale > 2.5f) scale = 2.5f;
    float pd = (pixel_density >= 0.5f && pixel_density <= 8.0f) ? pixel_density : 1.0f;
    if (!g_inited) init(scale, pd);
    ImGui::SetCurrentContext(g_ctx);
    if (!ImGui::GetIO().BackendRendererUserData) gl_imgui_overlay_init();
    ensure_fonts(scale, pd, true);
    return true;
}

void menu_render_opengl(int win_w, int win_h, int pix_w, int pix_h, float pixel_density) {
    int ww = win_w > 0 ? win_w : 1280, wh = win_h > 0 ? win_h : 720;
    int pw = pix_w > 0 ? pix_w : ww, ph = pix_h > 0 ? pix_h : wh;
    float fb_sx = (float)pw / (float)ww;
    float fb_sy = (float)ph / (float)wh;
    float pd = (pixel_density >= 0.5f && pixel_density <= 8.0f) ? pixel_density : (fb_sx + fb_sy) * 0.5f;
    if (pd < 0.5f) pd = 1.0f;
    bool open = g_open.load();
    bool kbd = g_kbd.load() == 1;
    bool loading = gl_backend_is_loading();
    static int s_win_title_lang = -1;
    int cur_ui_lang = ui_resolve_lang();
    if (s_win_title_lang != cur_ui_lang) {
        s_win_title_lang = cur_ui_lang;
        g_want_title_lang.store(cur_ui_lang, std::memory_order_release);
    }
    if (!loading && !render_needed(open, kbd)) {
        if (g_inited) { pthread_mutex_lock(&q_lock); q_events.clear(); pthread_mutex_unlock(&q_lock); }
        g_ov_same = 0;
        return;
    }
    float scale = (float)wh / 1080.0f;
    if (scale < 0.45f) scale = 0.45f;
    if (scale > 2.5f) scale = 2.5f;
    if (!g_inited) init(scale, pd);
    ImGui::SetCurrentContext(g_ctx);
    if (!ImGui::GetIO().BackendRendererUserData) gl_imgui_overlay_init();
    ensure_fonts(scale, pd, true);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)ww, (float)wh);
    io.DisplayFramebufferScale = ImVec2(fb_sx, fb_sy);
    io.FontGlobalScale = 1.0f / pd;
    static double last;
    double now = SDL_GetTicks() / 1000.0;
    io.DeltaTime = last > 0 && now > last ? (float)(now - last) : 1.0f / 60;
    last = now;
    if (g_ov_cache < 0) g_ov_cache = !getenv("LARECOMP_OVERLAY_CACHE") || atoi(getenv("LARECOMP_OVERLAY_CACHE")) != 0;
    if (vpad_on()) g_ov_cache = 0;
    int hud_mode = g_settings.hud_mode != HUD_OFF ? g_settings.hud_mode : (g_settings.show_fps ? HUD_FPS : HUD_OFF);
    uint32_t req_spd_pct = rt_speed_pct();
    uint32_t eff_spd_pct = rt_effective_speed_pct();
    if (g_ov_cache) fps_note(now, io.DeltaTime);

    uint64_t total_draws = 0, gl_errs = 0;
    int sh_ok = 0, sh_fail = 0;
    static uint64_t prev_draws = 0;
    static uint32_t frame_draws = 0;
    static float max_ms = 0.0f;
    static double max_ms_reset_t = 0.0;
    gl_hle_get_stats(&total_draws, &gl_errs, &sh_ok, &sh_fail);
    if (total_draws >= prev_draws) frame_draws = (uint32_t)(total_draws - prev_draws);
    prev_draws = total_draws;
    float cur_ms = io.DeltaTime * 1000.0f;
    if (now - max_ms_reset_t > 2.0 || cur_ms > max_ms) {
        if (now - max_ms_reset_t > 2.0) { max_ms = cur_ms; max_ms_reset_t = now; }
        else max_ms = cur_ms;
    }

    drain_events(io);

    static ImGuiStyle base;
    static float style_scale = -1;
    if (style_scale < 0) base = ImGui::GetStyle();
    if (fabsf(style_scale - scale) > 0.01f) {
        ImGui::GetStyle() = base;
        ImGui::GetStyle().ScaleAllSizes(scale);
        style_scale = scale;
    }

    gl_imgui_overlay_new_frame();
    ImGui::NewFrame();

    if (loading) {
        static double load_t0 = -1.0;
        if (load_t0 < 0.0) load_t0 = now;
        ui_draw_loading_visual(now - load_t0, 3, ui_tr(STR_LOAD_STAGE_3), scale, g_menu_font_big, g_menu_font);
    }

    if (!loading && hud_mode != HUD_OFF) {
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 16 * scale, 16 * scale), 0, ImVec2(1, 0));
        ImGui::SetNextWindowBgAlpha(0.55f);
        ImGui::Begin("##fps", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
        double dfps = (g_ov_cache && g_fps_shown > 0) ? g_fps_shown : io.Framerate;
        if (hud_mode == HUD_FPS) {
            ImGui::Text("%.0f FPS", dfps);
        } else {
            double avg_ms = dfps > 0 ? 1000.0 / dfps : cur_ms;
            const char *ven = nullptr, *ren = nullptr, *ver = nullptr;
            gl_hle_get_renderer_strings(&ven, &ren, &ver);
            ImGui::Text("FPS: %.1f", dfps);
            ImGui::Text(ui_tr(STR_HUD_FTIME_FMT), avg_ms, (double)max_ms);
            ImGui::Text(ui_tr(STR_HUD_DRAWS_FMT), (unsigned long long)frame_draws);
            ImGui::Text("OpenGL 4.5 · %s (GL err: %llu)", ren ? ren : "GPU", (unsigned long long)gl_errs);
        }
        ImGui::End();
    }

    if (!loading && req_spd_pct > 100) {
        ImGui::SetNextWindowPos(ImVec2(16 * scale, 16 * scale), 0, ImVec2(0, 0));
        ImGui::SetNextWindowBgAlpha(0.6f);
        ImGui::Begin("##speed", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                          ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
        if (eff_spd_pct > 100) {
            ImGui::TextColored(ImVec4(0.98f, 0.80f, 0.30f, 1.0f), ui_tr(STR_SPEED_BADGE_FMT),
                               (double)eff_spd_pct / 100.0);
        } else {
            ImGui::TextColored(ImVec4(0.55f, 0.86f, 0.98f, 1.0f), ui_tr(STR_SPEED_MOVIE_PAUSED_FMT),
                               (double)req_spd_pct / 100.0);
        }
        ImGui::End();
    }

    if (!loading && open) {
        // dim the game behind the menu
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 110));
        float w = fminf(io.DisplaySize.x * 0.9f, 1500 * scale), h = io.DisplaySize.y - 2 * 18 * scale;
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
        ImGui::Begin("##menu", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImFont *keep_big = g_font_big;
        g_font_big = g_menu_font_big;
        ImGui::PushFont(g_menu_font);
        ImGui::PushFont(g_font_big);
        ImGui::TextColored(ImVec4(0.95f, 0.79f, 0.36f, 1), "%s", ui_tr(STR_SYS_SETTINGS_TITLE));
        ImGui::PopFont();
        {
            float bx = 44 * scale;
            const char *close_lbl = ui_tr(STR_BTN_CLOSE_MENU);
            float cbw = fmaxf(136 * scale, ImGui::CalcTextSize(close_lbl).x + 32 * scale);
            ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - bx - cbw - 14 * scale);
            bool close_clicked = ImGui::Button(close_lbl, ImVec2(cbw, bx));
            if (s_btest_enabled == 1 && open) {
                ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
                float cx = (bmin.x + bmax.x) * 0.5f, cy = (bmin.y + bmax.y) * 0.5f;
                bool bhov = ImGui::IsItemHovered();
                uint64_t f = gl_backend_present_count();
                if (s_btest_step == 1 || (s_btest_step == 4 && ww == 1600 && wh == 900 && f > s_btest_step_frame + 1)) {
                    s_btest_tx = cx;
                    s_btest_ty = cy;
                    s_btest_rect_ready = true;
                    s_btest_down_sent = false;
                    s_btest_up_sent = false;
                    btest_push_sdl_motion(cx, cy);
                    s_btest_step = (s_btest_step == 1) ? 2 : 6;
                    s_btest_step_frame = f;
                    if (ww == 1600 && wh == 900) {
                        s_btest_touch_ok = input_verify_window_to_touch(&s_btest_touch_x, &s_btest_touch_y);
                    }
                } else if ((s_btest_step == 2 || s_btest_step == 6) && s_btest_rect_ready) {
                    float err = hypotf(io.MousePos.x - s_btest_tx, io.MousePos.y - s_btest_ty);
                    bool is_def = (s_btest_step == 2);
                    bool &hov_flag = is_def ? s_btest_def_hov : s_btest_res_hov;
                    bool &clk_flag = is_def ? s_btest_def_clk : s_btest_res_clk;
                    if (!hov_flag && err < 1.0f && bhov) {
                        hov_flag = true;
                        LOG("[ui-test] hit_test stage=B mode=%s res_scale=%.1fx win=%dx%d pix=%dx%d disp_scale=%.3f DisplaySize=(%.0f,%.0f) fb_scale=(%.3f,%.3f) viewport=%dx%d target=(%.1f,%.1f) mouse=(%.1f,%.1f) err=%.3f hovered=1\n",
                            is_def ? "default" : "resized", (double)gl_hle_active_res_scale_pct() / 100.0,
                            ww, wh, pw, ph, (double)gl_backend_display_scale(),
                            io.DisplaySize.x, io.DisplaySize.y, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y,
                            pw, ph, s_btest_tx, s_btest_ty, io.MousePos.x, io.MousePos.y, err);
                        btest_push_sdl_btn(true, s_btest_tx, s_btest_ty);
                        s_btest_down_sent = true;
                    } else if (hov_flag && s_btest_down_sent && !s_btest_up_sent && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        btest_push_sdl_btn(false, s_btest_tx, s_btest_ty);
                        s_btest_up_sent = true;
                    }
                    if (close_clicked) {
                        clk_flag = true;
                        LOG("[ui-test] hit_test stage=B mode=%s clicked=1 glErrors=%llu\n",
                            is_def ? "default" : "resized", (unsigned long long)gl_imgui_gl_error_count());
                        if (is_def) {
                            s_btest_step = 3;
                            s_btest_step_frame = f;
                        } else {
                            s_btest_step = 7;
                            LOG("[ui-test] DONE stage=B hit_default=%s hit_resized=%s touch_center=(%d,%d) touch_ok=%d res_scale=%.1fx glErrors=%llu status=%s\n",
                                (s_btest_def_hov && s_btest_def_clk) ? "PASS" : "FAIL",
                                (s_btest_res_hov && s_btest_res_clk) ? "PASS" : "FAIL",
                                s_btest_touch_x, s_btest_touch_y, s_btest_touch_ok ? 1 : 0,
                                (double)gl_hle_active_res_scale_pct() / 100.0,
                                (unsigned long long)gl_imgui_gl_error_count(),
                                (s_btest_def_hov && s_btest_def_clk && s_btest_res_hov && s_btest_res_clk &&
                                 s_btest_touch_ok && gl_imgui_gl_error_count() == 0) ? "PASS" : "FAIL");
                        }
                    }
                }
            }
            if (close_clicked) set_open(false);
            ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - bx);
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
        float foot_h = 36.0f * scale;
        ImGui::BeginChild("##menu_tabs_wrap", ImVec2(0, -foot_h), ImGuiChildFlags_None, ImGuiWindowFlags_NavFlattened);
        tabs_body(scale, true, NULL);
        ImGui::EndChild();
        ImGui::Separator();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.58f, 0.78f, 0.88f, 0.95f), ui_tr(STR_HINT_BAR_FMT),
                           ui_pad_confirm_label(), ui_pad_back_label());
        if (!any_popup && !typing && !capturing_bind && !ui_text_busy() && !controls_ui_took_back() &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
            set_open(false);
        ImGui::PopFont();
        g_font_big = keep_big;
        ImGui::End();
        osk_draw(g_font_big, scale);
    }

    if (!loading && kbd) keyboard_draw(io, scale);
    if (!loading && !open) vpad_draw(scale);
    ImGui::Render();
    g_text_busy = open && ui_text_busy();
    gl_imgui_overlay_render_draw_data(ImGui::GetDrawData(), pw, ph, gl_backend_present_count());
}
