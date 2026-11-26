// The game's RomFS image, laid out from a folder of game files (runtime/romfs_build.c). The game reads its data through
// one raw RomFS image; runtime/services/fs.c serves that image from this layout - the header and tables in memory, each
// file's data straight from its file in the player's romfs/ folder - so no second copy of the game data is kept.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROMFS_DATA_START 0x200u
typedef struct { uint64_t start, size; char *path; } RomfsLayoutFile;  // start: offset in the image
typedef struct {
    uint8_t header[ROMFS_DATA_START];  // the image's first bytes (the 0x50-byte header, then zeros)
    uint64_t tables_off, tables_size;  // dir hash, dir entries, file hash and file entries, one blob
    uint8_t *tables;
    RomfsLayoutFile *files;  // sorted by start; the bytes between files read as zeros
    int nfiles;
    uint64_t size;  // the whole image
} RomfsLayout;

// Lays out the RomFS image of the folder `dir` with optional LayeredFS overlay directories applied in order.
// `overlay_dirs[n_overlays - 1]` has the highest priority. false = failed (why: a short message).
bool romfs_layout_with_overlays(const char *dir, const char *const *overlay_dirs, int n_overlays,
                                RomfsLayout *out, char *why, size_t why_size,
                                int *out_replaced, int *out_added);

// Lays out the RomFS image of the folder `dir`. false = failed (why: a short message).
bool romfs_layout(const char *dir, RomfsLayout *out, char *why, size_t why_size);
void romfs_layout_free(RomfsLayout *l);

typedef struct {
    char name[256];
    bool is_dir;
    uint64_t size;
} FsLayeredEntry;

// LayeredFS mods helpers (runtime/services/fs.c).
void fs_mods_dir(char *out, size_t cap);
bool fs_mods_enabled(void);
int fs_mods_count(void);
int fs_list_active_mod_romfs(char out_romfs[][1100], char out_names[][256], int max_mods);
bool fs_resolve_romfs_file(const char *rel, char *out, size_t cap, uint64_t *out_size);
bool fs_layered_stat(const char *rel, bool *out_is_dir, uint64_t *out_size);
int fs_layered_list_dir(const char *rel, FsLayeredEntry **out_entries);

// Bytes of file data under dir (the image is about this plus its tables).
uint64_t romfs_dir_bytes(const char *dir);

// Copies the folder tree src to dst (hard links where the file system allows: no second copy of 6 GB). progress as
// above. false = failed.
bool romfs_copy_tree(const char *src, const char *dst, void (*progress)(uint64_t done, uint64_t total));

#ifdef __cplusplus
}
#endif
