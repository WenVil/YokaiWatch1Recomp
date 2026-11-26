// The game-address tables of every supported game version and their selection by build ID (game_addresses.h).
#include "game_addresses.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "paths.h"
#include "rt.h"

static const GameAddrs ga_vyw1_jp_v1_3_0 = {0};
static const GameAddrs ga_vyw1_universal = {0};
static char g_uni_label[64] = "Yo-kai Watch 1 Universal";
static char g_uni_build_id[41];
static GameVersion g_uni_version = {"vyw1_universal", g_uni_label, g_uni_build_id, &ga_vyw1_universal};

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

uint64_t game_npdm_title_id(const char *npdm_path) {
    if (!npdm_path || !*npdm_path) return 0;
    FILE *f = plat_fopen(npdm_path, "rb");
    if (!f) return 0;
    uint8_t hdr[0x80];
    if (fread(hdr, 1, sizeof hdr, f) != sizeof hdr || memcmp(hdr, "META", 4) != 0) {
        fclose(f);
        return 0;
    }
    uint32_t aci0_off = (uint32_t)hdr[0x70] | ((uint32_t)hdr[0x71] << 8) |
                        ((uint32_t)hdr[0x72] << 16) | ((uint32_t)hdr[0x73] << 24);
    if (aci0_off < 0x80 || aci0_off > 0x100000 || fseek(f, (long)aci0_off, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    uint8_t aci0[0x20];
    bool ok = fread(aci0, 1, sizeof aci0, f) == sizeof aci0 && memcmp(aci0, "ACI0", 4) == 0;
    fclose(f);
    if (!ok) return 0;
    uint64_t tid = 0;
    for (int i = 0; i < 8; i++) tid |= (uint64_t)aci0[0x10 + i] << (i * 8);
    return tid;
}

static bool has_yw1_archive(const char *game_dir) {
    if (!game_dir || !*game_dir) return false;
    char romfs[1100], p[1200];
    if (!rt_find_subdir_icase(game_dir, "romfs", romfs, sizeof romfs))
        snprintf(romfs, sizeof romfs, "%s/romfs", game_dir);
    static const char *const names[] = {"yw1_a.fa", "YW1_A.FA"};
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        snprintf(p, sizeof p, "%s/%s", romfs, names[i]);
        FILE *f = plat_fopen(p, "rb");
        if (f) { fclose(f); return true; }
    }
    return false;
}

static const GameVersion *universal_version_for(const uint8_t id[20], const char *exefs_dir, const char *game_dir) {
    char npdm[1200];
    snprintf(npdm, sizeof npdm, "%s/main.npdm", exefs_dir);
    uint64_t tid = game_npdm_title_id(npdm);
    if (tid != 0 && tid != YW1_TITLE_ID) return NULL;  // explicitly another Switch title
    if (tid != YW1_TITLE_ID && !has_yw1_archive(game_dir)) return NULL;
    for (int i = 0; i < 20; i++) snprintf(g_uni_build_id + i * 2, 3, "%02X", id[i]);
    g_uni_build_id[40] = 0;
    return &g_uni_version;
}

const GameVersion *game_version_of_main(const char *main_path) {
    uint8_t id[20];
    if (!read_build_id(main_path, id)) return NULL;
    const GameVersion *v = game_version_by_build_id(id);
    if (v) return v;
    char exefs[1100];
    snprintf(exefs, sizeof exefs, "%s", main_path);
    char *s = strrchr(exefs, '/'), *b = strrchr(exefs, '\\');
    if (b > s) s = b;
    if (s) *s = 0; else snprintf(exefs, sizeof exefs, ".");
    return universal_version_for(id, exefs, NULL);
}

const GameVersion *game_version_of(const char *game_dir) {
    char exefs[1100], path[1100];
    uint8_t id[20];
    if (!rt_find_subdir_icase(game_dir, "exefs", exefs, sizeof exefs))
        snprintf(exefs, sizeof exefs, "%s/exefs", game_dir);
    snprintf(path, sizeof path, "%s/main", exefs);
    if (!read_build_id(path, id)) return NULL;
    const GameVersion *v = game_version_by_build_id(id);
    return v ? v : universal_version_for(id, exefs, game_dir);
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
    if (!v) v = universal_version_for(id, exefs, game_dir);
    if (!v) {
        char npdm[1200];
        snprintf(npdm, sizeof npdm, "%s/main.npdm", exefs);
        uint64_t tid = game_npdm_title_id(npdm);
        if (tid != 0 && tid != YW1_TITLE_ID) {
            snprintf(why, why_size,
                     "these game files belong to Title ID %016llX (expected Yo-kai Watch 1 %016llX)",
                     (unsigned long long)tid, (unsigned long long)YW1_TITLE_ID);
        } else {
            snprintf(why, why_size,
                     "these game files are not a supported Yo-kai Watch 1 dump (main build ID "
                     "%02X%02X%02X%02X%02X%02X%02X%02X)",
                     id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7]);
        }
        return false;
    }
    g_version = v;
    g_ga = v->addrs;
    char label[64];
    LOG("[game] version %s (title ID %016llX, main build ID %.16s, address table %s)\n",
        game_release_label(exefs, label, sizeof label), (unsigned long long)YW1_TITLE_ID, v->build_id, v->key);
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
