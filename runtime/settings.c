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
// Otherwise reads/writes yokaiwatch1.ini in the data root.
static const char *settings_file(void) {
    const char *e = getenv("LARECOMP_SETTINGS");
    return e && *e ? e : "yokaiwatch1.ini";
}

static const char *settings_read_file(void) {
    const char *e = getenv("LARECOMP_SETTINGS");
    return (e && *e) ? e : "yokaiwatch1.ini";
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

Settings g_settings = {
    .fullscreen = false,
    .volume = 1.0f,
    .reverb = true,
    .movie_audio = true,
    .rumble = true,
    .rumble_strength = 100,
    .controller_light = true,
    .menu_confirm = MENU_CONFIRM_CONTROLLER,
    .show_fps = false,
    .dpad_move = true,
#ifndef __APPLE__
    .bg_input = true,
#endif
    .window_mode = WIN_WINDOWED,
    .output_res = OUT_RES_AUTO,
    .res_scale = RES_SCALE_1X,
    .aspect_mode = ASPECT_MODE_LOCK_16_9,
    .vsync_mode = VSYNC_MAILBOX,
    .hud_mode = HUD_OFF,
    .speed_pct = 100,
    .mods_enabled = true,
    .mute_unfocused = false,
    .audio_channels = AUDIO_CH_AUTO,
    .ls_deadzone = 12,
    .rs_deadzone = 12,
    .trigger_threshold = 50,
    .pad_layout = PAD_LAYOUT_NINTENDO,
    .ui_lang = UI_LANG_AUTO,
    .game_lang = GAME_LANG_AUTO,
    .game_dir = "",
    .skip_launcher = false,
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
    if (gv && !strncmp(gv->key, "vyw1_", 5)) {
        return "ja";
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
    return settings_game_lang_code();
}

const char *settings_language_name(void) {
    const char *c = settings_language();
    for (int i = 0; i < N_LANGUAGES; i++)
        if (!strcmp(c, g_language_codes[i])) return g_language_names[i];
    return "Japanese";
}

static const char *const k_window_modes[] = {"windowed", "borderless", "exclusive"};
static const char *const k_output_res[] = {"auto", "720p", "1080p", "1440p", "4k"};
static const char *const k_res_scales[] = {"1x", "1.5x", "2x"};
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

static void apply_launcher_pad_fallback(void) {
    const char *e = getenv("RECOMPLAUNCHER_PAD");
    if (e && !SDL_strcasecmp(e, "xbox")) bind_preset_pad_xbox();
}

void settings_load(void) {
    static bool loaded;
    if (loaded) return;
    loaded = true;
    const char *src = settings_read_file();
    FILE *f = open_utf8(src, "r");
    if (!f) {
        apply_launcher_pad_fallback();
        return;
    }
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
        if (!strcmp(key, "game_dir")) {
            strlcpy(g_settings.game_dir, val, sizeof g_settings.game_dir);
            continue;
        }
        if (fields != 2) continue;
        if (!strcmp(key, "fullscreen")) { g_settings.fullscreen = atoi(val) != 0; seen_fs = true; }
        else if (!strcmp(key, "window_mode")) { g_settings.window_mode = parse_choice(val, k_window_modes, 3, WIN_WINDOWED); seen_win_mode = true; }
        else if (!strcmp(key, "output_res")) g_settings.output_res = parse_choice(val, k_output_res, 5, OUT_RES_AUTO);
        else if (!strcmp(key, "res_scale")) {
            if (!strcmp(val, "1.5x") || !strcmp(val, "150")) g_settings.res_scale = RES_SCALE_1_5X;
            else if (!strcmp(val, "2x") || !strcmp(val, "200")) g_settings.res_scale = RES_SCALE_2X;
            else g_settings.res_scale = parse_choice(val, k_res_scales, 3, RES_SCALE_1X);
        }
        else if (!strcmp(key, "aspect_mode")) g_settings.aspect_mode = parse_choice(val, k_aspect_modes, 2, ASPECT_MODE_LOCK_16_9);
        else if (!strcmp(key, "vsync_mode")) g_settings.vsync_mode = parse_choice(val, k_vsync_modes, 3, VSYNC_MAILBOX);
        else if (!strcmp(key, "hud_mode")) { g_settings.hud_mode = parse_choice(val, k_hud_modes, 3, HUD_OFF); seen_hud_mode = true; }
        else if (!strcmp(key, "game_speed")) {
            double sp = atof(val);
            int pct = (int)(sp * 100.0 + 0.5);
            g_settings.speed_pct = (pct >= 275) ? 300 : (pct >= 175) ? 200 : (pct >= 125) ? 150 : 100;
        }
        else if (!strcmp(key, "mods_enabled")) g_settings.mods_enabled = atoi(val) != 0;
        else if (!strcmp(key, "volume")) g_settings.volume = (float)atof(val);
        else if (!strcmp(key, "reverb")) g_settings.reverb = atoi(val) != 0;
        else if (!strcmp(key, "movie_audio")) g_settings.movie_audio = atoi(val) != 0;
        else if (!strcmp(key, "mute_unfocused")) g_settings.mute_unfocused = atoi(val) != 0;
        else if (!strcmp(key, "audio_channels")) g_settings.audio_channels = parse_choice(val, k_audio_channels, 3, AUDIO_CH_AUTO);
        else if (!strcmp(key, "show_fps")) { g_settings.show_fps = atoi(val) != 0; seen_show_fps = true; }
        else if (!strcmp(key, "dpad_move")) g_settings.dpad_move = atoi(val) != 0;
        else if (!strcmp(key, "rumble")) g_settings.rumble = atoi(val) != 0;
        else if (!strcmp(key, "rumble_strength")) g_settings.rumble_strength = atoi(val);
        else if (!strcmp(key, "controller_light")) g_settings.controller_light = atoi(val) != 0;
        else if (!strcmp(key, "background_input")) g_settings.bg_input = atoi(val) != 0;
        else if (!strcmp(key, "prompts")) g_settings.prompts = !strcmp(val, "nintendo") ? PROMPTS_NINTENDO : !strcmp(val, "xbox") ?
                                                              PROMPTS_XBOX : !strcmp(val, "playstation") ? PROMPTS_PLAYSTATION : PROMPTS_AUTO;
        else if (!strcmp(key, "menu_confirm")) g_settings.menu_confirm = !strcmp(val, "game") ? MENU_CONFIRM_GAME
                                                                                                  : MENU_CONFIRM_CONTROLLER;
        else if (!strcmp(key, "language")) {
            const char *v = !strcmp(val, "zh-CN") ? "zh-Hans" : !strcmp(val, "zh-TW") ? "zh-Hant" : val;
            g_settings.language = LANG_AUTO;
            for (int i = 1; i < N_LANGUAGES; i++)
                if (!strcmp(v, g_language_codes[i])) g_settings.language = i;
        }
        else if (!strcmp(key, "menu_tip_seen")) g_settings.menu_tip_seen = atoi(val) != 0, tip_key = true;
        else if (!strcmp(key, "aspect_asked")) g_settings.aspect_asked = atoi(val) != 0, aspect_key = true;
        else if (!strcmp(key, "onedrive_kept")) g_settings.onedrive_kept = atoi(val) != 0;
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
            g_settings.game_lang = parse_choice(v ? v : val, k_game_langs, N_GAME_LANGS, GAME_LANG_AUTO);
        }
        else if (!strcmp(key, "skip_launcher")) g_settings.skip_launcher = atoi(val) != 0;
        else if (!strcmp(key, "hotkey_menu")) g_settings.hotkey_menu = parse_hotkey(val, SDL_SCANCODE_F1);
        else if (!strcmp(key, "hotkey_hud")) g_settings.hotkey_hud = parse_hotkey(val, SDL_SCANCODE_F2);
        else if (!strcmp(key, "hotkey_speed")) g_settings.hotkey_speed = parse_hotkey(val, SDL_SCANCODE_F3);
        else if (!strcmp(key, "hotkey_fullscreen")) g_settings.hotkey_fullscreen = parse_hotkey(val, SDL_SCANCODE_F11);
        else {
            if (!strcmp(key, "bind_pad_a") || !strcmp(key, "bind_pad_b") || !strcmp(key, "bind_pad_x") || !strcmp(key, "bind_pad_y"))
                seen_pad_face = true;
            bind_settings_load(key, val);  // bind_pad_* / bind_key_*: runtime/hle/bindings.c
        }
    }
    fclose(f);
    if (!tip_key) g_settings.menu_tip_seen = true;
    if (!aspect_key) g_settings.aspect_asked = true;
    if (seen_win_mode) g_settings.fullscreen = (g_settings.window_mode != WIN_WINDOWED);
    else if (seen_fs && g_settings.fullscreen) g_settings.window_mode = WIN_BORDERLESS;
    if (seen_hud_mode) g_settings.show_fps = (g_settings.hud_mode != HUD_OFF);
    else if (seen_show_fps && g_settings.show_fps) g_settings.hud_mode = HUD_FPS;
    if (seen_pad_layout && !seen_pad_face) {
        if (g_settings.pad_layout == PAD_LAYOUT_XBOX) bind_preset_pad_xbox();
        else bind_preset_pad_nintendo();
    } else if (!seen_pad_layout && !seen_pad_face) {
        apply_launcher_pad_fallback();
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
    fprintf(f, "# YokaiWatch1Recomp settings (edited from the in-game menu: F1, or Minus+Plus on a controller)\n");
    fprintf(f, "window_mode = %s\n", k_window_modes[clamp_int(g_settings.window_mode, 0, 2)]);
    fprintf(f, "fullscreen = %d\n", g_settings.window_mode != WIN_WINDOWED ? 1 : 0);
    fprintf(f, "output_res = %s\n", k_output_res[clamp_int(g_settings.output_res, 0, 4)]);
    fprintf(f, "res_scale = %s\n", k_res_scales[clamp_int(g_settings.res_scale, 0, 2)]);
    fprintf(f, "aspect_mode = %s\n", k_aspect_modes[clamp_int(g_settings.aspect_mode, 0, 1)]);
    fprintf(f, "vsync_mode = %s\n", k_vsync_modes[clamp_int(g_settings.vsync_mode, 0, 2)]);
    fprintf(f, "hud_mode = %s\n", k_hud_modes[clamp_int(g_settings.hud_mode, 0, 2)]);
    fprintf(f, "game_speed = %.1fx\n", (double)(g_settings.speed_pct >= 100 ? g_settings.speed_pct : 100) / 100.0);
    fprintf(f, "mods_enabled = %d\n", g_settings.mods_enabled ? 1 : 0);
    fprintf(f, "volume = %.2f\n", g_settings.volume);
    fprintf(f, "reverb = %d\n", g_settings.reverb);
    fprintf(f, "movie_audio = %d\n", g_settings.movie_audio);
    fprintf(f, "mute_unfocused = %d\n", g_settings.mute_unfocused);
    fprintf(f, "audio_channels = %s\n", k_audio_channels[clamp_int(g_settings.audio_channels, 0, 2)]);
    fprintf(f, "show_fps = %d\n", g_settings.hud_mode != HUD_OFF ? 1 : 0);
    fprintf(f, "dpad_move = %d\n", g_settings.dpad_move);
    fprintf(f, "rumble = %d\n", g_settings.rumble);
    fprintf(f, "rumble_strength = %d\n", g_settings.rumble_strength);
    fprintf(f, "controller_light = %d\n", g_settings.controller_light);
    fprintf(f, "background_input = %d\n", g_settings.bg_input);
    static const char *const prompts[] = {"auto", "nintendo", "xbox", "playstation"};
    fprintf(f, "prompts = %s\n", prompts[g_settings.prompts & 3]);
    fprintf(f, "menu_confirm = %s\n", g_settings.menu_confirm == MENU_CONFIRM_CONTROLLER ? "controller" : "game");
    fprintf(f, "aspect_asked = %d\n", g_settings.aspect_asked);
    fprintf(f, "menu_tip_seen = %d\n", g_settings.menu_tip_seen);
    fprintf(f, "onedrive_kept = %d\n", g_settings.onedrive_kept);
    fprintf(f, "ls_deadzone = %d\nrs_deadzone = %d\ntrigger_threshold = %d\n",
            clamp_int(g_settings.ls_deadzone, 0, 40), clamp_int(g_settings.rs_deadzone, 0, 40),
            clamp_int(g_settings.trigger_threshold, 10, 90));
    fprintf(f, "pad_layout = %s\n", k_pad_layouts[clamp_int(g_settings.pad_layout, 0, 1)]);
    fprintf(f, "ui_lang = %s\n", k_ui_langs[clamp_int(g_settings.ui_lang, 0, N_UI_LANGS - 1)]);
    fprintf(f, "game_lang = %s\n", k_game_langs[clamp_int(g_settings.game_lang, 0, N_GAME_LANGS - 1)]);
    fprintf(f, "game_dir = %s\n", g_settings.game_dir);
    fprintf(f, "skip_launcher = %d\n", g_settings.skip_launcher);
    save_hotkey(f, "hotkey_menu", g_settings.hotkey_menu);
    save_hotkey(f, "hotkey_hud", g_settings.hotkey_hud);
    save_hotkey(f, "hotkey_speed", g_settings.hotkey_speed);
    save_hotkey(f, "hotkey_fullscreen", g_settings.hotkey_fullscreen);
    bind_settings_save(f);
    fclose(f);
}
