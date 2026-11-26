// The game-address tables of every supported game version and their selection by build ID (game_addresses.h).
#include "game_addresses.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "paths.h"
#include "rt.h"

static const GameAddrs ga_vyw1_jp_v1_3_0 = {0};

static const GameVersion versions[] = {
#define GA_VERSION(key, label, build_id) {#key, label, build_id, &ga_##key},
#define GA_RELEASE(key, label, npdm_sha256)
#include "game_addresses/versions.h"
#undef GA_VERSION
#undef GA_RELEASE
};

const GameAddrs *g_ga;
static const GameVersion *g_version;

static int hexval(char c) { return c <= '9' ? c - '0' : c - 'A' + 10; }

const GameVersion *game_version_by_build_id(const uint8_t id[20]) {
    for (size_t i = 0; i < sizeof versions / sizeof versions[0]; i++) {
        const char *h = versions[i].build_id;
        int k = 0;
        while (k < 20 && id[k] == (uint8_t)(hexval(h[2 * k]) << 4 | hexval(h[2 * k + 1]))) k++;
        if (k == 20) return &versions[i];
    }
    return NULL;
}

// The main program's build ID (0x40 in the file). false: missing or not a program.
static bool read_build_id(const char *path, uint8_t id[20]) {
    uint8_t hdr[0x60];
    FILE *f = plat_fopen(path, "rb");
    if (!f) return false;
    bool ok = fread(hdr, 1, sizeof hdr, f) == sizeof hdr && !memcmp(hdr, "NSO0", 4);
    fclose(f);
    if (ok) memcpy(id, hdr + 0x40, 20);
    return ok;
}

const GameVersion *game_version_of_main(const char *main_path) {
    uint8_t id[20];
    return read_build_id(main_path, id) ? game_version_by_build_id(id) : NULL;
}

const GameVersion *game_version_of(const char *game_dir) {
    char exefs[1100], path[1100];
    if (!rt_find_subdir_icase(game_dir, "exefs", exefs, sizeof exefs))
        snprintf(exefs, sizeof exefs, "%s/exefs", game_dir);
    snprintf(path, sizeof path, "%s/main", exefs);
    return game_version_of_main(path);
}

bool game_version_select(const char *game_dir, char *why, size_t why_size) {
    uint8_t id[20];
    char exefs[1100], path[1100];
    if (!rt_find_subdir_icase(game_dir, "exefs", exefs, sizeof exefs))
        snprintf(exefs, sizeof exefs, "%s/exefs", game_dir);
    snprintf(path, sizeof path, "%s/main", exefs);
    if (!read_build_id(path, id)) {
        snprintf(why, why_size, "the game files' exefs/main is missing or unreadable");
        return false;
    }
    const GameVersion *v = game_version_by_build_id(id);
    if (!v) {
        snprintf(why, why_size, "these game files are an unsupported version of the game (main build ID "
                 "%02X%02X%02X%02X%02X%02X%02X%02X)",
                 id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7]);
        return false;
    }
    g_version = v;
    g_ga = v->addrs;
    char label[32];
    LOG("[game] version %s (main build ID %.16s, address table %s)\n",
        game_release_label(exefs, label, sizeof label), v->build_id, v->key);
    return true;
}

const GameVersion *game_version(void) { return g_version; }

uint64_t game_romfs_bytes(const GameVersion *v) {
    (void)v;
    return 0;
}

struct GameRomfsCheck { int unused; };

GameRomfsCheck *game_romfs_check_begin(const GameVersion *v) {
    (void)v;
    return NULL;
}

uint64_t game_romfs_hash(const char *rel, uint64_t size) {
    char digits[24];
    snprintf(digits, sizeof digits, "%llu", (unsigned long long)size);
    uint64_t h = 0xcbf29ce484222325ull;
    for (const unsigned char *p = (const unsigned char *)rel; ; p++) {
        h = (h ^ *p) * 0x100000001b3ull;
        if (!*p) break;
    }
    for (const char *p = digits; *p; p++) h = (h ^ (unsigned char)*p) * 0x100000001b3ull;
    return h;
}

bool game_romfs_check_file(GameRomfsCheck *c, const char *rel, uint64_t size) {
    (void)c; (void)rel; (void)size;
    return true;
}

int game_romfs_check_end(GameRomfsCheck *c) {
    free(c);
    return 0;
}

bool game_romfs_exefs_copy(const char *game_dir, const char *rel, uint64_t size) {
    static const char *const names[] = {"main", "main.npdm", "rtld", "sdk", "subsdk0", "subsdk1"};
    bool named = false;
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) named |= !strcmp(rel, names[i]);
    if (!named || !game_dir) return false;
    char path[1200];
    snprintf(path, sizeof path, "%s/exefs/%s", game_dir, rel);
    FILE *a = fopen(path, "rb");
    snprintf(path, sizeof path, "%s/romfs/%s", game_dir, rel);
    FILE *b = fopen(path, "rb");
    enum { CHUNK = 1 << 16 };
    char *x = malloc(2 * CHUNK), *y = x ? x + CHUNK : NULL;
    bool same = a && b && x;
    uint64_t n = 0;
    while (same) {
        size_t r = fread(x, 1, CHUNK, a), s = fread(y, 1, CHUNK, b);
        same = r == s && !memcmp(x, y, r);
        n += r;
        if (r < CHUNK) break;
    }
    free(x);
    if (a) fclose(a);
    if (b) fclose(b);
    return same && n == size;
}
