#pragma once
#include <SDL3/SDL_opengl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Controlled exclusively by YW1_GL_TRACE_HOTKEY=1. When disabled, all hooks are fast no-ops.
bool gl_trace_hotkey_enabled(void);
bool gl_trace_is_active(void);

// Called on main thread when user presses F9. Arms capture for the next complete frame.
void gl_trace_request(void);

// Optional glGetError stashing so per-call error checks in trace mode never swallow guest glGetError().
GLenum gl_trace_pop_stashed_error(void);
void gl_trace_record_error_fn(GLenum err);

// Object / shader / epa tracking active whenever YW1_GL_TRACE_HOTKEY=1.
void gl_trace_on_epa_miss(const char *name);
void gl_trace_on_create_shader(GLenum type, GLuint shader);
void gl_trace_on_delete_shader(GLuint shader);
void gl_trace_on_shader_source(GLuint shader, const char *full_src, size_t len,
                               bool has_fc, bool has_ts, bool has_tf, bool has_dx, bool has_gs);
void gl_trace_on_compile_shader(GLuint shader, GLint ok, const char *info_log);
void gl_trace_on_create_program(GLuint prog);
void gl_trace_on_delete_program(GLuint prog);
void gl_trace_on_attach_shader(GLuint prog, GLuint shader);
void gl_trace_on_link_program(GLuint prog, GLint ok, const char *info_log,
                              int n_samplers, const char (*sampler_names)[64],
                              const GLenum *sampler_types, const GLint *sampler_locs);

void gl_trace_on_tex_image_2d(GLuint tex_id, GLenum target, GLint int_fmt, GLsizei guest_w, GLsizei guest_h,
                              GLsizei real_w, GLsizei real_h, bool scaled, GLenum err);
void gl_trace_on_rbo_storage(GLuint rbo_id, GLenum fmt, GLsizei guest_w, GLsizei guest_h,
                             GLsizei real_w, GLsizei real_h, bool scaled, GLenum err);
void gl_trace_on_fbo_attach(GLuint guest_fbo, GLenum attachment, bool is_rbo, GLuint obj_id, GLenum err);
void gl_trace_on_delete_fbo(GLuint fbo);
void gl_trace_on_copy_tex_sub_image_2d(GLuint tex_id, GLuint read_fbo, GLenum target, GLint level,
                                       GLint xoff, GLint yoff, GLint x, GLint y, GLsizei w, GLsizei h,
                                       bool read_fbo_scaled, GLenum err);

// Per-frame call logging (no-op unless gl_trace_is_active()).
void gl_trace_log_bind_fbo(GLenum target, GLuint guest_fb, GLuint real_fb, bool draw_scaled,
                           GLuint vfbo_id, int vfbo_w, int vfbo_h, int res_scale_pct,
                           GLuint vfbo_color, GLuint vfbo_depth, GLenum err);
void gl_trace_log_viewport(GLint gx, GLint gy, GLsizei gw, GLsizei gh,
                           GLint rx, GLint ry, GLsizei rw, GLsizei rh,
                           bool scaled, GLuint draw_fbo, GLenum err);
void gl_trace_log_scissor(GLint gx, GLint gy, GLsizei gw, GLsizei gh,
                          GLint rx, GLint ry, GLsizei rw, GLsizei rh,
                          bool scaled, GLuint draw_fbo, GLenum err);
void gl_trace_log_enable_cap(GLenum cap, bool enable, GLenum err);
void gl_trace_log_clear(GLbitfield mask, GLenum err);
void gl_trace_log_clear_buffer(const char *kind, GLenum buffer, GLint drawbuffer, GLenum err);
void gl_trace_log_clear_buffer_f(const char *kind, GLenum buffer, GLint drawbuffer, const GLfloat *val, GLenum err);
void gl_trace_log_clear_buffer_i(const char *kind, GLenum buffer, GLint drawbuffer, const GLint *val, GLenum err);
void gl_trace_log_clear_buffer_u(const char *kind, GLenum buffer, GLint drawbuffer, const GLuint *val, GLenum err);
void gl_trace_log_clear_buffer_fi(GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil, GLenum err);
void gl_trace_log_clear_color(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void gl_trace_log_clear_depth(GLfloat d);
void gl_trace_log_clear_stencil(GLint s);
void gl_trace_log_color_mask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
void gl_trace_log_depth_mask(GLboolean flag);
void gl_trace_log_depth_func(GLenum func);
void gl_trace_log_stencil_func(GLenum func, GLint ref, GLuint mask);
void gl_trace_log_stencil_op(GLenum sfail, GLenum dpfail, GLenum dppass);
void gl_trace_log_stencil_mask(GLuint mask);
void gl_trace_log_stencil_func_sep(GLenum face, GLenum func, GLint ref, GLuint mask);
void gl_trace_log_stencil_op_sep(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass);
void gl_trace_log_stencil_mask_sep(GLenum face, GLuint mask);
void gl_trace_log_copy_tex_image_2d(GLuint tex_id, GLuint read_fbo, GLenum target, GLint level,
                                    GLenum int_fmt, GLint x, GLint y, GLsizei w, GLsizei h,
                                    bool read_fbo_scaled, GLenum err);
void gl_trace_log_blit_framebuffer(GLuint read_fbo, GLuint draw_fbo,
                                   GLint sx0, GLint sy0, GLint sx1, GLint sy1,
                                   GLint dx0, GLint dy0, GLint dx1, GLint dy1,
                                   GLbitfield mask, GLenum filter, GLenum err);
void gl_trace_log_blend_eq(GLenum rgb, GLenum alpha);
void gl_trace_log_blend_func(GLenum src_rgb, GLenum dst_rgb, GLenum src_a, GLenum dst_a);
void gl_trace_log_use_program(GLuint prog, GLenum err);
void gl_trace_log_active_texture(GLenum tex_unit);
void gl_trace_log_bind_texture(GLenum target, GLuint tex, GLenum err);
void gl_trace_log_draw_arrays(GLenum mode, GLint first, GLsizei count, GLuint draw_fbo, GLenum err);
void gl_trace_log_draw_elements(GLenum mode, GLsizei count, GLenum type, const void *indices,
                                GLuint draw_fbo, GLenum err);

void gl_trace_log_blit_vfbo(GLuint vfbo_id, int src_w, int src_h, int dx, int dy, int dw, int dh);
const char *gl_trace_dir_path(void);
uint64_t gl_trace_current_frame(void);
void gl_trace_on_frame_swap_end(uint64_t just_finished_frame, int res_scale_pct, int vfbo_w, int vfbo_h,
                                GLuint draw_fbo, GLuint read_fbo, GLuint vfbo_id,
                                GLuint vfbo_color, GLuint vfbo_depth);
void gl_trace_reset(void);

#ifdef __cplusplus
}
#endif
