// A fatal error, explained to the player: the crash dialog (plat_fatal_notice) says what went wrong in plain words
// and what to do about it; the technical lines (FATAL / signal, backtrace, registers) stay in the log for bug reports.
// Known causes are recognised from what the runtime knows when it stops: the fatal line, where the crash was (the
// game code, LAS Recomp itself, the graphics driver), and the game files.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "game_addresses.h"
#include "platform.h"
#include "rt.h"

static char g_line[512];   // the first fatal line (FATAL message / "signal 11 accessing 0x10")
static uint64_t g_pc;      // a crash's host pc (0: a FATAL)
int rt_romfs_files = -1;   // the game's own RomFS files (runtime/services/fs.c; -1: not read yet)
uint64_t rt_romfs_bytes;   // their sizes summed (issue #391: one damaged file, every file there)
int rt_romfs_missing;      // the game's files not found by name and size (#391: a renamed file keeps count and bytes)
enum { ROMFS_FILES = 3836 };  // (every supported version: runtime/ui/launcher.mm counts the same)

void rt_fatal_msg(const char *fmt, ...) {
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    fprintf(stderr, "FATAL: %s", b);
    if (!g_line[0]) snprintf(g_line, sizeof g_line, "%s", b);
}

void rt_crash_fault(const char *what, uint64_t fa, uint64_t pc) {
    if (!g_line[0]) snprintf(g_line, sizeof g_line, "%s accessing %#llx", what, (unsigned long long)fa);
    if (!g_pc) g_pc = pc;
}

static bool has(const char *s) { return strstr(g_line, s) != NULL; }

const char *rt_host_fn_name(uint64_t pc, uint64_t *offset);  // runtime/dispatch.c: inside the game code?

static bool contains_nocase(const char *hay, const char *needle) {  // (no strcasestr on Windows)
    for (; *hay; hay++) {
        size_t i = 0;
        while (needle[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i])) i++;
        if (!needle[i]) return true;
    }
    return false;
}

// The graphics driver's own code (the crash was inside it, not in Yo-kai Watch 1 Recompiled): OpenGL / Mesa drivers
static bool in_gpu_driver(uint64_t pc) {
    char m[512];
    if (!pc || !plat_module_name(pc, m, sizeof m)) return false;
    static const char *const drivers[] = {"nvoglv", "nvgpucomp", "nvwgf", "libnvidia", "atio6axx", "atig6",
                                          "ig9icd", "igxelpicd", "igc64", "igd", "radeonsi", "iris", "nouveau",
                                          "llvmpipe", "libGL", "opengl32", "libLLVM"};
    for (size_t i = 0; i < sizeof drivers / sizeof *drivers; i++)
        if (contains_nocase(m, drivers[i])) return true;
    return false;
}

void rt_crash_explain(char *out, size_t n, const char *log_path) {
    static const char *const report = "Please attach the log file below when reporting an issue. "
                                      "Your progress up to your last save or autosave is safe.";
    char head[160], what[400];
    if (has("built for another version")) {
        snprintf(head, sizeof head, "The game code was built for other game files or another build");
        snprintf(what, sizeof what, "Start Yo-kai Watch 1 Recompiled again so the launcher rebuilds the game code.");
    } else if (has("unsupported version of the game")) {
        snprintf(head, sizeof head, "Wrong version of the game");
        snprintf(what, sizeof what, "Yo-kai Watch 1 Recompiled needs Yo-kai Watch 1 (Nintendo Switch) v1.3.0 JP game files.");
    } else if (has("yw1_a.fa") || has("cannot read the game data") || has("exefs/main")) {
        snprintf(head, sizeof head, "Your game files are incomplete or missing");
        snprintf(what, sizeof what, "Select the extracted game folder containing exefs/main and romfs/yw1_a.fa on the launcher screen.");
    } else if (has("no font for the game's text")) {
        snprintf(head, sizeof head, "No font for the game's text");
        snprintf(what, sizeof what, "Ensure system CJK/UI fonts or bundled fallback fonts are available.");
    } else if (has("get_terminate") || has("uncaught C++ exception")) {
        snprintf(head, sizeof head, "The game ran into an error it couldn't recover from");
        snprintf(what, sizeof what, "This is a runtime bug in Yo-kai Watch 1 Recompiled. %s", report);
    } else if (in_gpu_driver(g_pc)) {
        snprintf(head, sizeof head, "Your graphics driver crashed");
        snprintf(what, sizeof what, "Update your graphics driver (NVIDIA, AMD or Intel) and try again. %s", report);
    } else if (has("OpenGL") || has("GL_") || has("SDL_GL")) {
        snprintf(head, sizeof head, "OpenGL graphics driver error");
        snprintf(what, sizeof what, "Yo-kai Watch 1 Recompiled requires OpenGL 4.5 Compatibility support. Update your GPU driver. %s", report);
    } else if (has("out of memory") || has("cannot allocate") || has("alloc failed")) {
        snprintf(head, sizeof head, "Out of memory");
        snprintf(what, sizeof what, "Close other programs and try again.");
    } else if (g_pc && rt_host_fn_name(g_pc, NULL)) {
        snprintf(head, sizeof head, "The game crashed");
        snprintf(what, sizeof what, "This is a bug in Yo-kai Watch 1 Recompiled. %s", report);
    } else {
        snprintf(head, sizeof head, "Yo-kai Watch 1 Recompiled stopped because of an error");
        snprintf(what, sizeof what, "%s", report);
    }
    (void)rt_romfs_files; (void)rt_romfs_bytes; (void)rt_romfs_missing; (void)ROMFS_FILES;
    fprintf(stderr, "[crash] %s\n", head);
    if (log_path && *log_path) snprintf(out, n, "%s\n\n%s\n\nLog file:\n%s", head, what, log_path);
    else snprintf(out, n, "%s\n\n%s", head, what);
}
