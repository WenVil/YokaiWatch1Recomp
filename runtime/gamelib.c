// Loads the recompiled game code library (runtime/gamelib.h).
#include "gamelib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "paths.h"
#include "rt.h"

static const GameLibTables *g_tables;
static char g_error[256];

static bool full_id_utf8(const char *game_dir, char *out, size_t size) {
    char exefs[1100], path[1100];
    unsigned char hdr[0x60];
    if (!rt_find_subdir_icase(game_dir, "exefs", exefs, sizeof exefs))
        snprintf(exefs, sizeof exefs, "%s/exefs", game_dir);
    snprintf(path, sizeof path, "%s/main", exefs);
    FILE *f = plat_fopen(path, "rb");
    if (!f) return false;
    bool ok = fread(hdr, 1, sizeof hdr, f) == sizeof hdr && !memcmp(hdr, "NSO0", 4);
    fclose(f);
    if (!ok) return false;
    int n = snprintf(out, size, "%s-", GAMELIB_ID);
    for (int i = 0; i < 8 && n + 2 < (int)size; i++) n += snprintf(out + n, size - n, "%02X", hdr[0x40 + i]);
    return true;
}

// The library's id file (<library>.id, written next to it by the build): its id (gamelib_full_id), read without
// loading it.
static bool id_matches(const char *lib, const char *want) {
    char path[1100], id[128] = {0};
    snprintf(path, sizeof path, "%s.id", lib);
    FILE *f = plat_fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(id, 1, sizeof id - 1, f);
    fclose(f);
    while (n && (id[n - 1] == '\n' || id[n - 1] == '\r')) id[--n] = 0;
#ifdef _WIN32
    return !_stricmp(id, want);
#else
    return !strcasecmp(id, want);
#endif
}

static bool exists(const char *p) {
    FILE *f = plat_fopen(p, "rb");
    if (f) fclose(f);
    return f != NULL;
}

// The places to look, in order (gamelib.h). Returns how many were filled.
static int candidates(const char *game_dir, const char *want, char out[10][1100]) {
    int n = 0;
    const char *env = getenv("LARECOMP_GAME_LIB");
    if (env && *env) snprintf(out[n++], 1100, "%s", env);

    const char *dash = want ? strrchr(want, '-') : NULL;
    const char *build16 = (dash && dash[1]) ? dash + 1 : NULL;
    char ver_name[96] = "";
    if (build16) {
        const char *dot = strrchr(GAMELIB_NAME, '.');
        int stem = dot ? (int)(dot - GAMELIB_NAME) : (int)strlen(GAMELIB_NAME);
        snprintf(ver_name, sizeof ver_name, "%.*s_%s%s", stem, GAMELIB_NAME, build16, dot ? dot : "");
    }

    const char *dr = rt_data_root();
    char exe_side[1024];
    if (ver_name[0]) {
        if (dr && dr[0]) snprintf(out[n++], 1100, "%s/%s", dr, ver_name);
        if (rt_resource(ver_name, NULL, exe_side, sizeof exe_side) && exists(exe_side))
            snprintf(out[n++], 1100, "%s", exe_side);
        if (exists(ver_name)) snprintf(out[n++], 1100, "%s", ver_name);
        if (game_dir) snprintf(out[n++], 1100, "%s/%s", game_dir, ver_name);
    }
    if (dr && dr[0]) snprintf(out[n++], 1100, "%s/%s", dr, GAMELIB_NAME);
    if (rt_resource(GAMELIB_NAME, NULL, exe_side, sizeof exe_side) && exists(exe_side))
        snprintf(out[n++], 1100, "%s", exe_side);
    if (exists(GAMELIB_NAME)) snprintf(out[n++], 1100, "%s", GAMELIB_NAME);
    if (game_dir && n < 10) snprintf(out[n++], 1100, "%s/%s", game_dir, GAMELIB_NAME);
    return n;
}

static char g_loaded_want[GAMELIB_FULL_ID_SIZE];

bool gamelib_present(const char *game_dir) {
    char want[GAMELIB_FULL_ID_SIZE];
    if (!game_dir || !full_id_utf8(game_dir, want, sizeof want)) return false;
    if (g_tables) {
#ifdef _WIN32
        if (!_stricmp(g_loaded_want, want)) return true;
#else
        if (!strcasecmp(g_loaded_want, want)) return true;
#endif
    }
    char c[10][1100];
    int n = candidates(game_dir, want, c);
    for (int i = 0; i < n; i++)
        if (exists(c[i]) && id_matches(c[i], want)) return true;
    return false;
}

static const GameLibTables *open_lib(const char *path, const char *want) {
#ifdef _WIN32
    wchar_t wp[1100];
    HMODULE h = MultiByteToWideChar(CP_UTF8, 0, path, -1, wp, 1100) > 0 ? LoadLibraryW(wp) : LoadLibraryA(path);
    if (!h) { snprintf(g_error, sizeof g_error, "can't load %s (error %lu)", path, GetLastError()); return NULL; }
    GameLibEntry entry = (GameLibEntry)(void *)GetProcAddress(h, GAMELIB_ENTRY);
#else
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) { snprintf(g_error, sizeof g_error, "%s", dlerror()); return NULL; }
    GameLibEntry entry = (GameLibEntry)dlsym(h, GAMELIB_ENTRY);
#endif
    if (!entry) { snprintf(g_error, sizeof g_error, "%s: not game code", path); return NULL; }
    const GameLibTables *t = entry();
    bool id_ok = false;
    if (t && t->id) {
#ifdef _WIN32
        id_ok = !_stricmp(t->id, want);
#else
        id_ok = !strcasecmp(t->id, want);
#endif
    }
    if (!t || t->abi != GAMELIB_ABI || !id_ok) {
        LOG("[gamelib] ABI check failed for %s (table=%p abi=%u id=%s want=%s)\n", path, (void *)t,
            t ? t->abi : 0, t && t->id ? t->id : "<null>", want);
        snprintf(g_error, sizeof g_error, "%s was built for another version", path);
        return NULL;  // (left loaded: nothing of it runs)
    }
    return t;
}

bool gamelib_load(const char *game_dir, char *why, size_t why_size) {
    char want[GAMELIB_FULL_ID_SIZE] = "";
    if (!game_dir || !full_id_utf8(game_dir, want, sizeof want)) {
        snprintf(g_error, sizeof g_error, "the game files' exefs/main is missing or unreadable");
        if (why) snprintf(why, why_size, "%s", g_error);
        return false;
    }
    if (g_tables) {
#ifdef _WIN32
        if (!_stricmp(g_loaded_want, want)) return true;
#else
        if (!strcasecmp(g_loaded_want, want)) return true;
#endif
        g_tables = NULL;
    }
    snprintf(g_error, sizeof g_error, "the game code hasn't been built: set up the game files on the start screen");
    char c[10][1100];
    int n = candidates(game_dir, want, c);
    const char *env = getenv("LARECOMP_GAME_LIB");
    for (int i = 0; i < n && !g_tables; i++) {
        if (!exists(c[i])) continue;
        bool given = i == 0 && env && *env;  // LARECOMP_GAME_LIB: loaded as given, the library's own id decides
        if (!given && !id_matches(c[i], want)) {
            snprintf(g_error, sizeof g_error, "%s was built for another version", c[i]);
            continue;
        }
        g_tables = open_lib(c[i], want);
        if (g_tables) {
            snprintf(g_loaded_want, sizeof g_loaded_want, "%s", want);
            LOG("[gamelib] %s (%llu + %llu + %llu functions)\n", c[i],
                (unsigned long long)*g_tables->main_count, (unsigned long long)*g_tables->sdk_count,
                (unsigned long long)*g_tables->subsdk0_count);
        }
    }
    if (!g_tables && why) snprintf(why, why_size, "%s", g_error);
    return g_tables != NULL;
}

const GameLibTables *gamelib_tables(void) { return g_tables; }
