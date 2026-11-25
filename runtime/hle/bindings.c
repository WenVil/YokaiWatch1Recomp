// Control remapping: tables runtime/hle/input.c reads on every input event (so a change applies at once), the
// capture used by the "click a binding, then press" UI, and their larecomp.ini lines. See runtime/input_bind.h.
#include <SDL3/SDL.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "../hle.h"
#include "../input_bind.h"
#include "../settings.h"

_Static_assert(PADB_LTRIGGER == SDL_GAMEPAD_BUTTON_COUNT, "trigger pseudo-buttons follow SDL's buttons");

// Defaults:
// - Gamepad: Nintendo physical layout (east = A, south = B, north = X, west = Y), triggers = ZL / ZR, plus analog stick axes.
// - Keyboard (13S desktop layout): WASD = Left Stick, Arrows = D-pad, J/K/L/I = A/B/X/Y, Q/E = L/R, 1/3 = ZL/ZR,
//   Enter/Backspace = Plus/Minus, F/R = L3/R3, Numpad 8/5/4/6 = Right Stick.
#define DEF_PAD { \
    SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_WEST, \
    SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, \
    SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PADB_LTRIGGER, PADB_RTRIGGER, SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_BACK, \
    SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_DOWN, \
    PADB_LS_UP, PADB_LS_DOWN, PADB_LS_LEFT, PADB_LS_RIGHT, PADB_RS_UP, PADB_RS_DOWN, PADB_RS_LEFT, PADB_RS_RIGHT}
#define DEF_KEY { \
    SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_L, SDL_SCANCODE_I, SDL_SCANCODE_F, SDL_SCANCODE_R, \
    SDL_SCANCODE_Q, SDL_SCANCODE_E, SDL_SCANCODE_1, SDL_SCANCODE_3, SDL_SCANCODE_RETURN, SDL_SCANCODE_BACKSPACE, \
    SDL_SCANCODE_LEFT, SDL_SCANCODE_UP, SDL_SCANCODE_RIGHT, SDL_SCANCODE_DOWN, \
    SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_D, \
    SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_6}
static const int16_t k_def_pad[BIND_PAD_COUNT] = DEF_PAD, k_def_key[BIND_KEY_COUNT] = DEF_KEY;
int16_t g_bind_pad[BIND_PAD_COUNT] = DEF_PAD, g_bind_key[BIND_KEY_COUNT] = DEF_KEY;

static const char *const k_ini[BIND_KEY_COUNT] = {
    "a", "b", "x", "y", "lstick", "rstick", "l", "r", "zl", "zr", "plus", "minus", "left", "up", "right", "down",
    "ls_up", "ls_down", "ls_left", "ls_right", "rs_up", "rs_down", "rs_left", "rs_right",
};
static const char *const k_names[BIND_KEY_COUNT] = {
    "A", "B", "X", "Y", "Left stick press", "Right stick press", "L", "R", "ZL", "ZR", "Plus", "Minus",
    "D-pad Left", "D-pad Up", "D-pad Right", "D-pad Down", "Left stick Up", "Left stick Down", "Left stick Left",
    "Left stick Right", "Right stick Up", "Right stick Down", "Right stick Left", "Right stick Right",
};
// controller buttons/axes by position (larecomp.ini)
static const char *const k_pad_ini[PADB_HOST_COUNT] = {
    "south", "east", "west", "north", "back", "guide", "start", "lstick", "rstick", "lshoulder", "rshoulder",
    "dpad_up", "dpad_down", "dpad_left", "dpad_right", "misc1", "rpaddle1", "lpaddle1", "rpaddle2", "lpaddle2",
    "touchpad", "misc2", "misc3", "misc4", "misc5", "misc6", "ltrigger", "rtrigger",
    "ls_up", "ls_down", "ls_left", "ls_right", "rs_up", "rs_down", "rs_left", "rs_right",
};

static const struct { int16_t code; const char *label; const char *alt; } k_mouse_keys[] = {
    {KEY_MOUSE_LEFT,   "Mouse Left",   "mouse_left"},
    {KEY_MOUSE_RIGHT,  "Mouse Right",  "mouse_right"},
    {KEY_MOUSE_MIDDLE, "Mouse Middle", "mouse_middle"},
    {KEY_MOUSE_X1,     "Mouse X1",     "mouse_x1"},
    {KEY_MOUSE_X2,     "Mouse X2",     "mouse_x2"},
    {KEY_WHEEL_UP,     "Wheel Up",     "wheel_up"},
    {KEY_WHEEL_DOWN,   "Wheel Down",   "wheel_down"},
};

bool bind_is_mouse_code(int code) { return code >= KEY_MOUSE_LEFT && code < KEY_MOUSE_END; }

static const char *mouse_label_of(int code) {
    for (size_t i = 0; i < sizeof k_mouse_keys / sizeof *k_mouse_keys; i++)
        if (k_mouse_keys[i].code == code) return k_mouse_keys[i].label;
    return NULL;
}

static int mouse_code_from_name(const char *val) {
    for (size_t i = 0; i < sizeof k_mouse_keys / sizeof *k_mouse_keys; i++)
        if (!strcmp(val, k_mouse_keys[i].label) || !strcmp(val, k_mouse_keys[i].alt))
            return k_mouse_keys[i].code;
    return 0;
}

bool bind_hotkey_reserved(int sc) {
    if (sc <= 0) return false;
    return sc == g_settings.hotkey_menu || sc == g_settings.hotkey_hud || sc == g_settings.hotkey_speed ||
           sc == g_settings.hotkey_fullscreen || sc == SDL_SCANCODE_F9;
}

int bind_slot_from_name(const char *name) {
    for (int i = 0; i < BIND_KEY_COUNT; i++) if (!strcmp(name, k_ini[i])) return i;
    return -1;
}
int bind_pad_from_name(const char *name) {
    for (int i = 0; i < PADB_HOST_COUNT; i++) if (!strcmp(name, k_pad_ini[i])) return i;
    return -1;
}

const char *bind_input_name(int slot) { return slot >= 0 && slot < BIND_KEY_COUNT ? k_names[slot] : "?"; }

// fam: 0 Nintendo, 1 Xbox (and pads SDL labels like one), 2 PlayStation
const char *bind_pad_label(int host, int fam) {
    static const char *const nin[PADB_HOST_COUNT] = {
        "B", "A", "Y", "X", "Minus", "Home", "Plus", "Left stick press", "Right stick press", "L", "R",
        "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right", "Capture", "Right paddle 1", "Left paddle 1",
        "Right paddle 2", "Left paddle 2", "Touchpad", "Misc 2", "Misc 3", "Misc 4", "Misc 5", "Misc 6", "ZL", "ZR",
        "LS Up", "LS Down", "LS Left", "LS Right", "RS Up", "RS Down", "RS Left", "RS Right",
    };
    static const char *const xb[PADB_HOST_COUNT] = {
        "A", "B", "X", "Y", "View", "Xbox", "Menu", "LS (press)", "RS (press)", "LB", "RB",
        "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right", "Share", "Paddle P1", "Paddle P3",
        "Paddle P2", "Paddle P4", "Touchpad", "Misc 2", "Misc 3", "Misc 4", "Misc 5", "Misc 6", "LT", "RT",
        "LS Up", "LS Down", "LS Left", "LS Right", "RS Up", "RS Down", "RS Left", "RS Right",
    };
    static const char *const ps[PADB_HOST_COUNT] = {
        "Cross", "Circle", "Square", "Triangle", "Create", "PS", "Options", "L3", "R3", "L1", "R1",
        "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right", "Mute", "Right back button", "Left back button",
        "Right Fn", "Left Fn", "Touchpad", "Misc 2", "Misc 3", "Misc 4", "Misc 5", "Misc 6", "L2", "R2",
        "L-Stick Up", "L-Stick Down", "L-Stick Left", "L-Stick Right", "R-Stick Up", "R-Stick Down", "R-Stick Left", "R-Stick Right",
    };
    if (host < 0 || host >= PADB_HOST_COUNT) return "(none)";
    return fam == 1 ? xb[host] : fam == 2 ? ps[host] : nin[host];
}

const char *bind_key_label(int sc) {
    if (sc <= 0) return "(none)";
    if (bind_is_mouse_code(sc)) {
        const char *m = mouse_label_of(sc);
        return m ? m : "(mouse)";
    }
    if (sc >= SDL_SCANCODE_COUNT) return "(none)";
    const char *n = SDL_GetScancodeName((SDL_Scancode)sc);
    return n && *n ? n : "(unnamed key)";
}

int bind_label_family(void) {
    extern int input_pad_family(void);  // runtime/hle/input.c (the game's open pad, or LARECOMP_PAD_TYPE)
    int f = input_pad_family();
    if (f) return f;
    int n = 0, t = 0;  // start screen: input.c hasn't opened a pad yet
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    if (ids && n > 0) t = SDL_GetGamepadTypeForID(ids[0]);
    bool any = ids && n > 0;
    SDL_free(ids);
    return any ? input_family_of_type(t) : 0;
}

static void result(int kind, int dev, int slot, int other);

void bind_reset(int dev) {
    if (dev == BIND_DEV_PAD) {
        memcpy(g_bind_pad, k_def_pad, sizeof g_bind_pad);
        g_settings.pad_layout = PAD_LAYOUT_NINTENDO;
    } else {
        memcpy(g_bind_key, k_def_key, sizeof g_bind_key);
    }
    LOG("[input] %s bindings reset to the defaults\n", dev == BIND_DEV_PAD ? "controller" : "keyboard");
    result(5, dev, -1, -1);
}

static void apply_pad_face_preset(int layout, const int16_t face[4]) {
    g_settings.pad_layout = layout;
    for (int s = 0; s < 4; s++) {
        int16_t target = face[s];
        for (int i = 4; i < BIND_PAD_COUNT; i++)
            if (g_bind_pad[i] == target) g_bind_pad[i] = g_bind_pad[s];
        g_bind_pad[s] = target;
    }
    LOG("[input] controller preset: %s layout\n", layout == PAD_LAYOUT_XBOX ? "Xbox letter" : "Nintendo physical");
    result(5, BIND_DEV_PAD, -1, -1);
}

void bind_preset_pad_nintendo(void) {
    static const int16_t nin_face[4] = {
        SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_WEST
    };
    apply_pad_face_preset(PAD_LAYOUT_NINTENDO, nin_face);
}

void bind_preset_pad_xbox(void) {
    static const int16_t xb_face[4] = {
        SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH
    };
    apply_pad_face_preset(PAD_LAYOUT_XBOX, xb_face);
}

bool bind_is_default(int dev) {
    return dev == BIND_DEV_PAD ? !memcmp(g_bind_pad, k_def_pad, sizeof g_bind_pad) : !memcmp(g_bind_key, k_def_key, sizeof g_bind_key);
}

bool bind_is_default_slot(int dev, int slot) {
    return dev == BIND_DEV_PAD ? g_bind_pad[slot] == k_def_pad[slot] : g_bind_key[slot] == k_def_key[slot];
}

int bind_prompt_host(int slot) { return slot >= 0 && slot < BIND_PAD_COUNT ? g_bind_pad[slot] : -1; }

// ------------------------------------------------------------------ capture
static _Atomic int cap_dev = -1, cap_slot = -1;
static _Atomic uint64_t cap_deadline, cap_ignore_until;
static _Atomic int res_kind, res_dev, res_slot, res_other, res_serial;

static void result(int kind, int dev, int slot, int other) {
    res_kind = kind, res_dev = dev, res_slot = slot, res_other = other;
    res_serial++;
}

static int *hotkey_ptr(int slot) {
    switch (slot) {
    case HOTKEY_MENU: return &g_settings.hotkey_menu;
    case HOTKEY_HUD: return &g_settings.hotkey_hud;
    case HOTKEY_SPEED: return &g_settings.hotkey_speed;
    case HOTKEY_FULLSCREEN: return &g_settings.hotkey_fullscreen;
    default: return NULL;
    }
}

int bind_hotkey_get(int slot) {
    int *p = hotkey_ptr(slot);
    return p ? *p : 0;
}

const char *bind_hotkey_name(int slot) {
    static const char *const k_hk[HOTKEY_COUNT] = {"呼出设置菜单", "切换性能 HUD", "切换快进倍速", "切换全屏"};
    return slot >= 0 && slot < HOTKEY_COUNT ? k_hk[slot] : "?";
}

bool bind_hotkeys_is_default(void) {
    return g_settings.hotkey_menu == SDL_SCANCODE_F1 && g_settings.hotkey_hud == SDL_SCANCODE_F2 &&
           g_settings.hotkey_speed == SDL_SCANCODE_F3 && g_settings.hotkey_fullscreen == SDL_SCANCODE_F11;
}

void bind_reset_hotkeys(void) {
    g_settings.hotkey_menu = SDL_SCANCODE_F1;
    g_settings.hotkey_hud = SDL_SCANCODE_F2;
    g_settings.hotkey_speed = SDL_SCANCODE_F3;
    g_settings.hotkey_fullscreen = SDL_SCANCODE_F11;
    LOG("[input] global hotkeys reset to defaults\n");
    result(5, BIND_DEV_HOTKEY, -1, -1);
}

void bind_capture_begin(int dev, int slot) {
    int max_s = dev == BIND_DEV_PAD ? BIND_PAD_COUNT : dev == BIND_DEV_KEY ? BIND_KEY_COUNT : HOTKEY_COUNT;
    if (dev < BIND_DEV_PAD || dev > BIND_DEV_HOTKEY || slot < 0 || slot >= max_s) return;
    uint64_t now = SDL_GetTicks();
    cap_ignore_until = now + 120;
    cap_deadline = now + BIND_CAPTURE_MS;
    cap_slot = slot;
    cap_dev = dev;
}

void bind_capture_cancel(void) {
    if (cap_dev < 0) return;
    result(3, cap_dev, cap_slot, -1);
    cap_dev = -1;
}

bool bind_capture_active(int *dev, int *slot, int *ms_left) {
    int d = cap_dev;
    if (d >= 0 && SDL_GetTicks() >= cap_deadline) { result(4, d, cap_slot, -1); cap_dev = d = -1; }
    if (dev) *dev = d;
    if (slot) *slot = d >= 0 ? cap_slot : -1;
    if (ms_left) *ms_left = d >= 0 ? (int)(cap_deadline - SDL_GetTicks()) : 0;
    return d >= 0;
}

int bind_last_result(int *dev, int *slot, int *other, int *serial) {
    if (dev) *dev = res_dev;
    if (slot) *slot = res_slot;
    if (other) *other = res_other;
    if (serial) *serial = res_serial;
    return res_kind;
}

// Bind `host` to `slot`; an input already on `host` takes the slot's old binding (swap).
static void assign(int dev, int slot, int host) {
    int16_t *t = dev == BIND_DEV_PAD ? g_bind_pad : g_bind_key;
    int n = dev == BIND_DEV_PAD ? BIND_PAD_COUNT : BIND_KEY_COUNT, other = -1;
    for (int i = 0; i < n; i++)
        if (i != slot && t[i] == host) { other = i; t[i] = t[slot]; }
    t[slot] = (int16_t)host;
    LOG("[input] %s %s -> %s%s%s%s\n", dev == BIND_DEV_PAD ? "controller" : "keyboard", k_names[slot],
        dev == BIND_DEV_PAD ? k_pad_ini[host] : bind_key_label(host), other >= 0 ? " (swapped with " : "",
        other >= 0 ? k_names[other] : "", other >= 0 ? ")" : "");
    if (other >= 0) LOG("[input]   %s -> %s\n", k_names[other], dev == BIND_DEV_PAD ? k_pad_ini[t[other]] : bind_key_label(t[other]));
    result(other >= 0 ? 2 : 1, dev, slot, other);
}

static int mouse_code_from_sdl_button(Uint8 btn) {
    switch (btn) {
    case SDL_BUTTON_LEFT: return KEY_MOUSE_LEFT;
    case SDL_BUTTON_RIGHT: return KEY_MOUSE_RIGHT;
    case SDL_BUTTON_MIDDLE: return KEY_MOUSE_MIDDLE;
    case SDL_BUTTON_X1: return KEY_MOUSE_X1;
    case SDL_BUTTON_X2: return KEY_MOUSE_X2;
    default: return 0;
    }
}

static int pad_host_from_axis(Uint8 axis, Sint16 value) {
    int pct = g_settings.trigger_threshold >= 10 && g_settings.trigger_threshold <= 90 ? g_settings.trigger_threshold : 50;
    int trig_thresh = pct * 32767 / 100;
    if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER && value > trig_thresh) return PADB_LTRIGGER;
    if (axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER && value > trig_thresh) return PADB_RTRIGGER;
    if (axis == SDL_GAMEPAD_AXIS_LEFTX) return value < -20000 ? PADB_LS_LEFT : value > 20000 ? PADB_LS_RIGHT : -1;
    if (axis == SDL_GAMEPAD_AXIS_LEFTY) return value < -20000 ? PADB_LS_UP : value > 20000 ? PADB_LS_DOWN : -1;
    if (axis == SDL_GAMEPAD_AXIS_RIGHTX) return value < -20000 ? PADB_RS_LEFT : value > 20000 ? PADB_RS_RIGHT : -1;
    if (axis == SDL_GAMEPAD_AXIS_RIGHTY) return value < -20000 ? PADB_RS_UP : value > 20000 ? PADB_RS_DOWN : -1;
    return -1;
}

bool bind_capture_event(const SDL_Event *e) {
    int dev = cap_dev;
    if (dev < 0) return false;
    bool ready = SDL_GetTicks() >= cap_ignore_until;
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
        if (e->key.repeat) return true;
        if (e->key.scancode == SDL_SCANCODE_ESCAPE) { bind_capture_cancel(); return true; }
        if (dev == BIND_DEV_HOTKEY) {
            if (e->key.scancode <= 0 || e->key.scancode >= SDL_SCANCODE_COUNT || e->key.scancode == SDL_SCANCODE_F9) return true;
            int slot = cap_slot, *dst = hotkey_ptr(slot), other = -1;
            if (!dst) { cap_dev = -1; return true; }
            for (int i = 0; i < HOTKEY_COUNT; i++) {
                int *p = hotkey_ptr(i);
                if (i != slot && p && *p == (int)e->key.scancode) { other = i; *p = *dst; }
            }
            *dst = (int)e->key.scancode;
            cap_dev = -1;
            result(other >= 0 ? 2 : 1, BIND_DEV_HOTKEY, slot, other);
            return true;
        }
        if (dev != BIND_DEV_KEY || bind_hotkey_reserved(e->key.scancode) || e->key.scancode >= SDL_SCANCODE_COUNT) return true;
        cap_dev = -1;
        assign(dev, cap_slot, e->key.scancode);
        return true;
    case SDL_EVENT_KEY_UP: case SDL_EVENT_TEXT_INPUT:
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        if (dev != BIND_DEV_KEY || !ready) return false;
        int code = mouse_code_from_sdl_button(e->button.button);
        if (!code) return false;
        cap_dev = -1;
        assign(dev, cap_slot, code);
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        if (dev != BIND_DEV_KEY || !ready || e->wheel.y == 0) return false;
        int code = e->wheel.y > 0 ? KEY_WHEEL_UP : KEY_WHEEL_DOWN;
        cap_dev = -1;
        assign(dev, cap_slot, code);
        return true;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        // Home / Guide opens the menu; a keyboard capture ignores the controller (it times out, or Esc)
        if (dev != BIND_DEV_PAD || e->gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE || e->gbutton.button >= SDL_GAMEPAD_BUTTON_COUNT) return true;
        cap_dev = -1;
        assign(dev, cap_slot, e->gbutton.button);
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        return true;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        int host = dev == BIND_DEV_PAD && ready ? pad_host_from_axis(e->gaxis.axis, e->gaxis.value) : -1;
        if (host >= 0) {
            cap_dev = -1;
            assign(dev, cap_slot, host);
        }
        return true;  // (sticks: no menu movement while waiting)
    }
    default:
        return false;
    }
}

// ------------------------------------------------------------------ larecomp.ini
bool bind_settings_load(const char *key, const char *val) {
    bool pad = !strncmp(key, "bind_pad_", 9), kb = !strncmp(key, "bind_key_", 9);
    if (!pad && !kb) return false;
    for (int i = 0; i < (pad ? BIND_PAD_COUNT : BIND_KEY_COUNT); i++) {
        if (strcmp(key + 9, k_ini[i])) continue;
        if (pad) {
            for (int h = 0; h < PADB_HOST_COUNT; h++)
                if (!strcmp(val, k_pad_ini[h])) g_bind_pad[i] = (int16_t)h;
        } else if (!strcmp(val, "none")) {
            g_bind_key[i] = 0;
        } else {
            int mc = mouse_code_from_name(val);
            if (mc) g_bind_key[i] = (int16_t)mc;
            else {
                SDL_Scancode sc = SDL_GetScancodeFromName(val);
                if (sc != SDL_SCANCODE_UNKNOWN) g_bind_key[i] = (int16_t)sc;
            }
        }
    }
    return true;
}

void bind_settings_save(FILE *f) {
    for (int i = 0; i < BIND_PAD_COUNT; i++)
        if (g_bind_pad[i] != k_def_pad[i] && g_bind_pad[i] >= 0 && g_bind_pad[i] < PADB_HOST_COUNT)
            fprintf(f, "bind_pad_%s = %s\n", k_ini[i], k_pad_ini[g_bind_pad[i]]);
    for (int i = 0; i < BIND_KEY_COUNT; i++)
        if (g_bind_key[i] != k_def_key[i])
            fprintf(f, "bind_key_%s = %s\n", k_ini[i], g_bind_key[i] ? bind_key_label(g_bind_key[i]) : "none");
}
