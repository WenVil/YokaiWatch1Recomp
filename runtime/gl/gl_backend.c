// Native OpenGL 4.5 Compatibility window, context handoff, event pump, and frame dump backend.
#include "gl_backend.h"

#include <SDL3/SDL.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "../game_addresses.h"
#include "../paths.h"
#include "../rt.h"
#include "../settings.h"

void input_apply_background(void);
void input_load_user_mappings(void);
void input_cursor_event(const SDL_Event *e);
void input_handle_event(const SDL_Event *e);
void input_rumble_poll(void);
void input_rumble_stop(void);
void input_led_poll(void);
void input_led_stop(void);
bool input_user_blocked(const SDL_Event *e);

static SDL_Window *s_window;
static SDL_GLContext s_gl_ctx;
static atomic_bool s_win_focused = true;
static atomic_bool s_quitting;
static atomic_uint_fast64_t s_frames;
static int s_swap_interval = 1;
static atomic_flag s_quit_once = ATOMIC_FLAG_INIT;

bool gl_backend_is_active(void) {
    const char *env = getenv("YW1_GL_BACKEND");
    if (env && *env && strcmp(env, "0") != 0) return true;
    const GameVersion *gv = game_version();
    return gv && strncmp(gv->key, "vyw1_", 5) == 0;
}

static bool frame_should_dump(uint64_t frame) {
    const char *env = getenv("YW1_DUMP_FRAMES");
    if (!env || !*env) return false;
    const char *p = env;
    while (*p) {
        char *end = NULL;
        unsigned long long target = strtoull(p, &end, 10);
        if (end == p) break;
        if (target == frame) return true;
        p = (*end == ',') ? end + 1 : end;
    }
    return false;
}

static void dump_frame_bmp(uint64_t frame) {
    if (!s_window) return;
    int pw = 1920, ph = 1080;

    int row_stride = (pw * 3 + 3) & ~3;
    size_t img_bytes = (size_t)row_stride * (size_t)ph;
    uint8_t *bgr = malloc(img_bytes);
    if (!bgr) return;
    memset(bgr, 0, img_bytes);
    gl_hle_read_virtual_fbo_rgb(pw, ph, bgr);

    const char *dir = getenv("YW1_DUMP_DIR");
    if (!dir || !*dir) dir = "F:/decode-monsterwatch/work/screenshots";
    plat_mkdir_p(dir);

    char path[1200];
    snprintf(path, sizeof path, "%s/frame_%llu.bmp", dir, (unsigned long long)frame);
    FILE *f = plat_fopen(path, "wb");
    if (f) {
        uint32_t file_size = 54u + (uint32_t)img_bytes;
        uint8_t hdr[54] = {0};
        hdr[0] = 'B';
        hdr[1] = 'M';
        hdr[2] = (uint8_t)(file_size);
        hdr[3] = (uint8_t)(file_size >> 8);
        hdr[4] = (uint8_t)(file_size >> 16);
        hdr[5] = (uint8_t)(file_size >> 24);
        hdr[10] = 54;
        hdr[14] = 40;
        hdr[18] = (uint8_t)(pw);
        hdr[19] = (uint8_t)(pw >> 8);
        hdr[20] = (uint8_t)(pw >> 16);
        hdr[21] = (uint8_t)(pw >> 24);
        hdr[22] = (uint8_t)(ph);
        hdr[23] = (uint8_t)(ph >> 8);
        hdr[24] = (uint8_t)(ph >> 16);
        hdr[25] = (uint8_t)(ph >> 24);
        hdr[26] = 1;
        hdr[28] = 24;
        fwrite(hdr, 1, sizeof hdr, f);
        fwrite(bgr, 1, img_bytes, f);
        fclose(f);
        LOG("[gl] dumped frame %llu -> %s (%dx%d)\n", (unsigned long long)frame, path, pw, ph);
    } else {
        LOG("[gl] failed to open %s for frame dump\n", path);
    }
    free(bgr);
}

static void gl_quick_exit_handler(void) {
    gl_backend_quit();
    fflush(stdout);
    fflush(stderr);
#if defined(_WIN32) || defined(__linux__)
    plat_terminate();
#endif
}

void gl_backend_window_init(void) {
    if (s_window) return;
    atexit(gl_quick_exit_handler);
    settings_load();
    input_apply_background();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) FATAL("SDL_Init: %s\n", SDL_GetError());
    input_load_user_mappings();

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    int win_w = 1280, win_h = 720;
    if (getenv("LARECOMP_WINDOW")) sscanf(getenv("LARECOMP_WINDOW"), "%dx%d", &win_w, &win_h);

    SDL_WindowFlags wflags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (getenv("LARECOMP_HIDDEN")) wflags |= SDL_WINDOW_HIDDEN;

    s_window = SDL_CreateWindow("Yo-kai Watch 1 (OpenGL 4.5)", win_w, win_h, wflags);
    if (!s_window) FATAL("SDL_CreateWindow (OPENGL): %s\n", SDL_GetError());
    rt_window_icon(s_window);
    atomic_store_explicit(&s_win_focused, (SDL_GetWindowFlags(s_window) & SDL_WINDOW_INPUT_FOCUS) != 0,
                          memory_order_relaxed);

    s_gl_ctx = SDL_GL_CreateContext(s_window);
    if (!s_gl_ctx) FATAL("SDL_GL_CreateContext (4.5 Compatibility): %s\n", SDL_GetError());
    SDL_GL_SetSwapInterval(s_swap_interval);

    // Pre-resolve all host GL procedures while the context is temporarily current on the main thread,
    // so guest eglGetProcAddress calls succeed even before eglMakeCurrent on the render thread.
    gl_hle_init_procs();

    // Unbind from main thread immediately so the guest render thread can bind it exclusively.
    if (!SDL_GL_MakeCurrent(s_window, NULL)) {
        LOG("[gl] warning: SDL_GL_MakeCurrent(NULL) failed: %s\n", SDL_GetError());
    }
    LOG("[gl] SDL3 OpenGL window ready (%dx%d), context unbound from main thread\n", win_w, win_h);
}

void gl_backend_init(void) { gl_backend_window_init(); }
void gl_backend_reset(void) { atomic_store_explicit(&s_frames, 0, memory_order_release); }
void gl_backend_guest_memory_reset(void) {}
void gl_backend_clear_window(void) {}

void gl_backend_quit(void) {
    if (atomic_flag_test_and_set(&s_quit_once)) return;
    atomic_store_explicit(&s_quitting, true, memory_order_release);
    gl_hle_log_summary();
}

SDL_Window *gl_backend_window(void) { return s_window; }
bool gl_backend_window_focused(void) { return atomic_load_explicit(&s_win_focused, memory_order_relaxed); }
void gl_backend_window_set_title(const char *title) {
    if (s_window && title && *title) SDL_SetWindowTitle(s_window, title);
}
uint64_t gl_backend_present_count(void) { return atomic_load_explicit(&s_frames, memory_order_acquire); }

void *gl_backend_get_proc_address(const char *name) {
    if (!name || !*name) return NULL;
    void *p = (void *)SDL_GL_GetProcAddress(name);
#ifdef _WIN32
    if (!p) {
        static HMODULE gl32;
        if (!gl32) gl32 = GetModuleHandleA("opengl32.dll");
        if (!gl32) gl32 = LoadLibraryA("opengl32.dll");
        if (gl32) p = (void *)GetProcAddress(gl32, name);
    }
#endif
    return p;
}

bool gl_backend_make_current(bool make_null) {
    if (!s_window || !s_gl_ctx) {
        LOG("[gl] gl_backend_make_current called before window/context init\n");
        return false;
    }
    if (make_null) {
        if (!SDL_GL_MakeCurrent(s_window, NULL)) {
            LOG("[gl] SDL_GL_MakeCurrent(NULL) failed: %s\n", SDL_GetError());
            return false;
        }
        return true;
    }
    if (!SDL_GL_MakeCurrent(s_window, s_gl_ctx)) {
        LOG("[gl] SDL_GL_MakeCurrent(ctx) failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_SetSwapInterval(s_swap_interval);
    gl_hle_init_procs();
    gl_hle_ensure_virtual_fbo();
    return true;
}

void gl_backend_set_swap_interval(int interval) {
    s_swap_interval = interval;
    if (s_window && SDL_GL_GetCurrentContext()) SDL_GL_SetSwapInterval(interval);
}

void gl_backend_swap_buffers(void) {
    if (!s_window || atomic_load_explicit(&s_quitting, memory_order_acquire)) return;
    uint64_t frame = atomic_fetch_add_explicit(&s_frames, 1, memory_order_acq_rel) + 1;
    if (frame <= 10 || frame % 60 == 0) {
        LOG("[gl] eglSwapBuffers frame=%llu\n", (unsigned long long)frame);
    }
    gl_hle_check_post_swap_errors(frame);
    if (frame == 10 || frame % 120 == 0) gl_hle_log_summary();
    if (frame_should_dump(frame)) dump_frame_bmp(frame);
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(s_window, &pw, &ph);
    gl_hle_blit_virtual_fbo(pw, ph);
    SDL_GL_SwapWindow(s_window);
    extern void input_frame(uint64_t frame);
    input_frame(frame);
}

int gl_backend_run(void) {
    gl_backend_window_init();
    for (;;) {
        if (rt_reboot_requested()) return rt_reboot_requested();
        SDL_Event e;
        while (SDL_WaitEventTimeout(&e, 16)) {
            input_rumble_poll();
            if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
                atomic_store_explicit(&s_win_focused, true, memory_order_relaxed);
            else if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST || e.type == SDL_EVENT_WINDOW_MINIMIZED)
                atomic_store_explicit(&s_win_focused, false, memory_order_relaxed);
            else if (e.type == SDL_EVENT_WINDOW_RESTORED)
                atomic_store_explicit(&s_win_focused, (SDL_GetWindowFlags(s_window) & SDL_WINDOW_INPUT_FOCUS) != 0,
                                      memory_order_relaxed);
            if (e.type == SDL_EVENT_QUIT) {
                input_rumble_stop();
                input_led_stop();
                LOG("[gl] window closed\n");
                gl_backend_quit();
                exit(0);
            }
            if (input_user_blocked(&e)) continue;
            input_cursor_event(&e);
            input_handle_event(&e);
        }
        input_rumble_poll();
        input_led_poll();
    }
}
