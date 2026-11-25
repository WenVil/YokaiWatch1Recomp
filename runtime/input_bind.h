// Control remapping (runtime/hle/bindings.c): which controller button / key drives each Switch input. Edited from
// the in-game menu's Controls tab and the start screen (runtime/ui/controls_ui.mm), saved in larecomp.ini.
// Menus (the overlay, the start screen) never use these, so no binding can lock the player out: they follow the
// game's layout, or the controller's own (Confirm button; runtime/ui/pad_nav.h).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef union SDL_Event SDL_Event;

// Switch inputs, in nn::hid button-bit order (runtime/hle/input.c BTN_*), followed by both sticks' 4 directions.
enum {
    BIND_A, BIND_B, BIND_X, BIND_Y, BIND_LSTICK, BIND_RSTICK, BIND_L, BIND_R, BIND_ZL, BIND_ZR, BIND_PLUS, BIND_MINUS,
    BIND_LEFT, BIND_UP, BIND_RIGHT, BIND_DOWN,
    BIND_BTN_COUNT,                                      // 16 digital Npad button bits
    BIND_LS_UP = BIND_BTN_COUNT, BIND_LS_DOWN, BIND_LS_LEFT, BIND_LS_RIGHT,
    BIND_RS_UP, BIND_RS_DOWN, BIND_RS_LEFT, BIND_RS_RIGHT,
    BIND_PAD_COUNT,                                      // 24 controller slots (buttons + triggers + stick directions)
    BIND_KEY_COUNT = BIND_PAD_COUNT                      // 24 keyboard+mouse slots (buttons + both sticks)
};
enum { BIND_DEV_PAD, BIND_DEV_KEY, BIND_DEV_HOTKEY };
enum { HOTKEY_MENU = 0, HOTKEY_HUD = 1, HOTKEY_SPEED = 2, HOTKEY_FULLSCREEN = 3, HOTKEY_COUNT = 4 };
// Controller host input values: SDL_GamepadButton (0..25), triggers (26..27), or stick 4-way directions (28..35)
enum {
    PADB_LTRIGGER = 26 /* SDL_GAMEPAD_BUTTON_COUNT */, PADB_RTRIGGER,
    PADB_LS_UP, PADB_LS_DOWN, PADB_LS_LEFT, PADB_LS_RIGHT,
    PADB_RS_UP, PADB_RS_DOWN, PADB_RS_LEFT, PADB_RS_RIGHT,
    PADB_HOST_COUNT
};
// Mouse pseudo-keycodes stored in g_bind_key[] (disjoint from SDL_Scancode 0..511)
enum {
    KEY_MOUSE_BASE = 1000,
    KEY_MOUSE_LEFT = KEY_MOUSE_BASE,
    KEY_MOUSE_RIGHT,
    KEY_MOUSE_MIDDLE,
    KEY_MOUSE_X1,
    KEY_MOUSE_X2,
    KEY_WHEEL_UP,
    KEY_WHEEL_DOWN,
    KEY_MOUSE_END
};

extern int16_t g_bind_pad[BIND_PAD_COUNT];  // PADB value per Switch input (analog sticks default to natural PADB_* axes)
extern int16_t g_bind_key[BIND_KEY_COUNT];  // SDL_Scancode or KEY_MOUSE_* / KEY_WHEEL_* per input (0 = none)

const char *bind_input_name(int slot);         // "A", "D-pad Up", "Left stick Up"
const char *bind_pad_label(int host, int fam); // controller button in the pad family's own words (fam: input_pad_family)
const char *bind_key_label(int code);
bool bind_is_mouse_code(int code);
bool bind_hotkey_reserved(int scancode);
int bind_label_family(void);                   // the connected controller's family (also on the start screen)
int input_family_of_type(int sdl_gamepad_type); // runtime/hle/input.c: 0 Nintendo, 1 Xbox (or labelled like one), 2 PS
int input_menu_family(uint32_t sdl_joystick_id); // runtime/hle/input.c: our menus' layout for that pad (0: any)
void bind_reset(int dev);
void bind_reset_hotkeys(void);
int bind_hotkey_get(int slot);
const char *bind_hotkey_name(int slot);
bool bind_hotkeys_is_default(void);
void bind_preset_pad_nintendo(void);
void bind_preset_pad_xbox(void);
bool bind_is_default(int dev);
bool bind_is_default_slot(int dev, int slot);

// "Click a binding, then press": begun from the UI; the next press (any thread's SDL event) is taken. Escape cancels,
// as does BIND_CAPTURE_MS without a press. On a conflict the two inputs swap.
#define BIND_CAPTURE_MS 5000
void bind_capture_begin(int dev, int slot);
void bind_capture_cancel(void);
// capturing: dev / slot (else -1), ms left
bool bind_capture_active(int *dev, int *slot, int *ms_left);
// Main thread, before anything else sees the event: true = consumed by a capture
bool bind_capture_event(const SDL_Event *e);
// Last change for the UI: 0 none, 1 set, 2 swapped (with *other), 3 cancelled, 4 timed out, 5 reset; *serial bumps
int bind_last_result(int *dev, int *slot, int *other, int *serial);

// larecomp.ini: bind_pad_<input> / bind_key_<input> (only the ones that differ from the defaults are written)
bool bind_settings_load(const char *key, const char *val);
void bind_settings_save(FILE *f);

// bind_* names (larecomp.ini): Switch input "a", "zl", "ls_up"... -> slot; controller button "south"... -> PADB value
int bind_slot_from_name(const char *name);
int bind_pad_from_name(const char *name);

// Button prompts (runtime/nvn/texpatch.c): the controller button now driving Switch input `slot` (BIND_A..)
int bind_prompt_host(int slot);

#ifdef __cplusplus
}
#endif
