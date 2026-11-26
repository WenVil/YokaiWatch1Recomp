// User settings (yokaiwatch1.ini in the data folder), edited from the launcher and in-game menu.
#pragma once
#include <stdbool.h>

enum { WIN_WINDOWED = 0, WIN_BORDERLESS = 1, WIN_EXCLUSIVE = 2 };
enum { OUT_RES_AUTO = 0, OUT_RES_720P = 1, OUT_RES_1080P = 2, OUT_RES_1440P = 3, OUT_RES_4K = 4 };
enum { RES_SCALE_1X = 0, RES_SCALE_1_5X = 1, RES_SCALE_2X = 2 };
enum { ASPECT_MODE_LOCK_16_9 = 0, ASPECT_MODE_STRETCH = 1 };
enum { VSYNC_MAILBOX = 0, VSYNC_FIFO = 1, VSYNC_IMMEDIATE = 2 };
enum { HUD_OFF = 0, HUD_FPS = 1, HUD_DETAILED = 2 };
enum { AUDIO_CH_AUTO = 0, AUDIO_CH_STEREO = 1, AUDIO_CH_5_1 = 2 };
enum { PAD_LAYOUT_NINTENDO = 0, PAD_LAYOUT_XBOX = 1 };
enum { UI_LANG_AUTO = 0, UI_LANG_ZH_HANS = 1, UI_LANG_ZH_HANT = 2, UI_LANG_EN = 3, UI_LANG_JA = 4, UI_LANG_KO = 5, N_UI_LANGS = 6 };
enum {
    GAME_LANG_AUTO = 0,
    GAME_LANG_ZH_HANS = 1,
    GAME_LANG_ZH_HANT = 2,
    GAME_LANG_KO = 3,
    GAME_LANG_JA = 4,
    GAME_LANG_EN_US = 5,
    GAME_LANG_FR = 6,
    GAME_LANG_DE = 7,
    GAME_LANG_IT = 8,
    GAME_LANG_ES = 9,
    N_GAME_LANGS = 10
};
enum { PROMPTS_AUTO = 0, PROMPTS_NINTENDO = 1, PROMPTS_XBOX = 2, PROMPTS_PLAYSTATION = 3 };
// Which face button confirms in our own menus (runtime/ui/pad_nav.mm); the game's input is never affected
enum { MENU_CONFIRM_GAME = 0, MENU_CONFIRM_CONTROLLER = 1 };
// System language codes served to the game
enum { LANG_AUTO = 0, N_LANGUAGES = 12 };
extern const char *const g_language_codes[N_LANGUAGES];
extern const char *const g_language_names[N_LANGUAGES];

typedef struct {
    bool fullscreen;
    float volume;        // master volume, 1 = 100%
    bool reverb;         // room reverb effect
    bool movie_audio;
    bool show_fps;
    bool dpad_move;      // d-pad / arrow keys mirror the left stick when it rests
    bool rumble;         // controller vibration from the game (runtime/hle/input.c)
    int rumble_strength; // intensity, percent (25 / 50 / 75 / 100)
    bool controller_light; // controller LED color
    bool bg_input;       // controllers keep working while unfocused
    int language;        // legacy language code override
    int prompts;         // PROMPTS_*
    int menu_confirm;    // MENU_CONFIRM_*
    bool aspect_asked;
    bool menu_tip_seen;
    bool onedrive_kept;
    // Display, audio, input, launcher & global hotkey settings
    int window_mode;         // WIN_* (0=windowed, 1=borderless fullscreen, 2=exclusive fullscreen)
    int output_res;          // OUT_RES_* (0=auto, 1=720p, 2=1080p, 3=1440p, 4=4k)
    int res_scale;           // RES_SCALE_* (0=1.0x/1080p, 1=1.5x/1620p, 2=2.0x/4k)
    int aspect_mode;         // ASPECT_MODE_* (0=lock 16:9, 1=stretch)
    int vsync_mode;          // VSYNC_* (0=adaptive/mailbox, 1=vsync on/fifo, 2=vsync off/immediate)
    int hud_mode;            // HUD_* (0=off, 1=fps, 2=detailed)
    int speed_pct;           // Fast-forward speed in percent (100/150/200/300)
    bool mods_enabled;       // LayeredFS overlay (<data_root>/mods/<name>/romfs)
    bool mute_unfocused;     // mute audio while the window is not focused
    int audio_channels;      // AUDIO_CH_* (0=auto, 1=stereo, 2=5.1)
    int ls_deadzone;         // left stick radial deadzone, percent (0..40, default 12)
    int rs_deadzone;         // right stick radial deadzone, percent (0..40, default 12)
    int trigger_threshold;   // analog trigger press threshold, percent (10..90, default 50)
    int pad_layout;          // PAD_LAYOUT_* (0=nintendo physical, 1=xbox letter)
    int ui_lang;             // UI_LANG_* (0=auto, 1=zh-Hans, 2=zh-Hant, 3=en, 4=ja, 5=ko)
    int game_lang;           // GAME_LANG_* (0=auto, 1..9: zh-Hans/zh-Hant/ko/ja/en-US/fr/de/it/es)
    char game_dir[1024];     // UTF-8 path to the user's game folder
    bool skip_launcher;      // skip the launcher when ready on startup
    int hotkey_menu;         // SDL_Scancode to toggle in-game settings (default F1)
    int hotkey_hud;          // SDL_Scancode to toggle/cycle HUD overlay (default F2)
    int hotkey_speed;        // SDL_Scancode to cycle fast-forward speed (default F3)
    int hotkey_fullscreen;   // SDL_Scancode to toggle fullscreen (default F11)
} Settings;

extern Settings g_settings;

void settings_load(void);
void settings_save(void);
// The language code to serve the game ("es", "en-US", ...)
const char *settings_language(void);
const char *settings_language_name(void);
// Resolved UI language index (returns 1..5: UI_LANG_ZH_HANS .. UI_LANG_KO).
int settings_resolved_ui_lang(void);
// Resolved Switch game language code (honors LARECOMP_LANG -> game_lang).
const char *settings_game_lang_code(void);
