#include "settings.h"

#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "game_addresses.h"
#include "hle.h"
#include "input_bind.h"

#define SETTINGS_FILE settings_file()

// LARECOMP_SETTINGS=<path>: another settings file (test runs never touch user config).
// Otherwise writes 13sentinels.ini, and falls back to reading legacy larecomp.ini if present.
static const char *settings_file(void) {
    const char *e = getenv("LARECOMP_SETTINGS");
    return e && *e ? e : "13sentinels.ini";
}

static const char *settings_read_file(void) {
    const char *e = getenv("LARECOMP_SETTINGS");
    return (e && *e) ? e : "13sentinels.ini";
}

static FILE *open_utf8(const char *path, const char *mode) {
#ifdef _WIN32
    wchar_t wp[1100], wm[8];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wp, 1100) > 0 &&
        MultiByteToWideChar(CP_UTF8, 0, mode, -1, wm, 8) > 0)
        return _wfopen(wp, wm);
#endif
    return fopen(path, mode);
}

const char *const g_enemy_colour_modes[N_ENEMY_COLOUR_MODES] = {"Off", "Random per seed", "Random every load"};
static const char *const k_enemy_colour_keys[N_ENEMY_COLOUR_MODES] = {"off", "seed", "load"};  // (larecomp.ini)
const char *const g_screen_filters[N_SCREEN_FILTERS] = {"Off", "Game Boy", "Game Boy Color", "Grayscale"};
static const char *const k_screen_filter_keys[N_SCREEN_FILTERS] = {"off", "gameboy", "gameboy_color", "grayscale"};  // (else: off)

Settings g_settings = {
    .aspect = ASPECT_UNLOCKED,
    .fullscreen = false,
    .volume = 1.0f,
    .reverb = true,
    .movie_audio = true,
    .rumble = true,
    .rumble_strength = 100,
    .controller_light = true,
    .menu_confirm = MENU_CONFIRM_CONTROLLER,
    .show_fps = false,
    .inv_keys = true,
    .tracker = true,
    .tracker_filters = -1,
    .chaos_letters = CHAOS_LETTERS_MAP,
    .diff_damage = 1.0f,
    .walk_speed = 1.0f,
    .walk_speed_dungeons = false,
    .upscaler = UPSCALE_SHARP,  // (MetalFX on macOS, AMD FSR 1 on Windows)
    .upscaler_auto = true,
    .res_auto = true,
    .livesplit_host = "localhost:16834",
#ifndef __APPLE__
    .bg_input = true,  // (as before the setting; on macOS it needs Input Monitoring permission, so Off there)
#endif
    .window_mode = WIN_WINDOWED,
    .output_res = OUT_RES_AUTO,
    .aspect_mode = ASPECT_MODE_LOCK_16_9,
    .vsync_mode = VSYNC_MAILBOX,
    .hud_mode = HUD_OFF,
    .mute_unfocused = false,
    .audio_channels = AUDIO_CH_AUTO,
    .ls_deadzone = 12,
    .rs_deadzone = 12,
    .trigger_threshold = 50,
    .pad_layout = PAD_LAYOUT_NINTENDO,
    .ui_lang = UI_LANG_AUTO,
    .game_lang = GAME_LANG_ZH_HANS,
    .game_dir = "",
    .skip_launcher = false,
    .vk_wait_pipes = true,
    .vk_copy_vertices = true,
    .vk_gpu_overlap = true,
    .hotkey_menu = SDL_SCANCODE_F1,
    .hotkey_hud = SDL_SCANCODE_F2,
    .hotkey_speed = SDL_SCANCODE_F3,
    .hotkey_fullscreen = SDL_SCANCODE_F11,
};

// The game's own text languages: its RomFS carries these per region (regionUS/USen|USes|USfr, regionEU/EUen|EUes|...,
// regionJP/JPja, regionKR/KRkr, regionTW/TWch, regionCN/CNch), and the game picks one from the system language it
// reads back (runtime/services/am.c), exactly as it does on the console. Codes as the Switch reports them, except for
// the Chinese ones: the game only recognizes zh-Hans / zh-Hant and falls back to Japanese when told zh-CN / zh-TW.
const char *const g_language_codes[N_LANGUAGES] = {"auto", "ja", "en-US", "es", "fr", "de", "it", "nl", "ru", "ko",
                                                   "zh-Hans", "zh-Hant"};
const char *const g_language_names[N_LANGUAGES] = {"Auto", "Japanese", "English", "Spanish", "French", "German",
                                                   "Italian", "Dutch", "Russian", "Korean", "Chinese (Simplified)",
                                                   "Chinese (Traditional)"};

// LANG_AUTO: the host's own language (LANG=es_ES.UTF-8 -> es), en-US when it is none of the game's. Linux only:
// macOS / Windows apps get no locale variables from the system, so Auto is English there (pick one in settings).
static const char *host_language(void) {
#ifndef __linux__
    return "en-US";
#endif
    const char *e = getenv("LC_ALL");
    if (!e || !*e) e = getenv("LC_MESSAGES");
    if (!e || !*e) e = getenv("LANG");
    if (!e || !*e || !strcmp(e, "C") || !strcmp(e, "POSIX")) return "en-US";
    if (!strncmp(e, "zh", 2) && (!e[2] || strchr("_-.@", e[2]))) {
        char locale[64];
        snprintf(locale, sizeof locale, "%s", e);
        locale[strcspn(locale, ".@")] = 0;
        for (char *p = locale; *p; ++p) *p = (char)tolower((unsigned char)*p);
        bool traditional = false;
        char *next;
        for (char *part = strtok_r(locale, "_-", &next); part; part = strtok_r(NULL, "_-", &next)) {
            // An explicit script takes precedence over the region, e.g. zh-Hans-HK.
            if (!strcmp(part, "hant")) return "zh-Hant";
            if (!strcmp(part, "hans")) return "zh-Hans";
            if (!strcmp(part, "tw") || !strcmp(part, "hk") || !strcmp(part, "mo")) traditional = true;
        }
        return traditional ? "zh-Hant" : "zh-Hans";
    }
    char l[8] = {0};
    size_t n = 0;
    for (const char *p = e; *p && *p != '.' && *p != '@' && *p != '_' && n < sizeof l - 1; p++) l[n++] = *p;
    static const struct { const char *loc, *code; } map[] = {{"ja", "ja"}, {"ko", "ko"}, {"ru", "ru"}, {"nl", "nl"},
                                                             {"de", "de"}, {"it", "it"}, {"fr", "fr"}, {"es", "es"}};
    for (size_t i = 0; i < sizeof map / sizeof *map; i++)
        if (!strcmp(l, map[i].loc)) return map[i].code;
    return "en-US";
}

static const char *normalize_lang_code(const char *val) {
    if (!val || !*val) return NULL;
    if (!strcmp(val, "zh-CN") || !strcmp(val, "zh_CN") || !strcmp(val, "zh-Hans")) return "zh-Hans";
    if (!strcmp(val, "zh-TW") || !strcmp(val, "zh_TW") || !strcmp(val, "zh-Hant")) return "zh-Hant";
    if (!strcmp(val, "en")) return "en-US";
    if (!strcmp(val, "fr-FR") || !strcmp(val, "fr-CA")) return "fr";
    if (!strcmp(val, "ge") || !strcmp(val, "de-DE")) return "de";
    if (!strcmp(val, "it-IT")) return "it";
    if (!strcmp(val, "sp") || !strcmp(val, "es-ES") || !strcmp(val, "es-419")) return "es";
    return val;
}

static int host_ui_lang(void) {
#ifdef _WIN32
    LANGID id = GetUserDefaultUILanguage();
    WORD pri = PRIMARYLANGID(id);
    WORD sub = SUBLANGID(id);
    if (pri == LANG_CHINESE) {
        if (sub == SUBLANG_CHINESE_TRADITIONAL || sub == SUBLANG_CHINESE_HONGKONG || sub == SUBLANG_CHINESE_MACAU)
            return UI_LANG_ZH_HANT;
        return UI_LANG_ZH_HANS;
    }
    if (pri == LANG_JAPANESE) return UI_LANG_JA;
    if (pri == LANG_KOREAN) return UI_LANG_KO;
    if (pri == LANG_ENGLISH) return UI_LANG_EN;
    return UI_LANG_EN;
#else
    const char *e = getenv("LC_ALL");
    if (!e || !*e) e = getenv("LC_MESSAGES");
    if (!e || !*e) e = getenv("LANG");
    if (!e || !*e) return UI_LANG_EN;
    if (!strncmp(e, "zh", 2)) {
        if (strstr(e, "TW") || strstr(e, "HK") || strstr(e, "MO") || strstr(e, "Hant")) return UI_LANG_ZH_HANT;
        return UI_LANG_ZH_HANS;
    }
    if (!strncmp(e, "ja", 2)) return UI_LANG_JA;
    if (!strncmp(e, "ko", 2)) return UI_LANG_KO;
    return UI_LANG_EN;
#endif
}

int settings_resolved_ui_lang(void) {
    if (g_settings.ui_lang >= UI_LANG_ZH_HANS && g_settings.ui_lang < N_UI_LANGS) return g_settings.ui_lang;
    return host_ui_lang();
}

const char *settings_game_lang_code(void) {
    const char *env = getenv("LARECOMP_LANG");
    if (env && *env) return normalize_lang_code(env);
    switch (g_settings.game_lang) {
    case GAME_LANG_ZH_HANS: return "zh-Hans";
    case GAME_LANG_ZH_HANT: return "zh-Hant";
    case GAME_LANG_KO: return "ko";
    case GAME_LANG_JA: return "ja";
    case GAME_LANG_EN_US: return "en-US";
    case GAME_LANG_FR: return "fr";
    case GAME_LANG_DE: return "de";
    case GAME_LANG_IT: return "it";
    case GAME_LANG_ES: return "es";
    default: break;
    }
    const GameVersion *gv = game_version();
    if (gv && (strncmp(gv->key, "v13_us", 6) == 0 || strncmp(gv->key, "v13_eu", 6) == 0)) {
        return "en-US";
    }
    switch (settings_resolved_ui_lang()) {
    case UI_LANG_ZH_HANT: return "zh-Hant";
    case UI_LANG_KO: return "ko";
    case UI_LANG_JA: return "ja";
    case UI_LANG_EN: return "en-US";
    default: return "zh-Hans";
    }
}

const char *settings_language(void) {
    const char *env = getenv("LARECOMP_LANG");
    if (env && *env) return normalize_lang_code(env);
    // For non-v13 games (Zelda) honor explicit legacy `language` choice; otherwise prefer 13S `game_lang`.
    const GameVersion *gv = game_version();
    if (gv && strncmp(gv->key, "v13_", 4) != 0 && g_settings.language > LANG_AUTO && g_settings.language < N_LANGUAGES)
        return g_language_codes[g_settings.language];
    return settings_game_lang_code();
}

const char *settings_language_name(void) {
    const char *c = settings_language();
    for (int i = 0; i < N_LANGUAGES; i++)
        if (!strcmp(c, g_language_codes[i])) return g_language_names[i];
    return "English";
}

int g_default_upscaler = UPSCALE_SHARP;
int g_default_res_mode = RES_MATCH;
bool g_gpu_integrated;

static const char *const k_window_modes[] = {"windowed", "borderless", "exclusive"};
static const char *const k_output_res[] = {"auto", "720p", "1080p", "1440p", "4k"};
static const char *const k_aspect_modes[] = {"16:9", "stretch"};
static const char *const k_vsync_modes[] = {"mailbox", "fifo", "immediate"};
static const char *const k_hud_modes[] = {"off", "fps", "detailed"};
static const char *const k_audio_channels[] = {"auto", "stereo", "5.1"};
static const char *const k_pad_layouts[] = {"nintendo", "xbox"};
static const char *const k_ui_langs[] = {"auto", "zh-Hans", "zh-Hant", "en", "ja", "ko"};
static const char *const k_game_langs[] = {"auto", "zh-Hans", "zh-Hant", "ko", "ja", "en-US", "fr", "de", "it", "es"};

static int parse_choice(const char *val, const char *const *names, int count, int fallback) {
    for (int i = 0; i < count; i++)
        if (!strcmp(val, names[i])) return i;
    if (isdigit((unsigned char)val[0]) && !val[1]) {
        int idx = val[0] - '0';
        if (idx >= 0 && idx < count) return idx;
    }
    return fallback;
}

static int parse_hotkey(const char *val, int fallback) {
    if (!val || !*val) return fallback;
    if (!strcmp(val, "none")) return 0;
    SDL_Scancode sc = SDL_GetScancodeFromName(val);
    return sc != SDL_SCANCODE_UNKNOWN ? (int)sc : fallback;
}

static int clamp_int(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static const char *res_name(int m) {
    static char n[8];
    if (m == RES_1_5X) return "1.5x";
    if (m >= 1 && m <= 8) { snprintf(n, sizeof n, "%dx", m); return n; }
    return m == RES_DYNAMIC ? "dynamic" : m == RES_Q75 ? "75%" : m == RES_Q50 ? "50%" : m == RES_Q25 ? "25%" : "match";
}

void settings_resolve_defaults(bool integrated) {
    const char *e = getenv("LARECOMP_INTEGRATED");
    if (e && *e) integrated = atoi(e) != 0;
    g_gpu_integrated = integrated;
    g_default_upscaler = integrated ? UPSCALE_BILINEAR : UPSCALE_SHARP;
    g_default_res_mode = RES_MATCH;  // (Dynamic is not a default until it is reliable: user 2026-10-18)
    if (g_settings.upscaler_auto) g_settings.upscaler = g_default_upscaler;
    if (g_settings.res_auto) g_settings.res_mode = g_default_res_mode;
    LOG("[settings] %s GPU: upscaler %s (%s), resolution %s (%s), anti-aliasing %s\n", integrated ? "integrated" : "discrete / other",
        g_settings.upscaler == UPSCALE_BILINEAR ? "bilinear" : g_settings.upscaler == UPSCALE_FSR1 ? "fsr1" : "metalfx",
        g_settings.upscaler_auto ? "default" : "chosen", res_name(g_settings.res_mode), g_settings.res_auto ? "default" : "chosen",
        g_settings.antialias == AA_SMAA ? "smaa" : "off");
}

void settings_load(void) {
    static bool loaded;
    if (loaded) return;
    loaded = true;
    const char *src = settings_read_file();
    FILE *f = open_utf8(src, "r");
    if (!f && !getenv("LARECOMP_SETTINGS")) {
        FILE *legacy = open_utf8("larecomp.ini", "r");
        if (legacy) {
            f = legacy;
            src = "larecomp.ini";
        }
    }
    if (!f) return;
    char line[2048];
    bool tip_key = false, aspect_key = false;
    bool seen_win_mode = false, seen_fs = false, seen_hud_mode = false, seen_show_fps = false;
    bool seen_pad_layout = false, seen_pad_face = false;
    while (fgets(line, sizeof line, f)) {
        if ((unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
            memmove(line, line + 3, strlen(line + 3) + 1);
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0] || line[0] == '#' || line[0] == ';') continue;
        char key[64];
        char val[1024] = {0};
        int fields = sscanf(line, " %63[^= ] = %1023[^\n]", key, val);
        if (fields < 1) continue;
        if (!strcmp(key, "rando_seed")) {
            strlcpy(g_settings.rando_seed, val, sizeof g_settings.rando_seed);
            continue;
        }
        if (!strcmp(key, "game_dir")) {
            strlcpy(g_settings.game_dir, val, sizeof g_settings.game_dir);
            continue;
        }
        if (fields != 2) continue;
        if (!strcmp(key, "aspect")) g_settings.aspect = !strcmp(val, "original") ? ASPECT_ORIGINAL : ASPECT_UNLOCKED;
        else if (!strcmp(key, "fullscreen")) { g_settings.fullscreen = atoi(val) != 0; seen_fs = true; }
        else if (!strcmp(key, "window_mode")) { g_settings.window_mode = parse_choice(val, k_window_modes, 3, WIN_WINDOWED); seen_win_mode = true; }
        else if (!strcmp(key, "output_res")) g_settings.output_res = parse_choice(val, k_output_res, 5, OUT_RES_AUTO);
        else if (!strcmp(key, "aspect_mode")) g_settings.aspect_mode = parse_choice(val, k_aspect_modes, 2, ASPECT_MODE_LOCK_16_9);
        else if (!strcmp(key, "vsync_mode")) g_settings.vsync_mode = parse_choice(val, k_vsync_modes, 3, VSYNC_MAILBOX);
        else if (!strcmp(key, "hud_mode")) { g_settings.hud_mode = parse_choice(val, k_hud_modes, 3, HUD_OFF); seen_hud_mode = true; }
        else if (!strcmp(key, "volume")) g_settings.volume = (float)atof(val);
        else if (!strcmp(key, "reverb")) g_settings.reverb = atoi(val) != 0;
        else if (!strcmp(key, "movie_audio")) g_settings.movie_audio = atoi(val) != 0;
        else if (!strcmp(key, "mute_unfocused")) g_settings.mute_unfocused = atoi(val) != 0;
        else if (!strcmp(key, "audio_channels")) g_settings.audio_channels = parse_choice(val, k_audio_channels, 3, AUDIO_CH_AUTO);
        else if (!strcmp(key, "show_fps")) { g_settings.show_fps = atoi(val) != 0; seen_show_fps = true; }
        else if (!strcmp(key, "resolution")) {  // (auto: never chosen, the GPU's default)
            int n = atoi(val);
            g_settings.res_auto = !strcmp(val, "auto");
            if (g_settings.res_auto) { g_settings.res_mode = g_default_res_mode; continue; }
            g_settings.res_mode = !strcmp(val, "dynamic") ? RES_DYNAMIC : (!strcmp(val, "75%") || !strcmp(val, "67%")) ? RES_Q75 :
                                  !strcmp(val, "50%") ? RES_Q50 : !strcmp(val, "25%") ? RES_Q25 :
                                  !strcmp(val, "1.5x") ? RES_1_5X :
                                  (n >= 1 && n <= 4) ? n : n > 4 ? 4 : RES_MATCH;  // (5x-8x were removed)
        }
        else if (!strcmp(key, "upscaler")) {  // (metalfx / fsr1: this OS's; auto: never chosen, the GPU's default)
            g_settings.upscaler_auto = !strcmp(val, "auto");
            if (!g_settings.upscaler_auto) g_settings.upscaler = !strcmp(val, "bilinear") ? UPSCALE_BILINEAR : UPSCALE_SHARP;
            else g_settings.upscaler = g_default_upscaler;
        }
        else if (!strcmp(key, "antialias")) g_settings.antialias = !strcmp(val, "off") ? AA_OFF : AA_SMAA;  // ("fxaa": SMAA now)
        else if (!strcmp(key, "inv_keys")) g_settings.inv_keys = atoi(val) != 0;
        else if (!strcmp(key, "dpad_move")) g_settings.dpad_move = atoi(val) != 0;
        else if (!strcmp(key, "rumble")) g_settings.rumble = atoi(val) != 0;
        else if (!strcmp(key, "rumble_strength")) g_settings.rumble_strength = atoi(val);
        else if (!strcmp(key, "controller_light")) g_settings.controller_light = atoi(val) != 0;
        else if (!strcmp(key, "background_input")) g_settings.bg_input = atoi(val) != 0;
        else if (!strcmp(key, "move_360")) g_settings.move_360 = atoi(val) != 0;
        else if (!strcmp(key, "blur_removal")) g_settings.blur_removal = atoi(val) != 0;
        else if (!strcmp(key, "hide_hero_icon")) g_settings.hide_hero_icon = atoi(val) != 0;
        else if (!strcmp(key, "mirror_mode")) g_settings.mirror_mode = atoi(val) != 0;
        else if (!strcmp(key, "prompts")) g_settings.prompts = !strcmp(val, "nintendo") ? PROMPTS_NINTENDO : !strcmp(val, "xbox") ?
                                                              PROMPTS_XBOX : !strcmp(val, "playstation") ? PROMPTS_PLAYSTATION : PROMPTS_AUTO;
        else if (!strcmp(key, "menu_confirm")) g_settings.menu_confirm = !strcmp(val, "game") ? MENU_CONFIRM_GAME
                                                                                                  : MENU_CONFIRM_CONTROLLER;
        else if (!strcmp(key, "language")) {  // ("auto" or one of the game's codes; anything else: auto)
            // (zh-CN / zh-TW: what a settings file from before this wrote; the game reads zh-Hans / zh-Hant)
            const char *v = !strcmp(val, "zh-CN") ? "zh-Hans" : !strcmp(val, "zh-TW") ? "zh-Hant" : val;
            g_settings.language = LANG_AUTO;
            for (int i = 1; i < N_LANGUAGES; i++)
                if (!strcmp(v, g_language_codes[i])) g_settings.language = i;
        }
        else if (!strcmp(key, "tracker")) g_settings.tracker = atoi(val) != 0;
        else if (!strcmp(key, "tracker_filters")) g_settings.tracker_filters = atoi(val);
        else if (!strcmp(key, "menu_tip_seen")) g_settings.menu_tip_seen = atoi(val) != 0, tip_key = true;
        else if (!strcmp(key, "aspect_asked")) g_settings.aspect_asked = atoi(val) != 0, aspect_key = true;
        else if (!strcmp(key, "onedrive_kept")) g_settings.onedrive_kept = atoi(val) != 0;
        else if (!strcmp(key, "chaos_letters"))
            g_settings.chaos_letters = !strcmp(val, "off") ? CHAOS_LETTERS_OFF : !strcmp(val, "explored") ? CHAOS_LETTERS_EXPLORED : CHAOS_LETTERS_MAP;
        else if (!strcmp(key, "cheat_hearts")) g_settings.cheat_hearts = atoi(val) != 0;
        else if (!strcmp(key, "cheat_ammo")) g_settings.cheat_ammo = atoi(val) != 0;
        else if (!strcmp(key, "cheat_rupees")) g_settings.cheat_rupees = atoi(val) != 0;
        else if (!strcmp(key, "cheat_hookshot")) g_settings.cheat_hookshot = atoi(val) != 0;
        else if (!strcmp(key, "no_buff_drops")) g_settings.no_buff_drops = atoi(val) != 0;
        else if (!strcmp(key, "diff_damage")) g_settings.diff_damage = (float)atof(val);
        else if (!strcmp(key, "walk_speed")) g_settings.walk_speed = (float)atof(val);
        else if (!strcmp(key, "walk_speed_dungeons")) g_settings.walk_speed_dungeons = atoi(val) != 0;
        else if (!strcmp(key, "diff_ohko")) g_settings.diff_ohko = atoi(val) != 0;
        else if (!strcmp(key, "diff_hero")) g_settings.diff_hero = atoi(val) != 0;
        else if (!strcmp(key, "timer")) g_settings.timer = atoi(val) != 0;
        else if (!strcmp(key, "timer_position")) g_settings.timer_pos = !strcmp(val, "right") ? TIMER_POS_RIGHT : TIMER_POS_LEFT;
        else if (!strcmp(key, "livesplit")) g_settings.livesplit = atoi(val) != 0;
        else if (!strcmp(key, "rando_options")) strlcpy(g_settings.rando_options, val, sizeof g_settings.rando_options);
        else if (!strcmp(key, "livesplit_host")) strlcpy(g_settings.livesplit_host, val, sizeof g_settings.livesplit_host);
        else if (!strcmp(key, "tunic")) col_parse(&g_settings.tunic, val);  // (colour rows: runtime/cosmetic_colour.c)
        else if (!strcmp(key, "hearts")) col_parse(&g_settings.hearts, val);
        else if (!strcmp(key, "hair")) col_parse(&g_settings.hair, val);
        else if (!strcmp(key, "shield")) col_parse(&g_settings.shield, val);
        else if (!strcmp(key, "skin")) col_parse(&g_settings.skin, val);
        else if (!strcmp(key, "leaves")) col_parse(&g_settings.leaves, val);
        else if (!strcmp(key, "grass")) col_parse(&g_settings.grass, val);
        else if (!strcmp(key, "sand")) col_parse(&g_settings.sand, val);
        else if (!strcmp(key, "screen_filter")) {
            for (int i = 0; i < N_SCREEN_FILTERS; i++) if (!strcmp(val, k_screen_filter_keys[i])) g_settings.screen_filter = i;
        }
        else if (!strcmp(key, "ls_deadzone")) g_settings.ls_deadzone = clamp_int(atoi(val), 0, 40);
        else if (!strcmp(key, "rs_deadzone")) g_settings.rs_deadzone = clamp_int(atoi(val), 0, 40);
        else if (!strcmp(key, "trigger_threshold")) g_settings.trigger_threshold = clamp_int(atoi(val), 10, 90);
        else if (!strcmp(key, "pad_layout")) { g_settings.pad_layout = parse_choice(val, k_pad_layouts, 2, PAD_LAYOUT_NINTENDO); seen_pad_layout = true; }
        else if (!strcmp(key, "ui_lang")) {
            const char *v = normalize_lang_code(val);
            if (v && !strcmp(v, "en-US")) v = "en";
            g_settings.ui_lang = parse_choice(v ? v : val, k_ui_langs, N_UI_LANGS, UI_LANG_AUTO);
        }
        else if (!strcmp(key, "game_lang")) {
            const char *v = normalize_lang_code(val);
            g_settings.game_lang = parse_choice(v ? v : val, k_game_langs, N_GAME_LANGS, GAME_LANG_ZH_HANS);
        }
        else if (!strcmp(key, "skip_launcher")) g_settings.skip_launcher = atoi(val) != 0;
        else if (!strcmp(key, "vk_wait_pipes")) g_settings.vk_wait_pipes = atoi(val) != 0;
        else if (!strcmp(key, "vk_copy_vertices")) g_settings.vk_copy_vertices = atoi(val) != 0;
        else if (!strcmp(key, "vk_gpu_overlap")) g_settings.vk_gpu_overlap = atoi(val) != 0;
        else if (!strcmp(key, "hotkey_menu")) g_settings.hotkey_menu = parse_hotkey(val, SDL_SCANCODE_F1);
        else if (!strcmp(key, "hotkey_hud")) g_settings.hotkey_hud = parse_hotkey(val, SDL_SCANCODE_F2);
        else if (!strcmp(key, "hotkey_speed")) g_settings.hotkey_speed = parse_hotkey(val, SDL_SCANCODE_F3);
        else if (!strcmp(key, "hotkey_fullscreen")) g_settings.hotkey_fullscreen = parse_hotkey(val, SDL_SCANCODE_F11);
        else if (!strcmp(key, "enemy_colours")) {
            for (int i = 0; i < N_ENEMY_COLOUR_MODES; i++) if (!strcmp(val, k_enemy_colour_keys[i])) g_settings.enemy_colours = i;
        }
        else {
            if (!strcmp(key, "bind_pad_a") || !strcmp(key, "bind_pad_b") || !strcmp(key, "bind_pad_x") || !strcmp(key, "bind_pad_y"))
                seen_pad_face = true;
            bind_settings_load(key, val);  // bind_pad_* / bind_key_*: runtime/hle/bindings.c
        }
    }
    fclose(f);
    if (!tip_key) g_settings.menu_tip_seen = true;  // (a settings file from before the note: not a first launch)
    if (!aspect_key) g_settings.aspect_asked = true;  // (likewise: the aspect ratio was chosen already)
    if (seen_win_mode) g_settings.fullscreen = (g_settings.window_mode != WIN_WINDOWED);
    else if (seen_fs && g_settings.fullscreen) g_settings.window_mode = WIN_BORDERLESS;
    if (seen_hud_mode) g_settings.show_fps = (g_settings.hud_mode != HUD_OFF);
    else if (seen_show_fps && g_settings.show_fps) g_settings.hud_mode = HUD_FPS;
    if (seen_pad_layout && !seen_pad_face) {
        if (g_settings.pad_layout == PAD_LAYOUT_XBOX) bind_preset_pad_xbox();
        else bind_preset_pad_nintendo();
    }
    if (g_settings.volume < 0) g_settings.volume = 0;
    if (g_settings.volume > 2) g_settings.volume = 2;
    if (g_settings.rumble_strength < 25 || g_settings.rumble_strength > 100) g_settings.rumble_strength = 100;
    LOG("[settings] loaded %s\n", src);
}

static void save_hotkey(FILE *f, const char *key, int sc) {
    const char *nm = sc > 0 && sc < SDL_SCANCODE_COUNT ? SDL_GetScancodeName((SDL_Scancode)sc) : NULL;
    fprintf(f, "%s = %s\n", key, nm && *nm ? nm : "none");
}

void settings_save(void) {
    FILE *f = open_utf8(SETTINGS_FILE, "w");
    if (!f) { LOG("[settings] cannot write %s\n", SETTINGS_FILE); return; }
    fprintf(f, "# 13SentinelsRecomp settings (edited from the in-game menu: F1, or Minus+Plus on a controller)\n");
    fprintf(f, "aspect = %s\n", g_settings.aspect == ASPECT_ORIGINAL ? "original" : "unlocked");
    fprintf(f, "window_mode = %s\n", k_window_modes[clamp_int(g_settings.window_mode, 0, 2)]);
    fprintf(f, "fullscreen = %d\n", g_settings.window_mode != WIN_WINDOWED ? 1 : 0);
    fprintf(f, "output_res = %s\n", k_output_res[clamp_int(g_settings.output_res, 0, 4)]);
    fprintf(f, "aspect_mode = %s\n", k_aspect_modes[clamp_int(g_settings.aspect_mode, 0, 1)]);
    fprintf(f, "vsync_mode = %s\n", k_vsync_modes[clamp_int(g_settings.vsync_mode, 0, 2)]);
    fprintf(f, "hud_mode = %s\n", k_hud_modes[clamp_int(g_settings.hud_mode, 0, 2)]);
    fprintf(f, "volume = %.2f\n", g_settings.volume);
    fprintf(f, "reverb = %d\n", g_settings.reverb);
    fprintf(f, "movie_audio = %d\n", g_settings.movie_audio);
    fprintf(f, "mute_unfocused = %d\n", g_settings.mute_unfocused);
    fprintf(f, "audio_channels = %s\n", k_audio_channels[clamp_int(g_settings.audio_channels, 0, 2)]);
    fprintf(f, "show_fps = %d\n", g_settings.hud_mode != HUD_OFF ? 1 : 0);
    fprintf(f, "upscaler = %s\n", g_settings.upscaler_auto ? "auto" : g_settings.upscaler == UPSCALE_METALFX ? "metalfx" :
                                   g_settings.upscaler == UPSCALE_FSR1 ? "fsr1" : "bilinear");
    fprintf(f, "antialias = %s\n", g_settings.antialias == AA_SMAA ? "smaa" : "off");
    fprintf(f, "resolution = %s\n", g_settings.res_auto ? "auto" : res_name(g_settings.res_mode));
    fprintf(f, "inv_keys = %d\n", g_settings.inv_keys);
    fprintf(f, "dpad_move = %d\n", g_settings.dpad_move);
    fprintf(f, "rumble = %d\n", g_settings.rumble);
    fprintf(f, "rumble_strength = %d\n", g_settings.rumble_strength);
    fprintf(f, "controller_light = %d\n", g_settings.controller_light);
    fprintf(f, "background_input = %d\n", g_settings.bg_input);
    fprintf(f, "move_360 = %d\nblur_removal = %d\nhide_hero_icon = %d\n", g_settings.move_360, g_settings.blur_removal,
            g_settings.hide_hero_icon);
    static const char *const prompts[] = {"auto", "nintendo", "xbox", "playstation"};
    fprintf(f, "prompts = %s\n", prompts[g_settings.prompts & 3]);
    fprintf(f, "menu_confirm = %s\n", g_settings.menu_confirm == MENU_CONFIRM_CONTROLLER ? "controller" : "game");
    fprintf(f, "mirror_mode = %d\n", g_settings.mirror_mode);
    fprintf(f, "language = %s\n", g_language_codes[g_settings.language > LANG_AUTO && g_settings.language < N_LANGUAGES
                                                     ? g_settings.language : LANG_AUTO]);
    fprintf(f, "tracker = %d\n", g_settings.tracker);
    if (g_settings.tracker_filters >= 0) fprintf(f, "tracker_filters = %d\n", g_settings.tracker_filters);
    fprintf(f, "aspect_asked = %d\n", g_settings.aspect_asked);
    fprintf(f, "menu_tip_seen = %d\n", g_settings.menu_tip_seen);
    fprintf(f, "onedrive_kept = %d\n", g_settings.onedrive_kept);
    static const char *const letters[] = {"off", "explored", "map"};
    fprintf(f, "chaos_letters = %s\n", letters[g_settings.chaos_letters]);
    fprintf(f, "cheat_hearts = %d\ncheat_ammo = %d\ncheat_rupees = %d\n", g_settings.cheat_hearts, g_settings.cheat_ammo,
            g_settings.cheat_rupees);
    fprintf(f, "cheat_hookshot = %d\n", g_settings.cheat_hookshot);
    fprintf(f, "no_buff_drops = %d\n", g_settings.no_buff_drops);
    fprintf(f, "diff_damage = %.2f\ndiff_ohko = %d\ndiff_hero = %d\n", g_settings.diff_damage, g_settings.diff_ohko,
            g_settings.diff_hero);
    fprintf(f, "walk_speed = %.2f\n", g_settings.walk_speed);
    fprintf(f, "walk_speed_dungeons = %d\n", g_settings.walk_speed_dungeons);
    fprintf(f, "timer_position = %s\n", g_settings.timer_pos == TIMER_POS_RIGHT ? "right" : "left");
    fprintf(f, "timer = %d\nlivesplit = %d\nlivesplit_host = %s\n", g_settings.timer, g_settings.livesplit,
            g_settings.livesplit_host);
    static const struct { const char *key; const CosColour *c; } cols[] = {
        {"tunic", &g_settings.tunic}, {"hearts", &g_settings.hearts}, {"hair", &g_settings.hair},
        {"shield", &g_settings.shield}, {"skin", &g_settings.skin},
        {"leaves", &g_settings.leaves}, {"grass", &g_settings.grass}, {"sand", &g_settings.sand}};
    for (size_t i = 0; i < sizeof cols / sizeof *cols; i++) {
        char v[16];
        col_format(cols[i].c, v, sizeof v);
        fprintf(f, "%s = %s\n", cols[i].key, v);
    }
    fprintf(f, "enemy_colours = %s\n", k_enemy_colour_keys[g_settings.enemy_colours]);
    fprintf(f, "screen_filter = %s\n", k_screen_filter_keys[g_settings.screen_filter]);
    fprintf(f, "ls_deadzone = %d\nrs_deadzone = %d\ntrigger_threshold = %d\n",
            clamp_int(g_settings.ls_deadzone, 0, 40), clamp_int(g_settings.rs_deadzone, 0, 40),
            clamp_int(g_settings.trigger_threshold, 10, 90));
    fprintf(f, "pad_layout = %s\n", k_pad_layouts[clamp_int(g_settings.pad_layout, 0, 1)]);
    fprintf(f, "ui_lang = %s\n", k_ui_langs[clamp_int(g_settings.ui_lang, 0, N_UI_LANGS - 1)]);
    fprintf(f, "game_lang = %s\n", k_game_langs[clamp_int(g_settings.game_lang, 0, N_GAME_LANGS - 1)]);
    fprintf(f, "game_dir = %s\n", g_settings.game_dir);
    fprintf(f, "skip_launcher = %d\n", g_settings.skip_launcher);
    fprintf(f, "vk_wait_pipes = %d\nvk_copy_vertices = %d\nvk_gpu_overlap = %d\n",
            g_settings.vk_wait_pipes, g_settings.vk_copy_vertices, g_settings.vk_gpu_overlap);
    save_hotkey(f, "hotkey_menu", g_settings.hotkey_menu);
    save_hotkey(f, "hotkey_hud", g_settings.hotkey_hud);
    save_hotkey(f, "hotkey_speed", g_settings.hotkey_speed);
    save_hotkey(f, "hotkey_fullscreen", g_settings.hotkey_fullscreen);
    bind_settings_save(f);
    fprintf(f, "rando_seed = %s\n", g_settings.rando_seed);
    if (g_settings.rando_options[0]) fprintf(f, "rando_options = %s\n", g_settings.rando_options);
    fclose(f);
}
