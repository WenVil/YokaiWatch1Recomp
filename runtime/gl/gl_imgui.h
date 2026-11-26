// Shared Dear ImGui OpenGL 3.2+ (#version 150) glue for both Stage A (launcher on main thread)
// and Stage B (in-game overlay / HUD / OSK / loading splash on guest render thread).
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef union SDL_Event SDL_Event;

#ifdef __cplusplus
extern "C" {
#endif

// ---- Stage A (Main Thread: Launcher + Pre-Game Loading) ----
bool gl_imgui_launcher_init(void *sdl_window, void *gl_context);
void gl_imgui_launcher_shutdown(void);
bool gl_imgui_launcher_process_event(const SDL_Event *e);
void gl_imgui_launcher_begin_frame(void);
void gl_imgui_launcher_end_frame(int pix_w, int pix_h);

// ---- Stage B (Guest Render Thread: Overlay / HUD / OSK / Stage-B Loading) ----
bool gl_imgui_overlay_init(void);
void gl_imgui_overlay_shutdown(void);
void gl_imgui_overlay_new_frame(void);
void gl_imgui_overlay_render_draw_data(void *draw_data, int pix_w, int pix_h, uint64_t frame_num);

// ---- Shared Helpers ----
// Rebuilds the active ImGuiContext's OpenGL font atlas texture. Caller must hold the current GL context.
bool gl_imgui_fonts_rebuild(void);
// Requests display/window/vsync re-application cleanly on the main/GL threads.
void ui_request_window_apply(void);
bool gl_imgui_consume_window_apply(void);
// Called from gl_backend_swap_buffers() on the guest render thread right after gl_hle_blit_virtual_fbo().
void gl_imgui_render_in_game_overlay(uint64_t frame_num, int win_w, int win_h, int pix_w, int pix_h, float pixel_density);
// Stage B overlay error counter.
uint64_t gl_imgui_gl_error_count(void);
void gl_imgui_overlay_log_summary(void);

#ifdef __cplusplus
}
#endif
