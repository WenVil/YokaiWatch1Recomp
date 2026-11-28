// OpenGL 4.5 Compatibility HLE thunks (all 90 closed gl* functions + diagnostics & ASTC decode fallback).
#include <SDL3/SDL_opengl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../cpu.h"
#include "../hle.h"
#include "astc.h"
#include "../platform.h"
#include "../rt.h"
#include "../settings.h"
#include "../simd.h"
#include "gl_backend.h"
#include "gl_imgui.h"
#include "gl_trace.h"

extern void ipc_get_telemetry(uint64_t *svc_misses_out, uint64_t *stub_cmds_out, uint64_t *am_cmds_out,
                              char *svcs_buf, size_t svcs_cap);

#ifndef APIENTRY
#define APIENTRY
#endif

// ---- OpenGL enums used in diagnostics / ASTC fallback
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_INFO_LOG_LENGTH
#define GL_INFO_LOG_LENGTH 0x8B84
#endif
#ifndef GL_SHADING_LANGUAGE_VERSION
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#endif
#ifndef GL_NUM_EXTENSIONS
#define GL_NUM_EXTENSIONS 0x821D
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_READ_FRAMEBUFFER_BINDING
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#endif
#ifndef GL_PACK_ALIGNMENT
#define GL_PACK_ALIGNMENT 0x0D05
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER 0x8D41
#endif
#ifndef GL_RENDERBUFFER_BINDING
#define GL_RENDERBUFFER_BINDING 0x8CA7
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif
#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT 0x8D00
#endif
#ifndef GL_STENCIL_ATTACHMENT
#define GL_STENCIL_ATTACHMENT 0x8D20
#endif
#ifndef GL_COLOR
#define GL_COLOR 0x1800
#endif
#ifndef GL_DEPTH
#define GL_DEPTH 0x1801
#endif
#ifndef GL_STENCIL
#define GL_STENCIL 0x1802
#endif
#ifndef GL_DEPTH_STENCIL
#define GL_DEPTH_STENCIL 0x84F9
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_BACK_LEFT
#define GL_BACK_LEFT 0x0402
#endif
#ifndef GL_SCISSOR_TEST
#define GL_SCISSOR_TEST 0x0C11
#endif
#ifndef GL_STENCIL_TEST
#define GL_STENCIL_TEST 0x0B90
#endif
#ifndef GL_ALWAYS
#define GL_ALWAYS 0x0207
#endif
#ifndef GL_INCR
#define GL_INCR 0x1E02
#endif
#ifndef GL_KEEP
#define GL_KEEP 0x1E00
#endif
#ifndef GL_FRONT
#define GL_FRONT 0x0404
#endif
#ifndef GL_BACK
#define GL_BACK 0x0405
#endif
#ifndef GL_FRONT_AND_BACK
#define GL_FRONT_AND_BACK 0x0408
#endif
#ifndef GL_STENCIL_BUFFER_BIT
#define GL_STENCIL_BUFFER_BIT 0x00000400
#endif
#ifndef GL_COLOR_CLEAR_VALUE
#define GL_COLOR_CLEAR_VALUE 0x0C22
#endif
#ifndef GL_COLOR_WRITEMASK
#define GL_COLOR_WRITEMASK 0x0C23
#endif
#ifndef GL_READ_BUFFER
#define GL_READ_BUFFER 0x0C02
#endif
#ifndef GL_VIEWPORT
#define GL_VIEWPORT 0x0BA2
#endif
#ifndef GL_TEXTURE_BINDING_2D
#define GL_TEXTURE_BINDING_2D 0x8069
#endif

#define VFBO_BASE_W 1920
#define VFBO_BASE_H 1080
#ifndef GL_BGR_EXT
#define GL_BGR_EXT 0x80E0
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif
#ifndef GL_FLOAT
#define GL_FLOAT 0x1406
#endif
#ifndef GL_ACTIVE_UNIFORMS
#define GL_ACTIVE_UNIFORMS 0x8B86
#endif

// ---- Explicit APIENTRY typedefs for all closed GL functions + helpers
typedef void(APIENTRY *PFN_glActiveTexture)(GLenum);
typedef void(APIENTRY *PFN_glAttachShader)(GLuint, GLuint);
typedef void(APIENTRY *PFN_glBindBuffer)(GLenum, GLuint);
typedef void(APIENTRY *PFN_glBindFramebuffer)(GLenum, GLuint);
typedef void(APIENTRY *PFN_glBindRenderbuffer)(GLenum, GLuint);
typedef void(APIENTRY *PFN_glBindTexture)(GLenum, GLuint);
typedef void(APIENTRY *PFN_glBlendColor)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void(APIENTRY *PFN_glBlendEquation)(GLenum);
typedef void(APIENTRY *PFN_glBlendEquationSeparate)(GLenum, GLenum);
typedef void(APIENTRY *PFN_glBlendFunc)(GLenum, GLenum);
typedef void(APIENTRY *PFN_glBlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
typedef void(APIENTRY *PFN_glBufferData)(GLenum, ptrdiff_t, const void *, GLenum);
typedef void(APIENTRY *PFN_glClearBufferfi)(GLenum, GLint, GLfloat, GLint);
typedef void(APIENTRY *PFN_glClearBufferfv)(GLenum, GLint, const GLfloat *);
typedef void(APIENTRY *PFN_glClearBufferiv)(GLenum, GLint, const GLint *);
typedef void(APIENTRY *PFN_glClearBufferuiv)(GLenum, GLint, const GLuint *);
typedef void(APIENTRY *PFN_glColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
typedef void(APIENTRY *PFN_glCompileShader)(GLuint);
typedef void(APIENTRY *PFN_glCompressedTexImage2D)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *);
typedef void(APIENTRY *PFN_glCopyTexImage2D)(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint);
typedef void(APIENTRY *PFN_glCopyTexSubImage2D)(GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
typedef GLuint(APIENTRY *PFN_glCreateProgram)(void);
typedef GLuint(APIENTRY *PFN_glCreateShader)(GLenum);
typedef void(APIENTRY *PFN_glCullFace)(GLenum);
typedef void(APIENTRY *PFN_glDeleteBuffers)(GLsizei, const GLuint *);
typedef void(APIENTRY *PFN_glDeleteFramebuffers)(GLsizei, const GLuint *);
typedef void(APIENTRY *PFN_glDeleteProgram)(GLuint);
typedef void(APIENTRY *PFN_glDeleteRenderbuffers)(GLsizei, const GLuint *);
typedef void(APIENTRY *PFN_glDeleteShader)(GLuint);
typedef void(APIENTRY *PFN_glDeleteTextures)(GLsizei, const GLuint *);
typedef void(APIENTRY *PFN_glDepthFunc)(GLenum);
typedef void(APIENTRY *PFN_glDepthMask)(GLboolean);
typedef void(APIENTRY *PFN_glDepthRange)(GLdouble, GLdouble);
typedef void(APIENTRY *PFN_glDepthRangef)(GLfloat, GLfloat);
typedef void(APIENTRY *PFN_glDisable)(GLenum);
typedef void(APIENTRY *PFN_glDisableVertexAttribArray)(GLuint);
typedef void(APIENTRY *PFN_glDrawArrays)(GLenum, GLint, GLsizei);
typedef void(APIENTRY *PFN_glDrawBuffers)(GLsizei, const GLenum *);
typedef void(APIENTRY *PFN_glDrawElements)(GLenum, GLsizei, GLenum, const void *);
typedef void(APIENTRY *PFN_glEnable)(GLenum);
typedef void(APIENTRY *PFN_glEnableVertexAttribArray)(GLuint);
typedef void(APIENTRY *PFN_glFinish)(void);
typedef void(APIENTRY *PFN_glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
typedef void(APIENTRY *PFN_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void(APIENTRY *PFN_glFrontFace)(GLenum);
typedef void(APIENTRY *PFN_glGenBuffers)(GLsizei, GLuint *);
typedef void(APIENTRY *PFN_glGenFramebuffers)(GLsizei, GLuint *);
typedef void(APIENTRY *PFN_glGenRenderbuffers)(GLsizei, GLuint *);
typedef void(APIENTRY *PFN_glGenTextures)(GLsizei, GLuint *);
typedef void(APIENTRY *PFN_glGenerateMipmap)(GLenum);
typedef void(APIENTRY *PFN_glGetActiveAttrib)(GLuint, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLchar *);
typedef void(APIENTRY *PFN_glGetActiveUniform)(GLuint, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLchar *);
typedef GLint(APIENTRY *PFN_glGetAttribLocation)(GLuint, const GLchar *);
typedef void(APIENTRY *PFN_glGetBooleanv)(GLenum, GLboolean *);
typedef void(APIENTRY *PFN_glGetIntegerv)(GLenum, GLint *);
typedef void(APIENTRY *PFN_glGetProgramiv)(GLuint, GLenum, GLint *);
typedef void(APIENTRY *PFN_glGetShaderPrecisionFormat)(GLenum, GLenum, GLint *, GLint *);
typedef void(APIENTRY *PFN_glGetShaderiv)(GLuint, GLenum, GLint *);
typedef const GLubyte *(APIENTRY *PFN_glGetString)(GLenum);
typedef GLint(APIENTRY *PFN_glGetUniformLocation)(GLuint, const GLchar *);
typedef void(APIENTRY *PFN_glLinkProgram)(GLuint);
typedef void(APIENTRY *PFN_glPolygonOffset)(GLfloat, GLfloat);
typedef void(APIENTRY *PFN_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
typedef void(APIENTRY *PFN_glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
typedef void(APIENTRY *PFN_glScissor)(GLint, GLint, GLsizei, GLsizei);
typedef void(APIENTRY *PFN_glShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
typedef void(APIENTRY *PFN_glStencilFunc)(GLenum, GLint, GLuint);
typedef void(APIENTRY *PFN_glStencilFuncSeparate)(GLenum, GLenum, GLint, GLuint);
typedef void(APIENTRY *PFN_glStencilMask)(GLuint);
typedef void(APIENTRY *PFN_glStencilMaskSeparate)(GLenum, GLuint);
typedef void(APIENTRY *PFN_glStencilOp)(GLenum, GLenum, GLenum);
typedef void(APIENTRY *PFN_glStencilOpSeparate)(GLenum, GLenum, GLenum, GLenum);
typedef void(APIENTRY *PFN_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
typedef void(APIENTRY *PFN_glTexParameterf)(GLenum, GLenum, GLfloat);
typedef void(APIENTRY *PFN_glTexParameterfv)(GLenum, GLenum, const GLfloat *);
typedef void(APIENTRY *PFN_glTexParameteri)(GLenum, GLenum, GLint);
typedef void(APIENTRY *PFN_glTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);
typedef void(APIENTRY *PFN_glUniform1f)(GLint, GLfloat);
typedef void(APIENTRY *PFN_glUniform1fv)(GLint, GLsizei, const GLfloat *);
typedef void(APIENTRY *PFN_glUniform1i)(GLint, GLint);
typedef void(APIENTRY *PFN_glUniform1iv)(GLint, GLsizei, const GLint *);
typedef void(APIENTRY *PFN_glUniform2f)(GLint, GLfloat, GLfloat);
typedef void(APIENTRY *PFN_glUniform2fv)(GLint, GLsizei, const GLfloat *);
typedef void(APIENTRY *PFN_glUniform2i)(GLint, GLint, GLint);
typedef void(APIENTRY *PFN_glUniform2iv)(GLint, GLsizei, const GLint *);
typedef void(APIENTRY *PFN_glUniform3f)(GLint, GLfloat, GLfloat, GLfloat);
typedef void(APIENTRY *PFN_glUniform3fv)(GLint, GLsizei, const GLfloat *);
typedef void(APIENTRY *PFN_glUniform3i)(GLint, GLint, GLint, GLint);
typedef void(APIENTRY *PFN_glUniform3iv)(GLint, GLsizei, const GLint *);
typedef void(APIENTRY *PFN_glUniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void(APIENTRY *PFN_glUniform4fv)(GLint, GLsizei, const GLfloat *);
typedef void(APIENTRY *PFN_glUniform4i)(GLint, GLint, GLint, GLint, GLint);
typedef void(APIENTRY *PFN_glUniform4iv)(GLint, GLsizei, const GLint *);
typedef void(APIENTRY *PFN_glUniformMatrix2fv)(GLint, GLsizei, GLboolean, const GLfloat *);
typedef void(APIENTRY *PFN_glUniformMatrix3fv)(GLint, GLsizei, GLboolean, const GLfloat *);
typedef void(APIENTRY *PFN_glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat *);
typedef void(APIENTRY *PFN_glUseProgram)(GLuint);
typedef void(APIENTRY *PFN_glVertexAttrib1fv)(GLuint, const GLfloat *);
typedef void(APIENTRY *PFN_glVertexAttrib2fv)(GLuint, const GLfloat *);
typedef void(APIENTRY *PFN_glVertexAttrib3fv)(GLuint, const GLfloat *);
typedef void(APIENTRY *PFN_glVertexAttrib4fv)(GLuint, const GLfloat *);
typedef void(APIENTRY *PFN_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
typedef void(APIENTRY *PFN_glViewport)(GLint, GLint, GLsizei, GLsizei);

// Adjacent extra helpers & diagnostics
typedef GLenum(APIENTRY *PFN_glGetError)(void);
typedef void(APIENTRY *PFN_glFlush)(void);
typedef void(APIENTRY *PFN_glClear)(GLbitfield);
typedef void(APIENTRY *PFN_glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void(APIENTRY *PFN_glClearDepth)(GLdouble);
typedef void(APIENTRY *PFN_glClearDepthf)(GLfloat);
typedef void(APIENTRY *PFN_glClearStencil)(GLint);
typedef const GLubyte *(APIENTRY *PFN_glGetStringi)(GLenum, GLuint);
typedef void(APIENTRY *PFN_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void(APIENTRY *PFN_glGetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void(APIENTRY *PFN_glGetFloatv)(GLenum, GLfloat *);
typedef void(APIENTRY *PFN_glPixelStorei)(GLenum, GLint);
typedef void(APIENTRY *PFN_glDrawBuffer)(GLenum);
typedef void(APIENTRY *PFN_glReadBuffer)(GLenum);
typedef GLenum(APIENTRY *PFN_glCheckFramebufferStatus)(GLenum);
typedef void(APIENTRY *PFN_glBlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
typedef GLboolean(APIENTRY *PFN_glIsEnabled)(GLenum);
typedef void(APIENTRY *PFN_glGetUniformiv)(GLuint, GLint, GLint *);
typedef void(APIENTRY *PFN_glGetUniformfv)(GLuint, GLint, GLfloat *);

#define GL_ALL_PROCS(X) \
    X(glActiveTexture) \
    X(glAttachShader) \
    X(glBindBuffer) \
    X(glBindFramebuffer) \
    X(glBindRenderbuffer) \
    X(glBindTexture) \
    X(glBlendColor) \
    X(glBlendEquation) \
    X(glBlendEquationSeparate) \
    X(glBlendFunc) \
    X(glBlendFuncSeparate) \
    X(glBufferData) \
    X(glClearBufferfi) \
    X(glClearBufferfv) \
    X(glClearBufferiv) \
    X(glClearBufferuiv) \
    X(glColorMask) \
    X(glCompileShader) \
    X(glCompressedTexImage2D) \
    X(glCopyTexImage2D) \
    X(glCopyTexSubImage2D) \
    X(glCreateProgram) \
    X(glCreateShader) \
    X(glCullFace) \
    X(glDeleteBuffers) \
    X(glDeleteFramebuffers) \
    X(glDeleteProgram) \
    X(glDeleteRenderbuffers) \
    X(glDeleteShader) \
    X(glDeleteTextures) \
    X(glDepthFunc) \
    X(glDepthMask) \
    X(glDepthRange) \
    X(glDepthRangef) \
    X(glDisable) \
    X(glDisableVertexAttribArray) \
    X(glDrawArrays) \
    X(glDrawBuffers) \
    X(glDrawElements) \
    X(glEnable) \
    X(glEnableVertexAttribArray) \
    X(glFinish) \
    X(glFramebufferRenderbuffer) \
    X(glFramebufferTexture2D) \
    X(glFrontFace) \
    X(glGenBuffers) \
    X(glGenFramebuffers) \
    X(glGenRenderbuffers) \
    X(glGenTextures) \
    X(glGenerateMipmap) \
    X(glGetActiveAttrib) \
    X(glGetActiveUniform) \
    X(glGetAttribLocation) \
    X(glGetBooleanv) \
    X(glGetIntegerv) \
    X(glGetProgramiv) \
    X(glGetShaderPrecisionFormat) \
    X(glGetShaderiv) \
    X(glGetString) \
    X(glGetUniformLocation) \
    X(glLinkProgram) \
    X(glPolygonOffset) \
    X(glReadPixels) \
    X(glRenderbufferStorage) \
    X(glScissor) \
    X(glShaderSource) \
    X(glStencilFunc) \
    X(glStencilFuncSeparate) \
    X(glStencilMask) \
    X(glStencilMaskSeparate) \
    X(glStencilOp) \
    X(glStencilOpSeparate) \
    X(glTexImage2D) \
    X(glTexParameterf) \
    X(glTexParameterfv) \
    X(glTexParameteri) \
    X(glTexSubImage2D) \
    X(glUniform1f) \
    X(glUniform1fv) \
    X(glUniform1i) \
    X(glUniform1iv) \
    X(glUniform2f) \
    X(glUniform2fv) \
    X(glUniform2i) \
    X(glUniform2iv) \
    X(glUniform3f) \
    X(glUniform3fv) \
    X(glUniform3i) \
    X(glUniform3iv) \
    X(glUniform4f) \
    X(glUniform4fv) \
    X(glUniform4i) \
    X(glUniform4iv) \
    X(glUniformMatrix2fv) \
    X(glUniformMatrix3fv) \
    X(glUniformMatrix4fv) \
    X(glUseProgram) \
    X(glVertexAttrib1fv) \
    X(glVertexAttrib2fv) \
    X(glVertexAttrib3fv) \
    X(glVertexAttrib4fv) \
    X(glVertexAttribPointer) \
    X(glViewport) \
    X(glGetError) \
    X(glFlush) \
    X(glClear) \
    X(glClearColor) \
    X(glClearDepth) \
    X(glClearDepthf) \
    X(glClearStencil) \
    X(glGetStringi) \
    X(glGetShaderInfoLog) \
    X(glGetProgramInfoLog) \
    X(glGetFloatv) \
    X(glPixelStorei) \
    X(glDrawBuffer) \
    X(glReadBuffer) \
    X(glCheckFramebufferStatus) \
    X(glBlitFramebuffer) \
    X(glIsEnabled) \
    X(glGetUniformiv) \
    X(glGetUniformfv)

static struct {
#define X(name) PFN_##name p_##name;
    GL_ALL_PROCS(X)
#undef X
} g_gl;

static pthread_mutex_t g_gl_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_gl_info_logged;

// Cached glGetString copies in HLE_HEAP
#define MAX_GL_STRINGS 16
static struct {
    GLenum name;
    uint64_t guest_ptr;
} g_str_cache[MAX_GL_STRINGS];
static int g_str_cache_n;

// Cached shader sources for compile-error telemetry
#define MAX_SHADER_SRC 512
static struct {
    GLuint id;
    char head[256];
} g_shader_src[MAX_SHADER_SRC];
static int g_shader_ok, g_shader_fail, g_prog_ok, g_prog_fail, g_shader_src_logs;

// Distinct glCompressedTexImage2D formats seen
static struct {
    GLenum fmt;
    uint32_t count;
    bool astc_sw;
    uint64_t astc_ns;
} g_comp_fmts[64];
static int g_ncomp_fmts;

// Texture / Renderbuffer dimensions for offscreen FBO attachment telemetry & supersampling
#define OBJ_DIM_CAP 4096
typedef struct {
    GLuint id;
    GLenum fmt;
    GLsizei w, h;
    bool scaled;
    bool used;
} ObjDimSlot;
static ObjDimSlot g_tex_dims[OBJ_DIM_CAP];
static ObjDimSlot g_rbo_dims[OBJ_DIM_CAP];
static GLuint g_cur_tex2d, g_cur_rbo;

typedef struct {
    GLuint fbo;
    bool scaled;
    bool depth_d24s8;
    bool stencil_d24s8;
    bool depth_stencil_d24s8;
    bool used;
} FboScaleSlot;
static FboScaleSlot g_fbo_scales[OBJ_DIM_CAP];

// Generic GL state tracking for D24S8 missed-stencil-clear detection & offscreen stencil-write alpha-test suppression
static GLuint g_stencil_wmask_front = 0xFFFFFFFFu;
static GLuint g_stencil_wmask_back = 0xFFFFFFFFu;
static bool g_pending_d24s8_stencil_clear = false;
static GLsizei g_cur_vp_w = 0, g_cur_vp_h = 0;
static bool g_cur_stencil_test = false;
static GLenum g_cur_stencil_func = GL_ALWAYS;
static GLenum g_cur_stencil_dppass = GL_KEEP;
static GLuint g_cur_prog = 0;
static GLuint g_atest_cached_prog = 0;
static GLint g_atest_en_loc = -1, g_atest_ref_loc = -1;
static bool g_atest_cache_valid = false;

typedef struct {
    GLsizei w, h;
    GLenum fmt;
    bool is_rbo;
    uint32_t unique_slots;
    uint64_t bind_count;
} OffscreenFboStat;
static OffscreenFboStat g_fbo_stats[64];
static int g_nfbo_stats;
static struct {
    GLuint fbo;
    GLenum attachment;
    GLsizei w, h;
    GLenum fmt;
    bool is_rbo;
} g_fbo_seen[256];
static int g_nfbo_seen;

typedef struct {
    GLsizei w, h;
    uint64_t count;
} ViewportStat;
static ViewportStat g_vp_stats[64];
static int g_nvp_stats;

// Virtual default framebuffer (1080p * res_scale) & guest FBO=0 tracking
static int g_res_scale_pct = 100;
static int g_vfbo_w = VFBO_BASE_W;
static int g_vfbo_h = VFBO_BASE_H;
static GLuint g_virtual_fbo;
static GLuint g_vfbo_color;
static GLuint g_vfbo_depth;
static GLuint g_guest_draw_fbo;
static GLuint g_guest_read_fbo;

static int g_sh_audit_total;
static int g_sh_audit_fragcoord;
static int g_sh_audit_texsize;
static int g_sh_audit_texelfetch;
static int g_sh_audit_dfdx;
static int g_sh_audit_gauss_uni;

static bool is_16_9_rt_dims(GLsizei w, GLsizei h) {
    if (w < 480 || h < 270 || w == h) return false;
    return (int64_t)w * 9 == (int64_t)h * 16;
}

static int resolve_configured_res_scale_pct(void) {
    const char *env = getenv("YW1_RES_SCALE");
    if (env && *env) {
        double v = atof(env);
        int pct = (v > 10.0) ? (int)(v + 0.5) : (int)(v * 100.0 + 0.5);
        if (pct >= 180) return 200;
        if (pct >= 125) return 150;
        return 100;
    }
    switch (g_settings.res_scale) {
    case RES_SCALE_1_5X: return 150;
    case RES_SCALE_2X:   return 200;
    case RES_SCALE_1X:
    default:             return 100;
    }
}

static void obj_dim_set_locked(ObjDimSlot *tab, GLuint id, GLenum fmt, GLsizei w, GLsizei h, bool scaled) {
    if (!id || w <= 0 || h <= 0) return;
    uint32_t idx = (uint32_t)(id * 2654435761u) & (OBJ_DIM_CAP - 1);
    for (uint32_t i = 0; i < OBJ_DIM_CAP; i++) {
        ObjDimSlot *s = &tab[(idx + i) & (OBJ_DIM_CAP - 1)];
        if (!s->used || s->id == id) {
            s->id = id;
            s->fmt = fmt;
            s->w = w;
            s->h = h;
            s->scaled = scaled;
            s->used = true;
            return;
        }
    }
}

static bool obj_dim_get_locked(const ObjDimSlot *tab, GLuint id, GLenum *fmt_out, GLsizei *w_out, GLsizei *h_out,
                               bool *scaled_out) {
    if (!id) return false;
    uint32_t idx = (uint32_t)(id * 2654435761u) & (OBJ_DIM_CAP - 1);
    for (uint32_t i = 0; i < OBJ_DIM_CAP; i++) {
        const ObjDimSlot *s = &tab[(idx + i) & (OBJ_DIM_CAP - 1)];
        if (!s->used) return false;
        if (s->id == id) {
            if (fmt_out) *fmt_out = s->fmt;
            if (w_out) *w_out = s->w;
            if (h_out) *h_out = s->h;
            if (scaled_out) *scaled_out = s->scaled;
            return true;
        }
    }
    return false;
}

static void fbo_scale_set_locked(GLuint fbo, bool scaled) {
    if (!fbo) return;
    uint32_t idx = (uint32_t)(fbo * 2654435761u) & (OBJ_DIM_CAP - 1);
    for (uint32_t i = 0; i < OBJ_DIM_CAP; i++) {
        FboScaleSlot *s = &g_fbo_scales[(idx + i) & (OBJ_DIM_CAP - 1)];
        if (!s->used || s->fbo == fbo) {
            if (!s->used) {
                s->fbo = fbo;
                s->scaled = scaled;
                s->used = true;
            } else if (scaled) {
                s->scaled = true;
            }
            return;
        }
    }
}

static void fbo_scale_clear_locked(GLuint fbo) {
    if (!fbo) return;
    uint32_t idx = (uint32_t)(fbo * 2654435761u) & (OBJ_DIM_CAP - 1);
    for (uint32_t i = 0; i < OBJ_DIM_CAP; i++) {
        FboScaleSlot *s = &g_fbo_scales[(idx + i) & (OBJ_DIM_CAP - 1)];
        if (!s->used) return;
        if (s->fbo == fbo) {
            s->scaled = false;
            s->depth_d24s8 = false;
            s->stencil_d24s8 = false;
            s->depth_stencil_d24s8 = false;
            return;
        }
    }
}

static void fbo_d24s8_set_locked(GLuint fbo, GLenum attachment, bool is_d24s8) {
    if (!fbo) return;
    if (attachment != GL_DEPTH_ATTACHMENT && attachment != GL_STENCIL_ATTACHMENT &&
        attachment != GL_DEPTH_STENCIL_ATTACHMENT) {
        return;
    }
    uint32_t idx = (uint32_t)(fbo * 2654435761u) & (OBJ_DIM_CAP - 1);
    for (uint32_t i = 0; i < OBJ_DIM_CAP; i++) {
        FboScaleSlot *s = &g_fbo_scales[(idx + i) & (OBJ_DIM_CAP - 1)];
        if (!s->used || s->fbo == fbo) {
            if (!s->used) {
                memset(s, 0, sizeof *s);
                s->fbo = fbo;
                s->used = true;
            }
            if (attachment == GL_DEPTH_ATTACHMENT) s->depth_d24s8 = is_d24s8;
            else if (attachment == GL_STENCIL_ATTACHMENT) s->stencil_d24s8 = is_d24s8;
            else {
                s->depth_stencil_d24s8 = is_d24s8;
                if (!is_d24s8) {
                    s->depth_d24s8 = false;
                    s->stencil_d24s8 = false;
                }
            }
            return;
        }
    }
}

static bool fbo_has_d24s8_locked(GLuint fbo) {
    if (!fbo) return false;
    uint32_t idx = (uint32_t)(fbo * 2654435761u) & (OBJ_DIM_CAP - 1);
    for (uint32_t i = 0; i < OBJ_DIM_CAP; i++) {
        const FboScaleSlot *s = &g_fbo_scales[(idx + i) & (OBJ_DIM_CAP - 1)];
        if (!s->used) return false;
        if (s->fbo == fbo) return s->depth_d24s8 || s->stencil_d24s8 || s->depth_stencil_d24s8;
    }
    return false;
}

static void invalidate_atest_cache(GLuint prog) {
    if (prog == 0 || prog == g_atest_cached_prog) {
        g_atest_cache_valid = false;
        g_atest_cached_prog = 0;
        g_atest_en_loc = -1;
        g_atest_ref_loc = -1;
    }
}

// Generic offscreen stencil-write suppression: avoids alpha-test discarding transparent glare pixels during lens mask writes.
static bool lens_mask_atest_suppress_begin(GLint *en_loc_out) {
    if (g_guest_draw_fbo == 0 || g_cur_vp_w != 360 || g_cur_vp_h != 202 || !g_cur_stencil_test ||
        g_cur_stencil_func != GL_ALWAYS || g_cur_stencil_dppass != GL_INCR ||
        (g_stencil_wmask_front & 1u) == 0u || g_cur_prog == 0 ||
        !g_gl.p_glGetUniformLocation || !g_gl.p_glGetUniformiv || !g_gl.p_glGetUniformfv ||
        !g_gl.p_glUniform1i) {
        return false;
    }
    if (!g_atest_cache_valid || g_atest_cached_prog != g_cur_prog) {
        g_atest_cached_prog = g_cur_prog;
        g_atest_en_loc = g_gl.p_glGetUniformLocation(g_cur_prog, "dmp_FragOperation.enableAlphaTest");
        g_atest_ref_loc = g_gl.p_glGetUniformLocation(g_cur_prog, "dmp_FragOperation.alphaRefValue");
        g_atest_cache_valid = true;
    }
    if (g_atest_en_loc < 0 || g_atest_ref_loc < 0) return false;
    GLint en_val = 0;
    GLfloat ref_val = -1.0f;
    g_gl.p_glGetUniformiv(g_cur_prog, g_atest_en_loc, &en_val);
    g_gl.p_glGetUniformfv(g_cur_prog, g_atest_ref_loc, &ref_val);
    if (en_val == 1 && ref_val == 0.0f) {
        g_gl.p_glUniform1i(g_atest_en_loc, 0);
        *en_loc_out = g_atest_en_loc;
        return true;
    }
    return false;
}

static bool fbo_is_scaled_locked(GLuint fbo) {
    if (fbo == 0) return g_res_scale_pct > 100;
    uint32_t idx = (uint32_t)(fbo * 2654435761u) & (OBJ_DIM_CAP - 1);
    for (uint32_t i = 0; i < OBJ_DIM_CAP; i++) {
        const FboScaleSlot *s = &g_fbo_scales[(idx + i) & (OBJ_DIM_CAP - 1)];
        if (!s->used) return false;
        if (s->fbo == fbo) return s->scaled;
    }
    return false;
}

static void record_offscreen_fbo(GLenum target, GLenum attachment, bool is_rbo, GLuint obj_id) {
    GLuint fbo = (target == GL_READ_FRAMEBUFFER) ? g_guest_read_fbo : g_guest_draw_fbo;
    if (fbo == 0) return;
    if (!obj_id) {
        pthread_mutex_lock(&g_gl_lock);
        fbo_d24s8_set_locked(fbo, attachment, false);
        pthread_mutex_unlock(&g_gl_lock);
        return;
    }
    pthread_mutex_lock(&g_gl_lock);
    GLenum fmt = 0;
    GLsizei w = 0, h = 0;
    bool obj_scaled = false;
    if (obj_dim_get_locked(is_rbo ? g_rbo_dims : g_tex_dims, obj_id, &fmt, &w, &h, &obj_scaled) && w > 0 && h > 0) {
        bool is_d24s8 = (fmt == GL_DEPTH24_STENCIL8 || fmt == GL_DEPTH_STENCIL);
        fbo_d24s8_set_locked(fbo, attachment, is_d24s8);
        fbo_scale_set_locked(fbo, obj_scaled);
        bool is_new_slot = true;
        for (int i = 0; i < g_nfbo_seen; i++) {
            if (g_fbo_seen[i].fbo == fbo && g_fbo_seen[i].attachment == attachment &&
                g_fbo_seen[i].w == w && g_fbo_seen[i].h == h && g_fbo_seen[i].fmt == fmt &&
                g_fbo_seen[i].is_rbo == is_rbo) {
                is_new_slot = false;
                break;
            }
        }
        if (is_new_slot && g_nfbo_seen < 256) {
            g_fbo_seen[g_nfbo_seen].fbo = fbo;
            g_fbo_seen[g_nfbo_seen].attachment = attachment;
            g_fbo_seen[g_nfbo_seen].w = w;
            g_fbo_seen[g_nfbo_seen].h = h;
            g_fbo_seen[g_nfbo_seen].fmt = fmt;
            g_fbo_seen[g_nfbo_seen].is_rbo = is_rbo;
            g_nfbo_seen++;
        }
        int sidx = -1;
        for (int i = 0; i < g_nfbo_stats; i++) {
            if (g_fbo_stats[i].w == w && g_fbo_stats[i].h == h && g_fbo_stats[i].fmt == fmt &&
                g_fbo_stats[i].is_rbo == is_rbo) {
                sidx = i;
                break;
            }
        }
        if (sidx < 0 && g_nfbo_stats < 64) {
            sidx = g_nfbo_stats++;
            g_fbo_stats[sidx].w = w;
            g_fbo_stats[sidx].h = h;
            g_fbo_stats[sidx].fmt = fmt;
            g_fbo_stats[sidx].is_rbo = is_rbo;
            g_fbo_stats[sidx].unique_slots = 0;
            g_fbo_stats[sidx].bind_count = 0;
        }
        if (sidx >= 0) {
            if (is_new_slot) g_fbo_stats[sidx].unique_slots++;
            g_fbo_stats[sidx].bind_count++;
        }
    }
    pthread_mutex_unlock(&g_gl_lock);
}

static void record_viewport_stat(GLsizei w, GLsizei h) {
    if (w < 0 || h < 0) return;
    pthread_mutex_lock(&g_gl_lock);
    int idx = -1;
    for (int i = 0; i < g_nvp_stats; i++) {
        if (g_vp_stats[i].w == w && g_vp_stats[i].h == h) { idx = i; break; }
    }
    if (idx < 0 && g_nvp_stats < 64) {
        idx = g_nvp_stats++;
        g_vp_stats[idx].w = w;
        g_vp_stats[idx].h = h;
        g_vp_stats[idx].count = 0;
    }
    if (idx >= 0) g_vp_stats[idx].count++;
    pthread_mutex_unlock(&g_gl_lock);
}

// Letterbox rect on host window for touch coordinate conversion
static int g_vp_x = 0, g_vp_y = 0, g_vp_w = 1280, g_vp_h = 720, g_win_w = 1280, g_win_h = 720;

// Lifetime glGetError & draw telemetry
static struct {
    GLenum code;
    uint64_t count;
    uint64_t first_frame;
    uint64_t last_frame;
} g_gl_err_stats[16];
static int g_gl_err_kind_n;
static uint64_t g_gl_err_total;
static uint64_t g_total_draws;
static char g_gl_vendor[128];
static char g_gl_renderer[128];
static char g_gl_version[128];

// eglGetProcAddress telemetry
static uint32_t g_epa_hits;
static uint32_t g_epa_misses;

RT_ON_RESET(gl_hle_reset) {
    pthread_mutex_lock(&g_gl_lock);
    g_str_cache_n = 0;
    memset(g_str_cache, 0, sizeof g_str_cache);
    memset(g_shader_src, 0, sizeof g_shader_src);
    g_shader_ok = g_shader_fail = g_prog_ok = g_prog_fail = g_shader_src_logs = 0;
    g_ncomp_fmts = 0;
    memset(g_comp_fmts, 0, sizeof g_comp_fmts);
    memset(g_tex_dims, 0, sizeof g_tex_dims);
    memset(g_rbo_dims, 0, sizeof g_rbo_dims);
    memset(g_fbo_scales, 0, sizeof g_fbo_scales);
    g_cur_tex2d = g_cur_rbo = 0;
    memset(g_fbo_stats, 0, sizeof g_fbo_stats);
    g_nfbo_stats = 0;
    memset(g_fbo_seen, 0, sizeof g_fbo_seen);
    g_nfbo_seen = 0;
    memset(g_vp_stats, 0, sizeof g_vp_stats);
    g_nvp_stats = 0;
    g_res_scale_pct = 100;
    g_vfbo_w = VFBO_BASE_W;
    g_vfbo_h = VFBO_BASE_H;
    g_virtual_fbo = g_vfbo_color = g_vfbo_depth = 0;
    g_guest_draw_fbo = g_guest_read_fbo = 0;
    g_stencil_wmask_front = 0xFFFFFFFFu;
    g_stencil_wmask_back = 0xFFFFFFFFu;
    g_pending_d24s8_stencil_clear = false;
    g_cur_vp_w = g_cur_vp_h = 0;
    g_cur_stencil_test = false;
    g_cur_stencil_func = GL_ALWAYS;
    g_cur_stencil_dppass = GL_KEEP;
    g_cur_prog = 0;
    invalidate_atest_cache(0);
    g_sh_audit_total = g_sh_audit_fragcoord = g_sh_audit_texsize = g_sh_audit_texelfetch = g_sh_audit_dfdx = g_sh_audit_gauss_uni = 0;
    memset(g_gl_err_stats, 0, sizeof g_gl_err_stats);
    g_gl_err_kind_n = 0;
    g_gl_err_total = 0;
    __atomic_store_n(&g_total_draws, 0, __ATOMIC_RELAXED);
    g_epa_hits = g_epa_misses = 0;
    pthread_mutex_unlock(&g_gl_lock);
    gl_trace_reset();
}

void gl_hle_init_procs(void) {
    pthread_mutex_lock(&g_gl_lock);
#define X(name) g_gl.p_##name = (PFN_##name)gl_backend_get_proc_address(#name);
    GL_ALL_PROCS(X)
#undef X
    if (!g_gl_info_logged && g_gl.p_glGetString) {
        g_gl_info_logged = true;
        const char *ven = (const char *)g_gl.p_glGetString(GL_VENDOR);
        const char *ren = (const char *)g_gl.p_glGetString(GL_RENDERER);
        const char *ver = (const char *)g_gl.p_glGetString(GL_VERSION);
        const char *glsl = (const char *)g_gl.p_glGetString(GL_SHADING_LANGUAGE_VERSION);
        snprintf(g_gl_vendor, sizeof g_gl_vendor, "%s", ven ? ven : "OpenGL");
        snprintf(g_gl_renderer, sizeof g_gl_renderer, "%s", ren ? ren : "OpenGL 4.5");
        snprintf(g_gl_version, sizeof g_gl_version, "%s", ver ? ver : "4.5");
        LOG("[gl] host driver: vendor=\"%s\" renderer=\"%s\" version=\"%s\" glsl=\"%s\"\n",
            ven ? ven : "(null)", ren ? ren : "(null)", ver ? ver : "(null)", glsl ? glsl : "(null)");
    }
    pthread_mutex_unlock(&g_gl_lock);
}

void gl_hle_ensure_virtual_fbo(void) {
    if (g_virtual_fbo != 0) {
        if (g_gl.p_glBindFramebuffer) g_gl.p_glBindFramebuffer(GL_FRAMEBUFFER, g_virtual_fbo);
        return;
    }
    g_res_scale_pct = resolve_configured_res_scale_pct();
    g_vfbo_w = (VFBO_BASE_W * g_res_scale_pct) / 100;
    g_vfbo_h = (VFBO_BASE_H * g_res_scale_pct) / 100;
    if (!g_gl.p_glGenFramebuffers || !g_gl.p_glBindFramebuffer || !g_gl.p_glGenTextures ||
        !g_gl.p_glBindTexture || !g_gl.p_glTexImage2D || !g_gl.p_glTexParameteri ||
        !g_gl.p_glFramebufferTexture2D || !g_gl.p_glGenRenderbuffers ||
        !g_gl.p_glBindRenderbuffer || !g_gl.p_glRenderbufferStorage ||
        !g_gl.p_glFramebufferRenderbuffer || !g_gl.p_glCheckFramebufferStatus) {
        LOG("[gl] ERROR: missing GL procs needed for virtual %dx%d FBO\n", g_vfbo_w, g_vfbo_h);
        return;
    }

    GLint prev_tex = 0, prev_rbo = 0;
    if (g_gl.p_glGetIntegerv) {
        g_gl.p_glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);
        g_gl.p_glGetIntegerv(GL_RENDERBUFFER_BINDING, &prev_rbo);
    }

    g_gl.p_glGenTextures(1, &g_vfbo_color);
    g_gl.p_glBindTexture(GL_TEXTURE_2D, g_vfbo_color);
    g_gl.p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_vfbo_w, g_vfbo_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    g_gl.p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    g_gl.p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    g_gl.p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    g_gl.p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    g_gl.p_glGenRenderbuffers(1, &g_vfbo_depth);
    g_gl.p_glBindRenderbuffer(GL_RENDERBUFFER, g_vfbo_depth);
    g_gl.p_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, g_vfbo_w, g_vfbo_h);

    g_gl.p_glGenFramebuffers(1, &g_virtual_fbo);
    g_gl.p_glBindFramebuffer(GL_FRAMEBUFFER, g_virtual_fbo);
    g_gl.p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_vfbo_color, 0);
    g_gl.p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, g_vfbo_depth);

    GLenum status = g_gl.p_glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOG("[gl] ERROR: virtual %dx%d FBO incomplete (status=0x%04x)\n", g_vfbo_w, g_vfbo_h, (unsigned)status);
    } else {
        LOG("[gl] virtual %dx%d default FBO #%u ready (res_scale=%.1fx color_tex=%u RGBA8 GL_LINEAR, ds_rbo=%u D24S8)\n",
            g_vfbo_w, g_vfbo_h, g_virtual_fbo, (double)g_res_scale_pct / 100.0, g_vfbo_color, g_vfbo_depth);
    }

    g_gl.p_glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex);
    g_gl.p_glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)prev_rbo);
    if (g_gl.p_glViewport) g_gl.p_glViewport(0, 0, g_vfbo_w, g_vfbo_h);
    g_guest_draw_fbo = 0;
    g_guest_read_fbo = 0;
}

void gl_hle_get_virtual_fbo_size(int *w_out, int *h_out) {
    if (w_out) *w_out = g_vfbo_w > 0 ? g_vfbo_w : VFBO_BASE_W;
    if (h_out) *h_out = g_vfbo_h > 0 ? g_vfbo_h : VFBO_BASE_H;
}

int gl_hle_active_res_scale_pct(void) { return g_res_scale_pct; }

void gl_hle_blit_virtual_fbo(int win_w, int win_h) {
    if (!g_virtual_fbo || win_w <= 0 || win_h <= 0 || !g_gl.p_glBlitFramebuffer ||
        !g_gl.p_glBindFramebuffer || !g_gl.p_glGetIntegerv) {
        return;
    }

    int dst_x = 0, dst_y = 0, dst_w = win_w, dst_h = win_h;
    if (g_settings.aspect_mode != ASPECT_MODE_STRETCH) {
        dst_h = (win_w * g_vfbo_h + g_vfbo_w / 2) / g_vfbo_w;
        if (dst_h > win_h) {
            dst_h = win_h;
            dst_w = (win_h * g_vfbo_w + g_vfbo_h / 2) / g_vfbo_h;
        }
        dst_x = (win_w - dst_w) / 2;
        dst_y = (win_h - dst_h) / 2;
    }

    pthread_mutex_lock(&g_gl_lock);
    g_vp_x = dst_x;
    g_vp_y = dst_y;
    g_vp_w = dst_w;
    g_vp_h = dst_h;
    g_win_w = win_w;
    g_win_h = win_h;
    pthread_mutex_unlock(&g_gl_lock);

    GLint prev_read_fbo = 0, prev_draw_fbo = 0, prev_read_buf = GL_COLOR_ATTACHMENT0;
    GLint prev_vp[4] = {0, 0, g_vfbo_w, g_vfbo_h};
    GLfloat prev_clear[4] = {0, 0, 0, 0};
    GLboolean prev_cmask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLboolean prev_scissor = g_gl.p_glIsEnabled ? g_gl.p_glIsEnabled(GL_SCISSOR_TEST) : GL_FALSE;

    g_gl.p_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);
    g_gl.p_glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_draw_fbo);
    g_gl.p_glGetIntegerv(GL_READ_BUFFER, &prev_read_buf);
    g_gl.p_glGetIntegerv(GL_VIEWPORT, prev_vp);
    if (g_gl.p_glGetFloatv) g_gl.p_glGetFloatv(GL_COLOR_CLEAR_VALUE, prev_clear);
    if (g_gl.p_glGetBooleanv) g_gl.p_glGetBooleanv(GL_COLOR_WRITEMASK, prev_cmask);

    if (prev_scissor && g_gl.p_glDisable) g_gl.p_glDisable(GL_SCISSOR_TEST);
    g_gl.p_glBindFramebuffer(GL_READ_FRAMEBUFFER, g_virtual_fbo);
    g_gl.p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    if (g_gl.p_glReadBuffer) g_gl.p_glReadBuffer(GL_COLOR_ATTACHMENT0);
    if (g_gl.p_glDrawBuffer) g_gl.p_glDrawBuffer(GL_BACK);
    if (g_gl.p_glColorMask) g_gl.p_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (g_gl.p_glViewport) g_gl.p_glViewport(0, 0, win_w, win_h);
    if (g_gl.p_glClearColor && g_gl.p_glClear) {
        g_gl.p_glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        g_gl.p_glClear(GL_COLOR_BUFFER_BIT);
    }
    g_gl.p_glBlitFramebuffer(0, 0, g_vfbo_w, g_vfbo_h,
                             dst_x, dst_y, dst_x + dst_w, dst_y + dst_h,
                             GL_COLOR_BUFFER_BIT, GL_LINEAR);
    gl_trace_log_blit_vfbo(g_virtual_fbo, g_vfbo_w, g_vfbo_h, dst_x, dst_y, dst_w, dst_h);

    if (g_gl.p_glClearColor) g_gl.p_glClearColor(prev_clear[0], prev_clear[1], prev_clear[2], prev_clear[3]);
    if (g_gl.p_glColorMask) g_gl.p_glColorMask(prev_cmask[0], prev_cmask[1], prev_cmask[2], prev_cmask[3]);
    if (g_gl.p_glViewport) g_gl.p_glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
    g_gl.p_glBindFramebuffer(GL_READ_FRAMEBUFFER, g_virtual_fbo);
    if (g_gl.p_glReadBuffer) g_gl.p_glReadBuffer((GLenum)prev_read_buf);
    g_gl.p_glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prev_read_fbo);
    g_gl.p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prev_draw_fbo);
    if (prev_scissor && g_gl.p_glEnable) g_gl.p_glEnable(GL_SCISSOR_TEST);
}

void gl_hle_get_viewport_rect(int *vx, int *vy, int *vw, int *vh, int *ww, int *wh) {
    pthread_mutex_lock(&g_gl_lock);
    if (vx) *vx = g_vp_x;
    if (vy) *vy = g_vp_y;
    if (vw) *vw = g_vp_w;
    if (vh) *vh = g_vp_h;
    if (ww) *ww = g_win_w;
    if (wh) *wh = g_win_h;
    pthread_mutex_unlock(&g_gl_lock);
}

void gl_hle_record_gl_error(unsigned err_code, uint64_t frame_num) {
    GLenum err = (GLenum)err_code;
    if (err == GL_NO_ERROR) return;
    pthread_mutex_lock(&g_gl_lock);
    g_gl_err_total++;
    int idx = -1;
    for (int j = 0; j < g_gl_err_kind_n; j++) {
        if (g_gl_err_stats[j].code == err) { idx = j; break; }
    }
    if (idx < 0 && g_gl_err_kind_n < 16) {
        idx = g_gl_err_kind_n++;
        g_gl_err_stats[idx].code = err;
        g_gl_err_stats[idx].first_frame = frame_num;
    }
    uint64_t occ = 0;
    if (idx >= 0) {
        g_gl_err_stats[idx].count++;
        g_gl_err_stats[idx].last_frame = frame_num;
        occ = g_gl_err_stats[idx].count;
    }
    pthread_mutex_unlock(&g_gl_lock);
    if (occ <= 5) {
        LOG("[gl] frame %llu glGetError() = 0x%04x (occ=%llu)\n",
            (unsigned long long)frame_num, (unsigned)err, (unsigned long long)occ);
    }
}

void gl_hle_check_post_swap_errors(uint64_t frame_num) {
    if (!g_gl.p_glGetError) return;
    for (int i = 0; i < 8; i++) {
        GLenum err = g_gl.p_glGetError();
        if (err == GL_NO_ERROR) break;
        gl_hle_record_gl_error((unsigned)err, frame_num);
    }
}

void gl_hle_get_stats(uint64_t *draws_out, uint64_t *gl_errs_out, int *shaders_ok_out, int *shaders_fail_out) {
    if (draws_out) *draws_out = __atomic_load_n(&g_total_draws, __ATOMIC_RELAXED);
    pthread_mutex_lock(&g_gl_lock);
    if (gl_errs_out) *gl_errs_out = g_gl_err_total + gl_imgui_gl_error_count();
    if (shaders_ok_out) *shaders_ok_out = g_shader_ok;
    if (shaders_fail_out) *shaders_fail_out = g_shader_fail;
    pthread_mutex_unlock(&g_gl_lock);
}

void gl_hle_get_renderer_strings(const char **vendor_out, const char **renderer_out, const char **version_out) {
    pthread_mutex_lock(&g_gl_lock);
    if (vendor_out) *vendor_out = g_gl_vendor[0] ? g_gl_vendor : "OpenGL";
    if (renderer_out) *renderer_out = g_gl_renderer[0] ? g_gl_renderer : "OpenGL 4.5";
    if (version_out) *version_out = g_gl_version[0] ? g_gl_version : "4.5";
    pthread_mutex_unlock(&g_gl_lock);
}

void gl_hle_read_virtual_fbo_rgb(int w, int h, uint8_t *bgr_out) {
    if (!g_gl.p_glReadPixels || !g_gl.p_glGetIntegerv || !g_gl.p_glPixelStorei) return;
    GLuint target_fbo = g_virtual_fbo ? g_virtual_fbo : 0;
    GLint prev_read_fbo = 0, prev_pack = 4, prev_read_buf = GL_COLOR_ATTACHMENT0;
    g_gl.p_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);
    g_gl.p_glGetIntegerv(GL_PACK_ALIGNMENT, &prev_pack);
    g_gl.p_glGetIntegerv(GL_READ_BUFFER, &prev_read_buf);
    if (g_gl.p_glBindFramebuffer) g_gl.p_glBindFramebuffer(GL_READ_FRAMEBUFFER, target_fbo);
    if (g_gl.p_glReadBuffer) g_gl.p_glReadBuffer(target_fbo ? GL_COLOR_ATTACHMENT0 : GL_BACK);
    g_gl.p_glPixelStorei(GL_PACK_ALIGNMENT, 4);
    g_gl.p_glReadPixels(0, 0, w, h, GL_BGR_EXT, GL_UNSIGNED_BYTE, bgr_out);
    g_gl.p_glPixelStorei(GL_PACK_ALIGNMENT, prev_pack);
    if (g_gl.p_glReadBuffer) g_gl.p_glReadBuffer((GLenum)prev_read_buf);
    if (g_gl.p_glBindFramebuffer) g_gl.p_glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prev_read_fbo);
}

void gl_hle_read_backbuffer_rgb(int w, int h, uint8_t *bgr_out) {
    gl_hle_read_virtual_fbo_rgb(w, h, bgr_out);
}

void gl_hle_log_summary(void) {
    extern void hid_vibration_counts(uint64_t *single_out, uint64_t *batch_out);
    uint64_t ipc_misses = 0, ipc_stub_cmds = 0, am_unhandled = 0;
    char ipc_svcs[512] = "";
    ipc_get_telemetry(&ipc_misses, &ipc_stub_cmds, &am_unhandled, ipc_svcs, sizeof ipc_svcs);
    uint64_t vib_single = 0, vib_batch = 0;
    hid_vibration_counts(&vib_single, &vib_batch);

    pthread_mutex_lock(&g_gl_lock);
    LOG("[gl] SUMMARY: res_scale=%.1fx (%dx%d) shaders(ok=%d fail=%d) programs(ok=%d fail=%d) epa(hits=%u misses=%u) glErrors=%llu\n",
        (double)g_res_scale_pct / 100.0, g_vfbo_w, g_vfbo_h,
        g_shader_ok, g_shader_fail, g_prog_ok, g_prog_fail,
        g_epa_hits, g_epa_misses, (unsigned long long)g_gl_err_total);
    LOG("[gl] SUMMARY shader_audit: scanned=%d gl_FragCoord=%d textureSize=%d texelFetch=%d dFdx=%d unf_Gaussian=%d\n",
        g_sh_audit_total, g_sh_audit_fragcoord, g_sh_audit_texsize,
        g_sh_audit_texelfetch, g_sh_audit_dfdx, g_sh_audit_gauss_uni);
    uint64_t astc_calls = 0, astc_ns = 0;
    if (g_ncomp_fmts == 0) {
        LOG("[gl] SUMMARY compressed_tex: none\n");
    } else {
        for (int i = 0; i < g_ncomp_fmts; i++) {
            if (g_comp_fmts[i].astc_sw) {
                astc_calls += g_comp_fmts[i].count;
                astc_ns += g_comp_fmts[i].astc_ns;
            }
            LOG("[gl] SUMMARY compressed fmt=0x%04x count=%u astc_sw=%s astc_ms=%.2f\n",
                (unsigned)g_comp_fmts[i].fmt, g_comp_fmts[i].count,
                g_comp_fmts[i].astc_sw ? "yes" : "no",
                (double)g_comp_fmts[i].astc_ns / 1e6);
        }
    }
    LOG("[gl] SUMMARY astc_total: sw_decodes=%llu decode_ms=%.2f\n",
        (unsigned long long)astc_calls, (double)astc_ns / 1e6);
    if (g_nfbo_stats == 0) {
        LOG("[gl] SUMMARY offscreen_fbo: none (game only used default FBO 0)\n");
    } else {
        for (int i = 0; i < g_nfbo_stats; i++) {
            LOG("[gl] SUMMARY offscreen_fbo %dx%d fmt=0x%04x kind=%s unique_slots=%u binds=%llu\n",
                (int)g_fbo_stats[i].w, (int)g_fbo_stats[i].h, (unsigned)g_fbo_stats[i].fmt,
                g_fbo_stats[i].is_rbo ? "rbo" : "tex", g_fbo_stats[i].unique_slots,
                (unsigned long long)g_fbo_stats[i].bind_count);
        }
    }
    if (g_nvp_stats == 0) {
        LOG("[gl] SUMMARY viewport: none\n");
    } else {
        for (int i = 0; i < g_nvp_stats; i++) {
            LOG("[gl] SUMMARY viewport %dx%d count=%llu\n",
                (int)g_vp_stats[i].w, (int)g_vp_stats[i].h, (unsigned long long)g_vp_stats[i].count);
        }
    }
    for (int i = 0; i < g_gl_err_kind_n; i++) {
        LOG("[gl] SUMMARY glError 0x%04x count=%llu frames=[%llu..%llu]\n",
            (unsigned)g_gl_err_stats[i].code, (unsigned long long)g_gl_err_stats[i].count,
            (unsigned long long)g_gl_err_stats[i].first_frame,
            (unsigned long long)g_gl_err_stats[i].last_frame);
    }
    LOG("[gl] SUMMARY ipc_hid: unhandled_services=%llu (%s) stub_cmds=%llu am_unhandled_cmds=%llu SendVibrationValue=%llu SendVibrationValues=%llu\n",
        (unsigned long long)ipc_misses, ipc_svcs, (unsigned long long)ipc_stub_cmds,
        (unsigned long long)am_unhandled, (unsigned long long)vib_single, (unsigned long long)vib_batch);
    pthread_mutex_unlock(&g_gl_lock);
}

// ---- AAPCS64 argument accessors
#define U32(i) ((uint32_t)c->x[i])
#define I32(i) ((int32_t)(uint32_t)c->x[i])
#define U8(i)  ((GLboolean)(uint8_t)c->x[i])
#define PTR(i) ((void *)(uintptr_t)c->x[i])
#define CPTR(i) ((const void *)(uintptr_t)c->x[i])
#define SZP(i) ((ptrdiff_t)(int64_t)c->x[i])
#define STK_PTR(slot) ((const void *)(uintptr_t)*(const uint64_t *)(uintptr_t)(c->sp + (uint64_t)(slot) * 8ull))
#define STK_U32(slot) ((uint32_t)*(const uint64_t *)(uintptr_t)(c->sp + (uint64_t)(slot) * 8ull))
#define F32(v) (VSF32(v))
#define F64(v) (VSF64(v))

static inline GLenum trace_poll_err(void) {
    if (!gl_trace_is_active() || !g_gl.p_glGetError) return 0;
    return g_gl.p_glGetError();
}

void gl_hle_trace_swap_end(uint64_t frame_num) {
    gl_trace_on_frame_swap_end(frame_num, g_res_scale_pct, g_vfbo_w, g_vfbo_h,
                               g_guest_draw_fbo, g_guest_read_fbo, g_virtual_fbo,
                               g_vfbo_color, g_vfbo_depth);
}

// ---- Individual HLE wrappers for all 90 closed gl* functions + helpers
static void hle_glActiveTexture(Cpu *c) {
    GLenum u = U32(0);
    g_gl.p_glActiveTexture(u);
    gl_trace_log_active_texture(u);
}
static void hle_glAttachShader(Cpu *c) {
    GLuint p = U32(0), s = U32(1);
    g_gl.p_glAttachShader(p, s);
    gl_trace_on_attach_shader(p, s);
}
static void hle_glBindBuffer(Cpu *c) { g_gl.p_glBindBuffer(U32(0), U32(1)); }
static void hle_glBindFramebuffer(Cpu *c) {
    GLenum target = U32(0);
    GLuint fb = U32(1);
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) {
        g_guest_draw_fbo = fb;
        g_pending_d24s8_stencil_clear = false;
    }
    if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER) g_guest_read_fbo = fb;
    GLuint real_fb = (fb == 0 && g_virtual_fbo != 0) ? g_virtual_fbo : fb;
    g_gl.p_glBindFramebuffer(target, real_fb);
    if (gl_trace_is_active()) {
        pthread_mutex_lock(&g_gl_lock);
        bool sc = fbo_is_scaled_locked(fb);
        pthread_mutex_unlock(&g_gl_lock);
        gl_trace_log_bind_fbo(target, fb, real_fb, sc, g_virtual_fbo, g_vfbo_w, g_vfbo_h,
                              g_res_scale_pct, g_vfbo_color, g_vfbo_depth, trace_poll_err());
    }
}
static void hle_glBindRenderbuffer(Cpu *c) {
    GLenum target = U32(0);
    GLuint rbo = U32(1);
    if (target == GL_RENDERBUFFER) {
        pthread_mutex_lock(&g_gl_lock);
        g_cur_rbo = rbo;
        pthread_mutex_unlock(&g_gl_lock);
    }
    g_gl.p_glBindRenderbuffer(target, rbo);
}
static void hle_glBindTexture(Cpu *c) {
    GLenum target = U32(0);
    GLuint tex = U32(1);
    if (target == GL_TEXTURE_2D) {
        pthread_mutex_lock(&g_gl_lock);
        g_cur_tex2d = tex;
        pthread_mutex_unlock(&g_gl_lock);
    }
    g_gl.p_glBindTexture(target, tex);
    gl_trace_log_bind_texture(target, tex, trace_poll_err());
}
static void hle_glBlendColor(Cpu *c) { g_gl.p_glBlendColor(F32(0), F32(1), F32(2), F32(3)); }
static void hle_glBlendEquation(Cpu *c) {
    GLenum m = U32(0);
    g_gl.p_glBlendEquation(m);
    gl_trace_log_blend_eq(m, m);
}
static void hle_glBlendEquationSeparate(Cpu *c) {
    GLenum rgb = U32(0), a = U32(1);
    g_gl.p_glBlendEquationSeparate(rgb, a);
    gl_trace_log_blend_eq(rgb, a);
}
static void hle_glBlendFunc(Cpu *c) {
    GLenum sf = U32(0), df = U32(1);
    g_gl.p_glBlendFunc(sf, df);
    gl_trace_log_blend_func(sf, df, sf, df);
}
static void hle_glBlendFuncSeparate(Cpu *c) {
    GLenum sr = U32(0), dr = U32(1), sa = U32(2), da = U32(3);
    g_gl.p_glBlendFuncSeparate(sr, dr, sa, da);
    gl_trace_log_blend_func(sr, dr, sa, da);
}
static void hle_glBufferData(Cpu *c) { g_gl.p_glBufferData(U32(0), SZP(1), CPTR(2), U32(3)); }
static void hle_glCheckFramebufferStatus(Cpu *c) {
    GLenum target = U32(0);
    GLenum st = g_gl.p_glCheckFramebufferStatus ? g_gl.p_glCheckFramebufferStatus(target) : 0;
    c->x[0] = (uint64_t)(uint32_t)st;
}
static void hle_glClearBufferfi(Cpu *c) {
    GLenum b = U32(0); GLint db = I32(1); GLfloat d = F32(0); GLint s = I32(2);
    if (b == GL_DEPTH_STENCIL) g_pending_d24s8_stencil_clear = false;
    g_gl.p_glClearBufferfi(b, db, d, s);
    gl_trace_log_clear_buffer_fi(b, db, d, s, trace_poll_err());
}
static void hle_glClearBufferfv(Cpu *c) {
    GLenum b = U32(0); GLint db = I32(1);
    const GLfloat *v = (const GLfloat *)CPTR(2);
    if (b == GL_DEPTH && g_guest_draw_fbo != 0 &&
        (g_stencil_wmask_front != 0u || g_stencil_wmask_back != 0u)) {
        pthread_mutex_lock(&g_gl_lock);
        bool has_d24s8 = fbo_has_d24s8_locked(g_guest_draw_fbo);
        pthread_mutex_unlock(&g_gl_lock);
        if (has_d24s8) g_pending_d24s8_stencil_clear = true;
    }
    g_gl.p_glClearBufferfv(b, db, v);
    gl_trace_log_clear_buffer_f("glClearBufferfv", b, db, v, trace_poll_err());
}
static void hle_glClearBufferiv(Cpu *c) {
    GLenum b = U32(0); GLint db = I32(1);
    const GLint *v = (const GLint *)CPTR(2);
    if (b == GL_STENCIL) g_pending_d24s8_stencil_clear = false;
    g_gl.p_glClearBufferiv(b, db, v);
    gl_trace_log_clear_buffer_i("glClearBufferiv", b, db, v, trace_poll_err());
}
static void hle_glClearBufferuiv(Cpu *c) {
    GLenum b = U32(0); GLint db = I32(1);
    const GLuint *v = (const GLuint *)CPTR(2);
    g_gl.p_glClearBufferuiv(b, db, v);
    gl_trace_log_clear_buffer_u("glClearBufferuiv", b, db, v, trace_poll_err());
}
static void hle_glColorMask(Cpu *c) {
    GLboolean r = U8(0), g = U8(1), b = U8(2), a = U8(3);
    g_gl.p_glColorMask(r, g, b, a);
    gl_trace_log_color_mask(r, g, b, a);
}

static void store_shader_head(GLuint shader, const char *src, int len) {
    char clean[256];
    int j = 0;
    for (int i = 0; i < len && j < (int)sizeof clean - 1; i++) {
        char ch = src[i];
        clean[j++] = (ch == '\r') ? ' ' : ch;
    }
    clean[j] = 0;

    pthread_mutex_lock(&g_gl_lock);
    int slot = -1;
    for (int i = 0; i < MAX_SHADER_SRC; i++) {
        if (g_shader_src[i].id == shader || (g_shader_src[i].id == 0 && slot < 0)) {
            slot = i;
            if (g_shader_src[i].id == shader) break;
        }
    }
    if (slot >= 0) {
        g_shader_src[slot].id = shader;
        memcpy(g_shader_src[slot].head, clean, sizeof clean);
    }
    int logged = ++g_shader_src_logs;
    pthread_mutex_unlock(&g_gl_lock);

    if (logged <= 5) {
        const char *nl = strchr(clean, '\n');
        int first_len = nl ? (int)(nl - clean) : (int)strlen(clean);
        if (first_len > 120) first_len = 120;
        LOG("[gl] shader #%u first line: %.*s\n", shader, first_len, clean);
    }
}

static void audit_full_shader_source(GLuint shader, const GLchar *const *strings, GLsizei count, const GLint *lengths) {
    if (count <= 0 || !strings) return;
    size_t total = 0;
    for (GLsizei i = 0; i < count; i++) {
        if (!strings[i]) continue;
        total += (lengths && lengths[i] >= 0) ? (size_t)lengths[i] : strlen(strings[i]);
    }
    char *full = malloc(total + 1);
    if (!full) return;
    size_t p = 0;
    for (GLsizei i = 0; i < count; i++) {
        if (!strings[i]) continue;
        size_t n = (lengths && lengths[i] >= 0) ? (size_t)lengths[i] : strlen(strings[i]);
        memcpy(full + p, strings[i], n);
        p += n;
    }
    full[p] = 0;
    bool has_fc = strstr(full, "gl_FragCoord") != NULL;
    bool has_ts = strstr(full, "textureSize") != NULL;
    bool has_tf = strstr(full, "texelFetch") != NULL;
    bool has_dx = strstr(full, "dFdx") != NULL || strstr(full, "dFdy") != NULL || strstr(full, "fwidth") != NULL;
    bool has_gs = strstr(full, "unf_Gaussian.textureSize") != NULL;
    pthread_mutex_lock(&g_gl_lock);
    g_sh_audit_total++;
    if (has_fc) g_sh_audit_fragcoord++;
    if (has_ts) g_sh_audit_texsize++;
    if (has_tf) g_sh_audit_texelfetch++;
    if (has_dx) g_sh_audit_dfdx++;
    if (has_gs) g_sh_audit_gauss_uni++;
    pthread_mutex_unlock(&g_gl_lock);
    if ((has_fc || has_ts || has_tf || has_dx || has_gs) && getenv("YW1_AUDIT_SHADERS")) {
        LOG("[gl] SHADER AUDIT #%u: fc=%d ts=%d tf=%d df=%d gauss=%d len=%zu\n%s\n",
            shader, has_fc, has_ts, has_tf, has_dx, has_gs, p, full);
    }
    gl_trace_on_shader_source(shader, full, p, has_fc, has_ts, has_tf, has_dx, has_gs);
    free(full);
}

static void hle_glShaderSource(Cpu *c) {
    GLuint shader = U32(0);
    GLsizei count = I32(1);
    const GLchar *const *strings = (const GLchar *const *)CPTR(2);
    const GLint *lengths = (const GLint *)CPTR(3);
    if (count > 0 && strings && strings[0]) {
        char buf[256];
        int pos = 0;
        for (GLsizei i = 0; i < count && pos < 220; i++) {
            const char *s = strings[i];
            if (!s) continue;
            int n = (lengths && lengths[i] >= 0) ? lengths[i] : (int)strlen(s);
            if (n > 220 - pos) n = 220 - pos;
            memcpy(buf + pos, s, (size_t)n);
            pos += n;
        }
        buf[pos] = 0;
        store_shader_head(shader, buf, pos);
        audit_full_shader_source(shader, strings, count, lengths);
    }
    g_gl.p_glShaderSource(shader, count, strings, lengths);
}

static void hle_glCompileShader(Cpu *c) {
    GLuint shader = U32(0);
    g_gl.p_glCompileShader(shader);
    if (g_gl.p_glGetShaderiv) {
        GLint ok = 0;
        g_gl.p_glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (ok) {
            int n = __atomic_add_fetch(&g_shader_ok, 1, __ATOMIC_RELAXED);
            if (n == 1 || n % 25 == 0) LOG("[gl] shaders compiled ok=%d fail=%d\n", n, g_shader_fail);
            gl_trace_on_compile_shader(shader, ok, NULL);
        } else {
            int nf = __atomic_add_fetch(&g_shader_fail, 1, __ATOMIC_RELAXED);
            char info[2048] = "";
            if (g_gl.p_glGetShaderInfoLog) g_gl.p_glGetShaderInfoLog(shader, sizeof info - 1, NULL, info);
            char head[201] = "";
            pthread_mutex_lock(&g_gl_lock);
            for (int i = 0; i < MAX_SHADER_SRC; i++) {
                if (g_shader_src[i].id == shader) {
                    snprintf(head, sizeof head, "%.200s", g_shader_src[i].head);
                    break;
                }
            }
            pthread_mutex_unlock(&g_gl_lock);
            gl_trace_on_compile_shader(shader, ok, info);
            LOG("[gl] SHADER COMPILE FAILED #%u (fail_count=%d):\n  log: %s\n  src[0..200]: %s\n",
                shader, nf, info[0] ? info : "(empty)", head[0] ? head : "(unknown)");
        }
    }
}

static bool astc_format_dims(GLenum fmt, uint32_t *bw, uint32_t *bh, bool *srgb) {
    static const uint8_t dims[14][2] = {
        {4, 4},  {5, 4},  {5, 5},  {6, 5},  {6, 6},  {8, 5},   {8, 6},
        {8, 8},  {10, 5}, {10, 6}, {10, 8}, {10, 10}, {12, 10}, {12, 12},
    };
    if (fmt >= 0x93B0 && fmt <= 0x93BD) {
        *bw = dims[fmt - 0x93B0][0];
        *bh = dims[fmt - 0x93B0][1];
        *srgb = false;
        return true;
    }
    if (fmt >= 0x93D0 && fmt <= 0x93DD) {
        *bw = dims[fmt - 0x93D0][0];
        *bh = dims[fmt - 0x93D0][1];
        *srgb = true;
        return true;
    }
    return false;
}

static void hle_glCompressedTexImage2D(Cpu *c) {
    GLenum target = U32(0);
    GLint level = I32(1);
    GLenum internalformat = U32(2);
    GLsizei width = I32(3);
    GLsizei height = I32(4);
    GLint border = I32(5);
    GLsizei imageSize = I32(6);
    const void *data = CPTR(7);

    uint32_t bw = 0, bh = 0;
    bool srgb = false;
    bool is_astc = astc_format_dims(internalformat, &bw, &bh, &srgb);

    bool first_seen = false;
    pthread_mutex_lock(&g_gl_lock);
    int idx = -1;
    for (int i = 0; i < g_ncomp_fmts; i++) {
        if (g_comp_fmts[i].fmt == internalformat) {
            idx = i;
            break;
        }
    }
    if (idx < 0 && g_ncomp_fmts < 64) {
        idx = g_ncomp_fmts++;
        g_comp_fmts[idx].fmt = internalformat;
        g_comp_fmts[idx].count = 0;
        g_comp_fmts[idx].astc_sw = is_astc;
        g_comp_fmts[idx].astc_ns = 0;
        first_seen = true;
    }
    if (idx >= 0) {
        g_comp_fmts[idx].count++;
        if (is_astc) g_comp_fmts[idx].astc_sw = true;
    }
    pthread_mutex_unlock(&g_gl_lock);

    if (first_seen) {
        LOG("[gl] glCompressedTexImage2D first format=0x%04x (%s) target=0x%04x %dx%d size=%d\n",
            internalformat, is_astc ? "ASTC -> software RGBA8 decode" : "native driver upload",
            target, width, height, imageSize);
    }

    if (is_astc) {
        if (width > 0 && height > 0 && data) {
            uint8_t *rgba = malloc((size_t)width * (size_t)height * 4);
            if (rgba) {
                uint64_t t0 = plat_now_ns();
                astc_decode_ldr((const uint8_t *)data, (uint32_t)width, (uint32_t)height, bw, bh, srgb, rgba);
                uint64_t dt = plat_now_ns() - t0;
                if (idx >= 0) {
                    pthread_mutex_lock(&g_gl_lock);
                    g_comp_fmts[idx].astc_ns += dt;
                    pthread_mutex_unlock(&g_gl_lock);
                }
                g_gl.p_glTexImage2D(target, level, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, width, height, border, GL_RGBA,
                                    GL_UNSIGNED_BYTE, rgba);
                free(rgba);
                return;
            }
        }
        g_gl.p_glTexImage2D(target, level, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, width, height, border, GL_RGBA,
                            GL_UNSIGNED_BYTE, NULL);
        return;
    }

    g_gl.p_glCompressedTexImage2D(target, level, internalformat, width, height, border, imageSize, data);
}

static void hle_glCopyTexImage2D(Cpu *c) {
    GLenum target = U32(0);
    GLint level = I32(1);
    GLenum int_fmt = U32(2);
    GLint x = I32(3), y = I32(4);
    GLsizei w = I32(5), h = I32(6);
    GLint border = I32(7);
    g_gl.p_glCopyTexImage2D(target, level, int_fmt, x, y, w, h, border);
    if (gl_trace_hotkey_enabled()) {
        pthread_mutex_lock(&g_gl_lock);
        GLuint dst_tex = g_cur_tex2d;
        bool read_sc = fbo_is_scaled_locked(g_guest_read_fbo);
        pthread_mutex_unlock(&g_gl_lock);
        gl_trace_log_copy_tex_image_2d(dst_tex, g_guest_read_fbo, target, level, int_fmt, x, y, w, h,
                                       read_sc, trace_poll_err());
    }
}
static void hle_glCopyTexSubImage2D(Cpu *c) {
    GLenum target = U32(0);
    GLint level = I32(1), xoff = I32(2), yoff = I32(3), x = I32(4), y = I32(5);
    GLsizei w = I32(6), h = I32(7);
    g_gl.p_glCopyTexSubImage2D(target, level, xoff, yoff, x, y, w, h);
    if (gl_trace_hotkey_enabled()) {
        pthread_mutex_lock(&g_gl_lock);
        GLuint dst_tex = g_cur_tex2d;
        bool read_sc = fbo_is_scaled_locked(g_guest_read_fbo);
        pthread_mutex_unlock(&g_gl_lock);
        gl_trace_on_copy_tex_sub_image_2d(dst_tex, g_guest_read_fbo, target, level,
                                          xoff, yoff, x, y, w, h, read_sc, trace_poll_err());
    }
}
static void hle_glCreateProgram(Cpu *c) {
    GLuint pr = g_gl.p_glCreateProgram();
    gl_trace_on_create_program(pr);
    c->x[0] = (uint64_t)pr;
}
static void hle_glCreateShader(Cpu *c) {
    GLenum t = U32(0);
    GLuint sh = g_gl.p_glCreateShader(t);
    gl_trace_on_create_shader(t, sh);
    c->x[0] = (uint64_t)sh;
}
static void hle_glCullFace(Cpu *c) { g_gl.p_glCullFace(U32(0)); }
static void hle_glDeleteBuffers(Cpu *c) { g_gl.p_glDeleteBuffers(I32(0), (const GLuint *)CPTR(1)); }
static GLenum rewrite_default_fbo_buf(GLenum buf, bool is_draw) {
    if (g_virtual_fbo != 0 && (is_draw ? (g_guest_draw_fbo == 0) : (g_guest_read_fbo == 0))) {
        if (buf == GL_BACK || buf == GL_BACK_LEFT) return GL_COLOR_ATTACHMENT0;
    }
    return buf;
}

static void hle_glDeleteFramebuffers(Cpu *c) {
    GLsizei n = I32(0);
    const GLuint *fbs = (const GLuint *)CPTR(1);
    g_gl.p_glDeleteFramebuffers(n, fbs);
    if (fbs && n > 0) {
        pthread_mutex_lock(&g_gl_lock);
        for (GLsizei i = 0; i < n; i++) fbo_scale_clear_locked(fbs[i]);
        pthread_mutex_unlock(&g_gl_lock);
        if (gl_trace_hotkey_enabled()) {
            for (GLsizei i = 0; i < n; i++) gl_trace_on_delete_fbo(fbs[i]);
        }
    }
    if (fbs && n > 0 && g_virtual_fbo != 0) {
        for (GLsizei i = 0; i < n; i++) {
            if (fbs[i] == 0) continue;
            if (fbs[i] == g_guest_draw_fbo) {
                g_guest_draw_fbo = 0;
                g_pending_d24s8_stencil_clear = false;
                g_gl.p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_virtual_fbo);
            }
            if (fbs[i] == g_guest_read_fbo) {
                g_guest_read_fbo = 0;
                g_gl.p_glBindFramebuffer(GL_READ_FRAMEBUFFER, g_virtual_fbo);
            }
        }
    }
}
static void hle_glDeleteProgram(Cpu *c) {
    GLuint p = U32(0);
    if (p == g_cur_prog) g_cur_prog = 0;
    invalidate_atest_cache(p);
    gl_trace_on_delete_program(p);
    g_gl.p_glDeleteProgram(p);
}
static void hle_glDeleteRenderbuffers(Cpu *c) { g_gl.p_glDeleteRenderbuffers(I32(0), (const GLuint *)CPTR(1)); }
static void hle_glDeleteShader(Cpu *c) {
    GLuint s = U32(0);
    gl_trace_on_delete_shader(s);
    g_gl.p_glDeleteShader(s);
}
static void hle_glDeleteTextures(Cpu *c) { g_gl.p_glDeleteTextures(I32(0), (const GLuint *)CPTR(1)); }
static void hle_glDepthFunc(Cpu *c) {
    GLenum f = U32(0);
    g_gl.p_glDepthFunc(f);
    gl_trace_log_depth_func(f);
}
static void hle_glDepthMask(Cpu *c) {
    GLboolean flag = U8(0);
    g_gl.p_glDepthMask(flag);
    gl_trace_log_depth_mask(flag);
}
static void hle_glDisable(Cpu *c) {
    GLenum cap = U32(0);
    if (cap == GL_STENCIL_TEST) g_cur_stencil_test = false;
    g_gl.p_glDisable(cap);
    gl_trace_log_enable_cap(cap, false, trace_poll_err());
}
static void hle_glDisableVertexAttribArray(Cpu *c) { g_gl.p_glDisableVertexAttribArray(U32(0)); }
static void hle_glDrawArrays(Cpu *c) {
    GLenum m = U32(0); GLint first = I32(1); GLsizei cnt = I32(2);
    g_pending_d24s8_stencil_clear = false;
    __atomic_fetch_add(&g_total_draws, 1, __ATOMIC_RELAXED);
    g_gl.p_glDrawArrays(m, first, cnt);
    gl_trace_log_draw_arrays(m, first, cnt, g_guest_draw_fbo, trace_poll_err());
}
static void hle_glDrawBuffer(Cpu *c) {
    if (g_gl.p_glDrawBuffer) g_gl.p_glDrawBuffer(rewrite_default_fbo_buf(U32(0), true));
}
static void hle_glDrawBuffers(Cpu *c) {
    GLsizei n = I32(0);
    const GLenum *bufs = (const GLenum *)CPTR(1);
    if (bufs && n > 0 && n <= 16 && g_guest_draw_fbo == 0 && g_virtual_fbo != 0) {
        GLenum mapped[16];
        bool changed = false;
        for (GLsizei i = 0; i < n; i++) {
            mapped[i] = rewrite_default_fbo_buf(bufs[i], true);
            if (mapped[i] != bufs[i]) changed = true;
        }
        g_gl.p_glDrawBuffers(n, changed ? mapped : bufs);
        return;
    }
    g_gl.p_glDrawBuffers(n, bufs);
}

static void hle_glDrawElements(Cpu *c) {
    GLenum m = U32(0), t = U32(2); GLsizei cnt = I32(1); const void *idx = CPTR(3);
    g_pending_d24s8_stencil_clear = false;
    __atomic_fetch_add(&g_total_draws, 1, __ATOMIC_RELAXED);
    GLint atest_loc = -1;
    bool suppressed = lens_mask_atest_suppress_begin(&atest_loc);
    g_gl.p_glDrawElements(m, cnt, t, idx);
    if (suppressed) g_gl.p_glUniform1i(atest_loc, 1);
    gl_trace_log_draw_elements(m, cnt, t, idx, g_guest_draw_fbo, trace_poll_err());
}
static void hle_glEnable(Cpu *c) {
    GLenum cap = U32(0);
    if (cap == GL_STENCIL_TEST) g_cur_stencil_test = true;
    g_gl.p_glEnable(cap);
    gl_trace_log_enable_cap(cap, true, trace_poll_err());
}
static void hle_glEnableVertexAttribArray(Cpu *c) { g_gl.p_glEnableVertexAttribArray(U32(0)); }
static void hle_glFinish(Cpu *c) { (void)c; g_gl.p_glFinish(); }
static void hle_glFramebufferRenderbuffer(Cpu *c) {
    GLenum target = U32(0), attachment = U32(1), rbt = U32(2);
    GLuint rb = U32(3);
    record_offscreen_fbo(target, attachment, true, rb);
    g_gl.p_glFramebufferRenderbuffer(target, attachment, rbt, rb);
    if (gl_trace_hotkey_enabled()) {
        GLuint gfbo = (target == GL_READ_FRAMEBUFFER) ? g_guest_read_fbo : g_guest_draw_fbo;
        gl_trace_on_fbo_attach(gfbo, attachment, true, rb, trace_poll_err());
    }
}
static void hle_glFramebufferTexture2D(Cpu *c) {
    GLenum target = U32(0), attachment = U32(1), textarget = U32(2);
    GLuint tex = U32(3);
    GLint level = I32(4);
    record_offscreen_fbo(target, attachment, false, tex);
    g_gl.p_glFramebufferTexture2D(target, attachment, textarget, tex, level);
    if (gl_trace_hotkey_enabled()) {
        GLuint gfbo = (target == GL_READ_FRAMEBUFFER) ? g_guest_read_fbo : g_guest_draw_fbo;
        gl_trace_on_fbo_attach(gfbo, attachment, false, tex, trace_poll_err());
    }
}
static void hle_glFrontFace(Cpu *c) { g_gl.p_glFrontFace(U32(0)); }
static void hle_glGenBuffers(Cpu *c) { g_gl.p_glGenBuffers(I32(0), (GLuint *)PTR(1)); }
static void hle_glGenFramebuffers(Cpu *c) { g_gl.p_glGenFramebuffers(I32(0), (GLuint *)PTR(1)); }
static void hle_glGenRenderbuffers(Cpu *c) { g_gl.p_glGenRenderbuffers(I32(0), (GLuint *)PTR(1)); }
static void hle_glGenTextures(Cpu *c) { g_gl.p_glGenTextures(I32(0), (GLuint *)PTR(1)); }
static void hle_glGenerateMipmap(Cpu *c) { g_gl.p_glGenerateMipmap(U32(0)); }
static void hle_glGetActiveAttrib(Cpu *c) {
    g_gl.p_glGetActiveAttrib(U32(0), U32(1), I32(2), (GLsizei *)PTR(3), (GLint *)PTR(4), (GLenum *)PTR(5), (GLchar *)PTR(6));
}
static void hle_glGetActiveUniform(Cpu *c) {
    g_gl.p_glGetActiveUniform(U32(0), U32(1), I32(2), (GLsizei *)PTR(3), (GLint *)PTR(4), (GLenum *)PTR(5), (GLchar *)PTR(6));
}
static void hle_glGetAttribLocation(Cpu *c) {
    c->x[0] = (uint64_t)(uint32_t)g_gl.p_glGetAttribLocation(U32(0), (const GLchar *)CPTR(1));
}
static void hle_glGetBooleanv(Cpu *c) { g_gl.p_glGetBooleanv(U32(0), (GLboolean *)PTR(1)); }
static void hle_glGetIntegerv(Cpu *c) {
    GLenum pname = U32(0);
    GLint *data = (GLint *)PTR(1);
    g_gl.p_glGetIntegerv(pname, data);
    if (data && g_virtual_fbo != 0 &&
        (pname == GL_FRAMEBUFFER_BINDING || pname == GL_READ_FRAMEBUFFER_BINDING) &&
        (GLuint)*data == g_virtual_fbo) {
        *data = 0;
    }
}
static void hle_glGetProgramiv(Cpu *c) { g_gl.p_glGetProgramiv(U32(0), U32(1), (GLint *)PTR(2)); }
static void hle_glGetShaderPrecisionFormat(Cpu *c) {
    GLenum stype = U32(0), ptype = U32(1);
    GLint *range = (GLint *)PTR(2);
    GLint *prec = (GLint *)PTR(3);
    if (g_gl.p_glGetShaderPrecisionFormat) {
        g_gl.p_glGetShaderPrecisionFormat(stype, ptype, range, prec);
    } else {
        bool is_float = (ptype >= GL_FLOAT);
        if (range) { range[0] = is_float ? 127 : 31; range[1] = is_float ? 127 : 30; }
        if (prec) *prec = is_float ? 23 : 0;
    }
}
static void hle_glGetShaderiv(Cpu *c) { g_gl.p_glGetShaderiv(U32(0), U32(1), (GLint *)PTR(2)); }

static void hle_glGetString(Cpu *c) {
    GLenum name = U32(0);
    pthread_mutex_lock(&g_gl_lock);
    for (int i = 0; i < g_str_cache_n; i++) {
        if (g_str_cache[i].name == name) {
            c->x[0] = g_str_cache[i].guest_ptr;
            pthread_mutex_unlock(&g_gl_lock);
            return;
        }
    }
    const char *s = g_gl.p_glGetString ? (const char *)g_gl.p_glGetString(name) : NULL;
    uint64_t gp = 0;
    if (s) {
        size_t len = strlen(s);
        char *dst = (char *)hle_alloc(len + 1);
        memcpy(dst, s, len + 1);
        gp = (uint64_t)(uintptr_t)dst;
        if (g_str_cache_n < MAX_GL_STRINGS) {
            g_str_cache[g_str_cache_n].name = name;
            g_str_cache[g_str_cache_n].guest_ptr = gp;
            g_str_cache_n++;
        }
        LOG("[gl] glGetString(0x%04x) -> %.160s%s\n", name, s, len > 160 ? "..." : "");
    }
    pthread_mutex_unlock(&g_gl_lock);
    c->x[0] = gp;
}

static void hle_glGetUniformLocation(Cpu *c) {
    c->x[0] = (uint64_t)(uint32_t)g_gl.p_glGetUniformLocation(U32(0), (const GLchar *)CPTR(1));
}

static void hle_glLinkProgram(Cpu *c) {
    GLuint prog = U32(0);
    invalidate_atest_cache(prog);
    g_gl.p_glLinkProgram(prog);
    if (g_gl.p_glGetProgramiv) {
        GLint ok = 0;
        g_gl.p_glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (ok) {
            int n = __atomic_add_fetch(&g_prog_ok, 1, __ATOMIC_RELAXED);
            if (n == 1 || n % 25 == 0) LOG("[gl] programs linked ok=%d fail=%d\n", n, g_prog_fail);
            if (gl_trace_hotkey_enabled()) {
                char snames[16][64];
                GLenum stypes[16];
                GLint slocs[16];
                int ns = 0;
                GLint nuni = 0;
                g_gl.p_glGetProgramiv(prog, GL_ACTIVE_UNIFORMS, &nuni);
                for (GLint i = 0; i < nuni && ns < 16; i++) {
                    GLsizei len = 0;
                    GLint sz = 0;
                    GLenum utype = 0;
                    char nm[64] = "";
                    if (g_gl.p_glGetActiveUniform) {
                        g_gl.p_glGetActiveUniform(prog, (GLuint)i, sizeof nm - 1, &len, &sz, &utype, nm);
                    }
                    bool is_samp = (utype >= 0x8B5D && utype <= 0x8B64) || (utype >= 0x8DC0 && utype <= 0x8DCF);
                    if (is_samp && nm[0]) {
                        snprintf(snames[ns], 64, "%s", nm);
                        stypes[ns] = utype;
                        slocs[ns] = g_gl.p_glGetUniformLocation ? g_gl.p_glGetUniformLocation(prog, nm) : -1;
                        ns++;
                    }
                }
                gl_trace_on_link_program(prog, ok, NULL, ns, snames, stypes, slocs);
            }
        } else {
            int nf = __atomic_add_fetch(&g_prog_fail, 1, __ATOMIC_RELAXED);
            char info[2048] = "";
            if (g_gl.p_glGetProgramInfoLog) g_gl.p_glGetProgramInfoLog(prog, sizeof info - 1, NULL, info);
            gl_trace_on_link_program(prog, ok, info, 0, NULL, NULL, NULL);
            LOG("[gl] PROGRAM LINK FAILED #%u (fail_count=%d): %s\n", prog, nf, info[0] ? info : "(empty)");
        }
    }
}

static void hle_glPolygonOffset(Cpu *c) { g_gl.p_glPolygonOffset(F32(0), F32(1)); }
static void hle_glReadBuffer(Cpu *c) {
    if (g_gl.p_glReadBuffer) g_gl.p_glReadBuffer(rewrite_default_fbo_buf(U32(0), false));
}
static void hle_glReadPixels(Cpu *c) { g_gl.p_glReadPixels(I32(0), I32(1), I32(2), I32(3), U32(4), U32(5), PTR(6)); }
static void hle_glRenderbufferStorage(Cpu *c) {
    GLenum target = U32(0), fmt = U32(1);
    GLsizei w = I32(2), h = I32(3);
    bool scaled = false;
    GLsizei real_w = w, real_h = h;
    GLuint rbo = 0;
    if (target == GL_RENDERBUFFER && w > 0 && h > 0) {
        if (g_res_scale_pct > 100 && is_16_9_rt_dims(w, h)) {
            scaled = true;
            real_w = (GLsizei)(((int64_t)w * g_res_scale_pct) / 100);
            real_h = (GLsizei)(((int64_t)h * g_res_scale_pct) / 100);
        }
        pthread_mutex_lock(&g_gl_lock);
        rbo = g_cur_rbo;
        obj_dim_set_locked(g_rbo_dims, rbo, fmt, w, h, scaled);
        pthread_mutex_unlock(&g_gl_lock);
    }
    g_gl.p_glRenderbufferStorage(target, fmt, real_w, real_h);
    if (gl_trace_hotkey_enabled() && target == GL_RENDERBUFFER && w > 0 && h > 0) {
        gl_trace_on_rbo_storage(rbo, fmt, w, h, real_w, real_h, scaled, trace_poll_err());
    }
}
static void hle_glScissor(Cpu *c) {
    GLint gx = I32(0), gy = I32(1);
    GLsizei gw = I32(2), gh = I32(3);
    GLint x = gx, y = gy;
    GLsizei w = gw, h = gh;
    bool scale_cur = false;
    if (g_res_scale_pct > 100) {
        pthread_mutex_lock(&g_gl_lock);
        scale_cur = fbo_is_scaled_locked(g_guest_draw_fbo);
        pthread_mutex_unlock(&g_gl_lock);
        if (scale_cur) {
            x = (GLint)(((int64_t)x * g_res_scale_pct) / 100);
            y = (GLint)(((int64_t)y * g_res_scale_pct) / 100);
            w = (GLsizei)(((int64_t)w * g_res_scale_pct) / 100);
            h = (GLsizei)(((int64_t)h * g_res_scale_pct) / 100);
        }
    }
    g_gl.p_glScissor(x, y, w, h);
    gl_trace_log_scissor(gx, gy, gw, gh, x, y, w, h, scale_cur, g_guest_draw_fbo, trace_poll_err());
}
static void hle_glStencilFunc(Cpu *c) {
    GLenum f = U32(0); GLint r = I32(1); GLuint m = U32(2);
    g_cur_stencil_func = f;
    g_gl.p_glStencilFunc(f, r, m);
    gl_trace_log_stencil_func(f, r, m);
}
static void hle_glStencilFuncSeparate(Cpu *c) {
    GLenum face = U32(0), f = U32(1); GLint r = I32(2); GLuint m = U32(3);
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) g_cur_stencil_func = f;
    g_gl.p_glStencilFuncSeparate(face, f, r, m);
    gl_trace_log_stencil_func_sep(face, f, r, m);
}
static void maybe_auto_clear_d24s8_stencil(GLenum face, GLuint new_mask) {
    if (new_mask != 0u || !g_pending_d24s8_stencil_clear) return;
    bool affect_front = (face == GL_FRONT || face == GL_FRONT_AND_BACK);
    bool affect_back = (face == GL_BACK || face == GL_FRONT_AND_BACK);
    bool was_unlocked = (affect_front && g_stencil_wmask_front != 0u) ||
                        (affect_back && g_stencil_wmask_back != 0u);
    g_pending_d24s8_stencil_clear = false;
    if (g_guest_draw_fbo != 0 && was_unlocked && g_gl.p_glClearBufferiv) {
        const GLint zero = 0;
        g_gl.p_glClearBufferiv(GL_STENCIL, 0, &zero);
        gl_trace_log_clear_buffer_i("glClearBufferiv[d24s8_auto]", GL_STENCIL, 0, &zero, trace_poll_err());
    }
}
static void hle_glStencilMask(Cpu *c) {
    GLuint m = U32(0);
    maybe_auto_clear_d24s8_stencil(GL_FRONT_AND_BACK, m);
    g_stencil_wmask_front = g_stencil_wmask_back = m;
    g_gl.p_glStencilMask(m);
    gl_trace_log_stencil_mask(m);
}
static void hle_glStencilMaskSeparate(Cpu *c) {
    GLenum face = U32(0); GLuint m = U32(1);
    maybe_auto_clear_d24s8_stencil(face, m);
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) g_stencil_wmask_front = m;
    if (face == GL_BACK || face == GL_FRONT_AND_BACK) g_stencil_wmask_back = m;
    g_gl.p_glStencilMaskSeparate(face, m);
    gl_trace_log_stencil_mask_sep(face, m);
}
static void hle_glStencilOp(Cpu *c) {
    GLenum sf = U32(0), df = U32(1), dp = U32(2);
    g_cur_stencil_dppass = dp;
    g_gl.p_glStencilOp(sf, df, dp);
    gl_trace_log_stencil_op(sf, df, dp);
}
static void hle_glStencilOpSeparate(Cpu *c) {
    GLenum face = U32(0), sf = U32(1), df = U32(2), dp = U32(3);
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) g_cur_stencil_dppass = dp;
    g_gl.p_glStencilOpSeparate(face, sf, df, dp);
    gl_trace_log_stencil_op_sep(face, sf, df, dp);
}
static void hle_glTexImage2D(Cpu *c) {
    GLenum target = U32(0);
    GLint level = I32(1), int_fmt = I32(2);
    GLsizei w = I32(3), h = I32(4);
    GLint border = I32(5);
    GLenum fmt = U32(6), type = U32(7);
    const void *pixels = STK_PTR(0);
    bool scaled = false;
    GLsizei real_w = w, real_h = h;
    GLuint tex_id = 0;
    if (target == GL_TEXTURE_2D && level == 0 && w > 0 && h > 0) {
        // Scale 16:9 offscreen render target textures (pixels == NULL); exclude movie uploads (pixels != NULL), UI, and 2048x2048 shadows.
        if (g_res_scale_pct > 100 && pixels == NULL && is_16_9_rt_dims(w, h)) {
            scaled = true;
            real_w = (GLsizei)(((int64_t)w * g_res_scale_pct) / 100);
            real_h = (GLsizei)(((int64_t)h * g_res_scale_pct) / 100);
        }
        pthread_mutex_lock(&g_gl_lock);
        tex_id = g_cur_tex2d;
        obj_dim_set_locked(g_tex_dims, tex_id, (GLenum)int_fmt, w, h, scaled);
        pthread_mutex_unlock(&g_gl_lock);
    }
    g_gl.p_glTexImage2D(target, level, int_fmt, real_w, real_h, border, fmt, type, pixels);
    if (gl_trace_hotkey_enabled() && level == 0 && w > 0 && h > 0) {
        gl_trace_on_tex_image_2d(tex_id, target, int_fmt, w, h, real_w, real_h, scaled, trace_poll_err());
    }
}
static void hle_glTexParameterf(Cpu *c) { g_gl.p_glTexParameterf(U32(0), U32(1), F32(0)); }
static void hle_glTexParameterfv(Cpu *c) { g_gl.p_glTexParameterfv(U32(0), U32(1), (const GLfloat *)CPTR(2)); }
static void hle_glTexParameteri(Cpu *c) { g_gl.p_glTexParameteri(U32(0), U32(1), I32(2)); }
static void hle_glTexSubImage2D(Cpu *c) {
    g_gl.p_glTexSubImage2D(U32(0), I32(1), I32(2), I32(3), I32(4), I32(5), U32(6), U32(7), STK_PTR(0));
}

static void hle_glUniform1f(Cpu *c) { g_gl.p_glUniform1f(I32(0), F32(0)); }
static void hle_glUniform1fv(Cpu *c) { g_gl.p_glUniform1fv(I32(0), I32(1), (const GLfloat *)CPTR(2)); }
static void hle_glUniform1i(Cpu *c) { g_gl.p_glUniform1i(I32(0), I32(1)); }
static void hle_glUniform1iv(Cpu *c) { g_gl.p_glUniform1iv(I32(0), I32(1), (const GLint *)CPTR(2)); }
static void hle_glUniform2f(Cpu *c) { g_gl.p_glUniform2f(I32(0), F32(0), F32(1)); }
static void hle_glUniform2fv(Cpu *c) { g_gl.p_glUniform2fv(I32(0), I32(1), (const GLfloat *)CPTR(2)); }
static void hle_glUniform2i(Cpu *c) { g_gl.p_glUniform2i(I32(0), I32(1), I32(2)); }
static void hle_glUniform2iv(Cpu *c) { g_gl.p_glUniform2iv(I32(0), I32(1), (const GLint *)CPTR(2)); }
static void hle_glUniform3f(Cpu *c) { g_gl.p_glUniform3f(I32(0), F32(0), F32(1), F32(2)); }
static void hle_glUniform3fv(Cpu *c) { g_gl.p_glUniform3fv(I32(0), I32(1), (const GLfloat *)CPTR(2)); }
static void hle_glUniform3i(Cpu *c) { g_gl.p_glUniform3i(I32(0), I32(1), I32(2), I32(3)); }
static void hle_glUniform3iv(Cpu *c) { g_gl.p_glUniform3iv(I32(0), I32(1), (const GLint *)CPTR(2)); }
static void hle_glUniform4f(Cpu *c) { g_gl.p_glUniform4f(I32(0), F32(0), F32(1), F32(2), F32(3)); }
static void hle_glUniform4fv(Cpu *c) { g_gl.p_glUniform4fv(I32(0), I32(1), (const GLfloat *)CPTR(2)); }
static void hle_glUniform4i(Cpu *c) { g_gl.p_glUniform4i(I32(0), I32(1), I32(2), I32(3), I32(4)); }
static void hle_glUniform4iv(Cpu *c) { g_gl.p_glUniform4iv(I32(0), I32(1), (const GLint *)CPTR(2)); }
static void hle_glUniformMatrix2fv(Cpu *c) { g_gl.p_glUniformMatrix2fv(I32(0), I32(1), U8(2), (const GLfloat *)CPTR(3)); }
static void hle_glUniformMatrix3fv(Cpu *c) { g_gl.p_glUniformMatrix3fv(I32(0), I32(1), U8(2), (const GLfloat *)CPTR(3)); }
static void hle_glUniformMatrix4fv(Cpu *c) { g_gl.p_glUniformMatrix4fv(I32(0), I32(1), U8(2), (const GLfloat *)CPTR(3)); }

static void hle_glUseProgram(Cpu *c) {
    GLuint p = U32(0);
    if (p != g_atest_cached_prog) invalidate_atest_cache(0);
    g_cur_prog = p;
    g_gl.p_glUseProgram(p);
    gl_trace_log_use_program(p, trace_poll_err());
}
static void hle_glVertexAttrib1fv(Cpu *c) { g_gl.p_glVertexAttrib1fv(U32(0), (const GLfloat *)CPTR(1)); }
static void hle_glVertexAttrib2fv(Cpu *c) { g_gl.p_glVertexAttrib2fv(U32(0), (const GLfloat *)CPTR(1)); }
static void hle_glVertexAttrib3fv(Cpu *c) { g_gl.p_glVertexAttrib3fv(U32(0), (const GLfloat *)CPTR(1)); }
static void hle_glVertexAttrib4fv(Cpu *c) { g_gl.p_glVertexAttrib4fv(U32(0), (const GLfloat *)CPTR(1)); }
static void hle_glVertexAttribPointer(Cpu *c) {
    g_gl.p_glVertexAttribPointer(U32(0), I32(1), U32(2), U8(3), I32(4), CPTR(5));
}
static void hle_glViewport(Cpu *c) {
    GLint gx = I32(0), gy = I32(1);
    GLsizei gw = I32(2), gh = I32(3);
    GLint x = gx, y = gy;
    GLsizei w = gw, h = gh;
    g_cur_vp_w = gw;
    g_cur_vp_h = gh;
    record_viewport_stat(gw, gh);
    bool scale_cur = false;
    if (g_res_scale_pct > 100) {
        pthread_mutex_lock(&g_gl_lock);
        scale_cur = fbo_is_scaled_locked(g_guest_draw_fbo);
        pthread_mutex_unlock(&g_gl_lock);
        if (scale_cur) {
            x = (GLint)(((int64_t)x * g_res_scale_pct) / 100);
            y = (GLint)(((int64_t)y * g_res_scale_pct) / 100);
            w = (GLsizei)(((int64_t)w * g_res_scale_pct) / 100);
            h = (GLsizei)(((int64_t)h * g_res_scale_pct) / 100);
        }
    }
    g_gl.p_glViewport(x, y, w, h);
    gl_trace_log_viewport(gx, gy, gw, gh, x, y, w, h, scale_cur, g_guest_draw_fbo, trace_poll_err());
}

// Adjacent safety helpers
static void hle_glGetError(Cpu *c) {
    GLenum e = gl_trace_hotkey_enabled() ? gl_trace_pop_stashed_error() : 0;
    if (!e && g_gl.p_glGetError) e = g_gl.p_glGetError();
    c->x[0] = (uint64_t)e;
}
static void hle_glFlush(Cpu *c) { (void)c; if (g_gl.p_glFlush) g_gl.p_glFlush(); }
static void hle_glClear(Cpu *c) {
    GLbitfield m = U32(0);
    if ((m & GL_STENCIL_BUFFER_BIT) != 0u) g_pending_d24s8_stencil_clear = false;
    g_gl.p_glClear(m);
    gl_trace_log_clear(m, trace_poll_err());
}
static void hle_glClearColor(Cpu *c) {
    GLfloat r = F32(0), g = F32(1), b = F32(2), a = F32(3);
    g_gl.p_glClearColor(r, g, b, a);
    gl_trace_log_clear_color(r, g, b, a);
}
static void hle_glClearDepth(Cpu *c) {
    GLdouble d = F64(0);
    g_gl.p_glClearDepth(d);
    gl_trace_log_clear_depth((GLfloat)d);
}
static void hle_glClearDepthf(Cpu *c) {
    GLfloat d = F32(0);
    g_gl.p_glClearDepthf(d);
    gl_trace_log_clear_depth(d);
}
static void hle_glClearStencil(Cpu *c) {
    GLint s = I32(0);
    g_gl.p_glClearStencil(s);
    gl_trace_log_clear_stencil(s);
}
static void hle_glDepthRange(Cpu *c) { g_gl.p_glDepthRange(F64(0), F64(1)); }
static void hle_glDepthRangef(Cpu *c) { g_gl.p_glDepthRangef(F32(0), F32(1)); }
static void hle_glBlitFramebuffer(Cpu *c) {
    GLint sx0 = I32(0), sy0 = I32(1), sx1 = I32(2), sy1 = I32(3);
    GLint dx0 = I32(4), dy0 = I32(5), dx1 = I32(6), dy1 = I32(7);
    GLbitfield mask = STK_U32(0);
    GLenum filter = STK_U32(1);
    if (g_res_scale_pct > 100) {
        pthread_mutex_lock(&g_gl_lock);
        bool rs = fbo_is_scaled_locked(g_guest_read_fbo);
        bool ds = fbo_is_scaled_locked(g_guest_draw_fbo);
        pthread_mutex_unlock(&g_gl_lock);
        if (rs) {
            sx0 = (GLint)(((int64_t)sx0 * g_res_scale_pct) / 100);
            sy0 = (GLint)(((int64_t)sy0 * g_res_scale_pct) / 100);
            sx1 = (GLint)(((int64_t)sx1 * g_res_scale_pct) / 100);
            sy1 = (GLint)(((int64_t)sy1 * g_res_scale_pct) / 100);
        }
        if (ds) {
            dx0 = (GLint)(((int64_t)dx0 * g_res_scale_pct) / 100);
            dy0 = (GLint)(((int64_t)dy0 * g_res_scale_pct) / 100);
            dx1 = (GLint)(((int64_t)dx1 * g_res_scale_pct) / 100);
            dy1 = (GLint)(((int64_t)dy1 * g_res_scale_pct) / 100);
        }
    }
    g_gl.p_glBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter);
    gl_trace_log_blit_framebuffer(g_guest_read_fbo, g_guest_draw_fbo, sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1,
                                  mask, filter, trace_poll_err());
}
static void hle_glIsEnabled(Cpu *c) {
    c->x[0] = (uint64_t)g_gl.p_glIsEnabled(U32(0));
}
static void hle_glGetShaderInfoLog(Cpu *c) {
    g_gl.p_glGetShaderInfoLog(U32(0), I32(1), (GLsizei *)PTR(2), (GLchar *)PTR(3));
}
static void hle_glGetProgramInfoLog(Cpu *c) {
    g_gl.p_glGetProgramInfoLog(U32(0), I32(1), (GLsizei *)PTR(2), (GLchar *)PTR(3));
}
static void hle_glGetFloatv(Cpu *c) { g_gl.p_glGetFloatv(U32(0), (GLfloat *)PTR(1)); }
static void hle_glPixelStorei(Cpu *c) { g_gl.p_glPixelStorei(U32(0), I32(1)); }
static void hle_glGetStringi(Cpu *c) {
    if (!g_gl.p_glGetStringi) { c->x[0] = 0; return; }
    const char *s = (const char *)g_gl.p_glGetStringi(U32(0), U32(1));
    if (!s) { c->x[0] = 0; return; }
    size_t len = strlen(s);
    char *dst = (char *)hle_alloc(len + 1);
    memcpy(dst, s, len + 1);
    c->x[0] = (uint64_t)(uintptr_t)dst;
}

typedef struct {
    const char *name;
    HostFn fn;
    void **proc_ptr;
    bool has_fallback;
} GlEntry;

#define GL_ENT(name) {#name, hle_##name, (void **)&g_gl.p_##name, false}
#define GL_ENT_FB(name) {#name, hle_##name, (void **)&g_gl.p_##name, true}

static const GlEntry g_gl_entries[] = {
    GL_ENT(glActiveTexture),
    GL_ENT(glAttachShader),
    GL_ENT(glBindBuffer),
    GL_ENT(glBindFramebuffer),
    GL_ENT(glBindRenderbuffer),
    GL_ENT(glBindTexture),
    GL_ENT(glBlendColor),
    GL_ENT(glBlendEquation),
    GL_ENT(glBlendEquationSeparate),
    GL_ENT(glBlendFunc),
    GL_ENT(glBlendFuncSeparate),
    GL_ENT(glBlitFramebuffer),
    GL_ENT(glBufferData),
    GL_ENT(glCheckFramebufferStatus),
    {"glCheckFramebufferStatusEXT", hle_glCheckFramebufferStatus, (void **)&g_gl.p_glCheckFramebufferStatus, false},
    GL_ENT(glClear),
    GL_ENT(glClearBufferfi),
    GL_ENT(glClearBufferfv),
    GL_ENT(glClearBufferiv),
    GL_ENT(glClearBufferuiv),
    GL_ENT(glClearColor),
    GL_ENT(glClearDepth),
    GL_ENT(glClearDepthf),
    GL_ENT(glClearStencil),
    GL_ENT(glColorMask),
    GL_ENT(glCompileShader),
    GL_ENT(glCompressedTexImage2D),
    GL_ENT(glCopyTexImage2D),
    GL_ENT(glCopyTexSubImage2D),
    GL_ENT(glCreateProgram),
    GL_ENT(glCreateShader),
    GL_ENT(glCullFace),
    GL_ENT(glDeleteBuffers),
    GL_ENT(glDeleteFramebuffers),
    GL_ENT(glDeleteProgram),
    GL_ENT(glDeleteRenderbuffers),
    GL_ENT(glDeleteShader),
    GL_ENT(glDeleteTextures),
    GL_ENT(glDepthFunc),
    GL_ENT(glDepthMask),
    GL_ENT(glDepthRange),
    GL_ENT(glDepthRangef),
    GL_ENT(glDisable),
    GL_ENT(glDisableVertexAttribArray),
    GL_ENT(glDrawArrays),
    GL_ENT(glDrawBuffer),
    GL_ENT(glDrawBuffers),
    GL_ENT(glDrawElements),
    GL_ENT(glEnable),
    GL_ENT(glEnableVertexAttribArray),
    GL_ENT(glFinish),
    GL_ENT(glFlush),
    GL_ENT(glFramebufferRenderbuffer),
    GL_ENT(glFramebufferTexture2D),
    GL_ENT(glFrontFace),
    GL_ENT(glGenBuffers),
    GL_ENT(glGenFramebuffers),
    GL_ENT(glGenRenderbuffers),
    GL_ENT(glGenTextures),
    GL_ENT(glGenerateMipmap),
    GL_ENT(glGetActiveAttrib),
    GL_ENT(glGetActiveUniform),
    GL_ENT(glGetAttribLocation),
    GL_ENT(glGetBooleanv),
    GL_ENT(glGetError),
    GL_ENT(glGetFloatv),
    GL_ENT(glGetIntegerv),
    GL_ENT(glGetProgramInfoLog),
    GL_ENT(glGetProgramiv),
    GL_ENT(glGetShaderInfoLog),
    GL_ENT_FB(glGetShaderPrecisionFormat),
    GL_ENT(glGetShaderiv),
    GL_ENT(glGetString),
    GL_ENT(glGetStringi),
    GL_ENT(glGetUniformLocation),
    GL_ENT(glIsEnabled),
    GL_ENT(glLinkProgram),
    GL_ENT(glPixelStorei),
    GL_ENT(glPolygonOffset),
    GL_ENT(glReadBuffer),
    GL_ENT(glReadPixels),
    GL_ENT(glRenderbufferStorage),
    GL_ENT(glScissor),
    GL_ENT(glShaderSource),
    GL_ENT(glStencilFunc),
    GL_ENT(glStencilFuncSeparate),
    GL_ENT(glStencilMask),
    GL_ENT(glStencilMaskSeparate),
    GL_ENT(glStencilOp),
    GL_ENT(glStencilOpSeparate),
    GL_ENT(glTexImage2D),
    GL_ENT(glTexParameterf),
    GL_ENT(glTexParameterfv),
    GL_ENT(glTexParameteri),
    GL_ENT(glTexSubImage2D),
    GL_ENT(glUniform1f),
    GL_ENT(glUniform1fv),
    GL_ENT(glUniform1i),
    GL_ENT(glUniform1iv),
    GL_ENT(glUniform2f),
    GL_ENT(glUniform2fv),
    GL_ENT(glUniform2i),
    GL_ENT(glUniform2iv),
    GL_ENT(glUniform3f),
    GL_ENT(glUniform3fv),
    GL_ENT(glUniform3i),
    GL_ENT(glUniform3iv),
    GL_ENT(glUniform4f),
    GL_ENT(glUniform4fv),
    GL_ENT(glUniform4i),
    GL_ENT(glUniform4iv),
    GL_ENT(glUniformMatrix2fv),
    GL_ENT(glUniformMatrix3fv),
    GL_ENT(glUniformMatrix4fv),
    GL_ENT(glUseProgram),
    GL_ENT(glVertexAttrib1fv),
    GL_ENT(glVertexAttrib2fv),
    GL_ENT(glVertexAttrib3fv),
    GL_ENT(glVertexAttrib4fv),
    GL_ENT(glVertexAttribPointer),
    GL_ENT(glViewport),
};

uint64_t gl_hle_lookup_trampoline(const char *name) {
    if (!name || !*name) return 0;
    for (size_t i = 0; i < sizeof g_gl_entries / sizeof g_gl_entries[0]; i++) {
        const GlEntry *e = &g_gl_entries[i];
        if (strcmp(e->name, name) != 0) continue;
        if (!*e->proc_ptr) {
            *e->proc_ptr = gl_backend_get_proc_address(name);
            if (!*e->proc_ptr) {
                LOG("[gl] WARN: host OpenGL driver does not expose %s%s\n",
                    name, e->has_fallback ? " (using HLE fallback)" : " (returning NULL)");
                if (!e->has_fallback) {
                    __atomic_add_fetch(&g_epa_misses, 1, __ATOMIC_RELAXED);
                    gl_trace_on_epa_miss(name);
                    return 0;
                }
            }
        }
        __atomic_add_fetch(&g_epa_hits, 1, __ATOMIC_RELAXED);
        return hle_register(e->name, e->fn);
    }
    __atomic_add_fetch(&g_epa_misses, 1, __ATOMIC_RELAXED);
    gl_trace_on_epa_miss(name);
    return 0;
}
