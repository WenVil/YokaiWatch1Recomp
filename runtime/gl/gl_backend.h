#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// True when the active target runs through the native OpenGL 4.5 / EGL HLE backend (vyw1_* or YW1_GL_BACKEND=1).
bool gl_backend_is_active(void);

// Host window / GL context lifecycle & event pump (main thread).
void gl_backend_window_init(void);
int gl_backend_run(void);
void gl_backend_init(void);
void gl_backend_reset(void);
void gl_backend_guest_memory_reset(void);
void gl_backend_clear_window(void);
void gl_backend_quit(void);

SDL_Window *gl_backend_window(void);
void *gl_backend_context(void);
bool gl_backend_window_focused(void);
void gl_backend_window_set_title(const char *title);
void gl_backend_get_cached_metrics(int *win_w, int *win_h, int *pix_w, int *pix_h, float *density);
float gl_backend_display_scale(void);
void gl_backend_refresh_metrics(void);
void gl_backend_apply_window_settings(void);
void gl_backend_apply_pending_vsync(void);
void gl_backend_set_loading(bool loading);
bool gl_backend_is_loading(void);
uint64_t gl_backend_present_count(void);
bool gl_backend_60fps_supported(void);
bool gl_backend_fps_locked_by_env(void);
bool gl_backend_effective_60fps(void);

// EGL -> SDL3 GL bridge (called from guest render thread).
bool gl_backend_make_current(bool make_null);
void gl_backend_swap_buffers(void);
void gl_backend_set_swap_interval(int interval);
void *gl_backend_get_proc_address(const char *name);

// Input hooks used by GL swap/quit.
void input_frame(uint64_t frame);
bool input_should_stop(uint64_t frame);
void input_shutdown(void);

// Implemented in gl_hle.c.
void gl_hle_init_procs(void);
uint64_t gl_hle_lookup_trampoline(const char *name);
void gl_hle_ensure_virtual_fbo(void);
void gl_hle_get_virtual_fbo_size(int *w_out, int *h_out);
int gl_hle_active_res_scale_pct(void);
void gl_hle_blit_virtual_fbo(int win_w, int win_h);
void gl_hle_read_virtual_fbo_rgb(int w, int h, uint8_t *bgr_out);
void gl_hle_check_post_swap_errors(uint64_t frame_num);
void gl_hle_record_gl_error(unsigned err, uint64_t frame_num);
void gl_hle_get_stats(uint64_t *draws_out, uint64_t *gl_errs_out, int *shaders_ok_out, int *shaders_fail_out);
void gl_hle_get_renderer_strings(const char **vendor_out, const char **renderer_out, const char **version_out);
void gl_hle_log_summary(void);
void gl_hle_get_viewport_rect(int *vx, int *vy, int *vw, int *vh, int *ww, int *wh);
void gl_hle_read_backbuffer_rgb(int w, int h, uint8_t *bgr_out);

#ifdef __cplusplus
}
#endif
