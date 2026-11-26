// In-game overlay menu (Dear ImGui + OpenGL 3.2+). Open with F1 / Esc, or Minus+Plus (Back+Start) on a controller.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef union SDL_Event SDL_Event;

// Main (UI) thread: returns true if the event was consumed by the menu (the game must not see it).
bool menu_handle_event(const SDL_Event *e);
bool menu_is_open(void);
// Main thread: apply window requests (fullscreen) made from the menu.
void menu_apply_window_requests(void *sdl_window);

// Render thread (guest OpenGL context active): prepare/render the overlay onto FBO 0 (no-op while closed).
bool menu_prepare_frame(uint32_t target_h, bool loading);
void menu_render_opengl(int win_w, int win_h, int pix_w, int pix_h, float pixel_density);
void menu_set_open_for_test(bool open);
void menu_set_tab_for_test(int tab_idx);

#ifdef __cplusplus
}
#endif
