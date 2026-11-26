// Guest addresses / metadata per supported game version (game_addresses/versions.h).
// game_version_select (runtime/game_addresses.c) picks the version by the main program's build ID when booting.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GameAddrs {
    uint64_t _reserved;
} GameAddrs;

// The selected table (NULL until game_version_select succeeds).
extern const GameAddrs *g_ga;
#define GA(name) (g_ga->name)

typedef struct GameVersion {
    const char *key;       // "vyw1_jp_v1_3_0"
    const char *label;     // "Yo-kai Watch 1 JP 1.3.0"
    const char *build_id;  // the main program's build ID, 40 upper-case hex digits
    const GameAddrs *addrs;
} GameVersion;

// Expected Nintendo Switch Program/Title ID for Yo-kai Watch 1 (all JP updates v1.0.0 / v1.3.0 / v1.4.0+).
#define YW1_TITLE_ID 0x0100C0000CEEA000ULL

// Reads an exefs/main.npdm file and returns its ACI0 Program/Title ID (little-endian), or 0 if invalid.
uint64_t game_npdm_title_id(const char *npdm_path);

// The version whose main build ID is the first 20 bytes of id (exefs/main 0x40), or NULL.
const GameVersion *game_version_by_build_id(const uint8_t id[20]);
// The version of a main program file (exefs/main), or NULL (missing, unreadable or unknown).
const GameVersion *game_version_of_main(const char *main_path);
// The version of the game files in game_dir (its exefs/main), or NULL.
const GameVersion *game_version_of(const char *game_dir);
// Display/log release label of exefs_dir, or NULL for unknown version.
const char *game_release_label(const char *exefs_dir, char *buf, size_t size);
// Selects the version record for the game files in game_dir and logs it. false: why says what's wrong.
bool game_version_select(const char *game_dir, char *why, size_t why_size);
// The selected version (NULL before game_version_select).
const GameVersion *game_version(void);
uint64_t game_romfs_bytes(const GameVersion *v);
typedef struct GameRomfsCheck GameRomfsCheck;
GameRomfsCheck *game_romfs_check_begin(const GameVersion *v);
bool game_romfs_check_file(GameRomfsCheck *c, const char *rel, uint64_t size);
int game_romfs_check_end(GameRomfsCheck *c);
uint64_t game_romfs_hash(const char *rel, uint64_t size);
bool game_romfs_exefs_copy(const char *game_dir, const char *rel, uint64_t size);

#ifdef __cplusplus
}
#endif
