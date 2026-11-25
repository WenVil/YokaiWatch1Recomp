// OpenGL 4.5 Compatibility HLE thunks (all 90 closed gl* functions + diagnostics & ASTC decode fallback).
#include <SDL3/SDL_opengl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../cpu.h"
#include "../hle.h"
#include "../nvn/astc.h"
#include "../rt.h"
#include "../simd.h"
#include "gl_backend.h"

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

// ---- Explicit APIENTRY typedefs for all closed GL functions + helpers
typedef void(APIENTRY *PFN_glActiveTexture)(GLenum);
typedef void(APIENTRY *PFN_glAttachShader)(GLuint, GLuint);
typedef void(APIENTRY *PFN_glBindBuffer)(GLenum, GLuint);
typedef void(APIENTRY *PFN_glBindFramebuffer)(GLenum, GLuint);
typedef void(APIENTRY *PFN_glBindRenderbuffer)(GLenum, GLuint);
typedef void(APIENTRY *PFN_glBindTexture)(GLenum, GLuint);
typedef void(APIENTRY *PFN_glBlendEquationSeparate)(GLenum, GLenum);
typedef void(APIENTRY *PFN_glBlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
typedef void(APIENTRY *PFN_glBufferData)(GLenum, ptrdiff_t, const void *, GLenum);
typedef void(APIENTRY *PFN_glClearBufferfv)(GLenum, GLint, const GLfloat *);
typedef void(APIENTRY *PFN_glClearBufferiv)(GLenum, GLint, const GLint *);
typedef void(APIENTRY *PFN_glColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
typedef void(APIENTRY *PFN_glCompileShader)(GLuint);
typedef void(APIENTRY *PFN_glCompressedTexImage2D)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *);
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
typedef void(APIENTRY *PFN_glStencilMask)(GLuint);
typedef void(APIENTRY *PFN_glStencilOp)(GLenum, GLenum, GLenum);
typedef void(APIENTRY *PFN_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
typedef void(APIENTRY *PFN_glTexParameterf)(GLenum, GLenum, GLfloat);
typedef void(APIENTRY *PFN_glTexParameterfv)(GLenum, GLenum, const GLfloat *);
typedef void(APIENTRY *PFN_glTexParameteri)(GLenum, GLenum, GLint);
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
typedef void(APIENTRY *PFN_glClearDepthf)(GLfloat);
typedef void(APIENTRY *PFN_glClearStencil)(GLint);
typedef const GLubyte *(APIENTRY *PFN_glGetStringi)(GLenum, GLuint);
typedef void(APIENTRY *PFN_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void(APIENTRY *PFN_glGetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void(APIENTRY *PFN_glGetFloatv)(GLenum, GLfloat *);
typedef void(APIENTRY *PFN_glPixelStorei)(GLenum, GLint);
typedef void(APIENTRY *PFN_glReadBuffer)(GLenum);

#define GL_ALL_PROCS(X) \
    X(glActiveTexture) \
    X(glAttachShader) \
    X(glBindBuffer) \
    X(glBindFramebuffer) \
    X(glBindRenderbuffer) \
    X(glBindTexture) \
    X(glBlendEquationSeparate) \
    X(glBlendFuncSeparate) \
    X(glBufferData) \
    X(glClearBufferfv) \
    X(glClearBufferiv) \
    X(glColorMask) \
    X(glCompileShader) \
    X(glCompressedTexImage2D) \
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
    X(glStencilMask) \
    X(glStencilOp) \
    X(glTexImage2D) \
    X(glTexParameterf) \
    X(glTexParameterfv) \
    X(glTexParameteri) \
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
    X(glClearDepthf) \
    X(glClearStencil) \
    X(glGetStringi) \
    X(glGetShaderInfoLog) \
    X(glGetProgramInfoLog) \
    X(glGetFloatv) \
    X(glPixelStorei) \
    X(glReadBuffer)

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
} g_comp_fmts[32];
static int g_ncomp_fmts;

RT_ON_RESET(gl_hle_reset) {
    pthread_mutex_lock(&g_gl_lock);
    g_str_cache_n = 0;
    memset(g_str_cache, 0, sizeof g_str_cache);
    memset(g_shader_src, 0, sizeof g_shader_src);
    g_shader_ok = g_shader_fail = g_prog_ok = g_prog_fail = g_shader_src_logs = 0;
    g_ncomp_fmts = 0;
    memset(g_comp_fmts, 0, sizeof g_comp_fmts);
    pthread_mutex_unlock(&g_gl_lock);
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
        LOG("[gl] host driver: vendor=\"%s\" renderer=\"%s\" version=\"%s\" glsl=\"%s\"\n",
            ven ? ven : "(null)", ren ? ren : "(null)", ver ? ver : "(null)", glsl ? glsl : "(null)");
    }
    pthread_mutex_unlock(&g_gl_lock);
}

void gl_hle_check_post_swap_errors(uint64_t frame_num) {
    if (!g_gl.p_glGetError) return;
    for (int i = 0; i < 8; i++) {
        GLenum err = g_gl.p_glGetError();
        if (err == GL_NO_ERROR) break;
        LOG("[gl] frame %llu glGetError() = 0x%04x\n", (unsigned long long)frame_num, (unsigned)err);
    }
}

void gl_hle_read_backbuffer_rgb(int w, int h, uint8_t *bgr_out) {
    if (!g_gl.p_glReadPixels || !g_gl.p_glGetIntegerv || !g_gl.p_glPixelStorei) return;
    GLint prev_read_fbo = 0, prev_pack = 4;
    g_gl.p_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);
    g_gl.p_glGetIntegerv(GL_PACK_ALIGNMENT, &prev_pack);
    if (g_gl.p_glBindFramebuffer && prev_read_fbo != 0) g_gl.p_glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    if (g_gl.p_glReadBuffer) g_gl.p_glReadBuffer(GL_BACK);
    g_gl.p_glPixelStorei(GL_PACK_ALIGNMENT, 4);
    g_gl.p_glReadPixels(0, 0, w, h, GL_BGR_EXT, GL_UNSIGNED_BYTE, bgr_out);
    g_gl.p_glPixelStorei(GL_PACK_ALIGNMENT, prev_pack);
    if (g_gl.p_glBindFramebuffer && prev_read_fbo != 0) g_gl.p_glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prev_read_fbo);
}

// ---- AAPCS64 argument accessors
#define U32(i) ((uint32_t)c->x[i])
#define I32(i) ((int32_t)(uint32_t)c->x[i])
#define U8(i)  ((GLboolean)(uint8_t)c->x[i])
#define PTR(i) ((void *)(uintptr_t)c->x[i])
#define CPTR(i) ((const void *)(uintptr_t)c->x[i])
#define SZP(i) ((ptrdiff_t)(int64_t)c->x[i])
#define STK_PTR(slot) ((const void *)(uintptr_t)*(const uint64_t *)(uintptr_t)(c->sp + (uint64_t)(slot) * 8ull))
#define F32(v) (VSF32(v))

// ---- Individual HLE wrappers for all 90 closed gl* functions + helpers
static void hle_glActiveTexture(Cpu *c) { g_gl.p_glActiveTexture(U32(0)); }
static void hle_glAttachShader(Cpu *c) { g_gl.p_glAttachShader(U32(0), U32(1)); }
static void hle_glBindBuffer(Cpu *c) { g_gl.p_glBindBuffer(U32(0), U32(1)); }
static void hle_glBindFramebuffer(Cpu *c) { g_gl.p_glBindFramebuffer(U32(0), U32(1)); }
static void hle_glBindRenderbuffer(Cpu *c) { g_gl.p_glBindRenderbuffer(U32(0), U32(1)); }
static void hle_glBindTexture(Cpu *c) { g_gl.p_glBindTexture(U32(0), U32(1)); }
static void hle_glBlendEquationSeparate(Cpu *c) { g_gl.p_glBlendEquationSeparate(U32(0), U32(1)); }
static void hle_glBlendFuncSeparate(Cpu *c) { g_gl.p_glBlendFuncSeparate(U32(0), U32(1), U32(2), U32(3)); }
static void hle_glBufferData(Cpu *c) { g_gl.p_glBufferData(U32(0), SZP(1), CPTR(2), U32(3)); }
static void hle_glClearBufferfv(Cpu *c) { g_gl.p_glClearBufferfv(U32(0), I32(1), (const GLfloat *)CPTR(2)); }
static void hle_glClearBufferiv(Cpu *c) { g_gl.p_glClearBufferiv(U32(0), I32(1), (const GLint *)CPTR(2)); }
static void hle_glColorMask(Cpu *c) { g_gl.p_glColorMask(U8(0), U8(1), U8(2), U8(3)); }

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
    if (idx < 0 && g_ncomp_fmts < 32) {
        idx = g_ncomp_fmts++;
        g_comp_fmts[idx].fmt = internalformat;
        g_comp_fmts[idx].count = 0;
        first_seen = true;
    }
    if (idx >= 0) g_comp_fmts[idx].count++;
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
                astc_decode_ldr((const uint8_t *)data, (uint32_t)width, (uint32_t)height, bw, bh, srgb, rgba);
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

static void hle_glCopyTexSubImage2D(Cpu *c) {
    g_gl.p_glCopyTexSubImage2D(U32(0), I32(1), I32(2), I32(3), I32(4), I32(5), I32(6), I32(7));
}
static void hle_glCreateProgram(Cpu *c) { c->x[0] = (uint64_t)g_gl.p_glCreateProgram(); }
static void hle_glCreateShader(Cpu *c) { c->x[0] = (uint64_t)g_gl.p_glCreateShader(U32(0)); }
static void hle_glCullFace(Cpu *c) { g_gl.p_glCullFace(U32(0)); }
static void hle_glDeleteBuffers(Cpu *c) { g_gl.p_glDeleteBuffers(I32(0), (const GLuint *)CPTR(1)); }
static void hle_glDeleteFramebuffers(Cpu *c) { g_gl.p_glDeleteFramebuffers(I32(0), (const GLuint *)CPTR(1)); }
static void hle_glDeleteProgram(Cpu *c) { g_gl.p_glDeleteProgram(U32(0)); }
static void hle_glDeleteRenderbuffers(Cpu *c) { g_gl.p_glDeleteRenderbuffers(I32(0), (const GLuint *)CPTR(1)); }
static void hle_glDeleteShader(Cpu *c) { g_gl.p_glDeleteShader(U32(0)); }
static void hle_glDeleteTextures(Cpu *c) { g_gl.p_glDeleteTextures(I32(0), (const GLuint *)CPTR(1)); }
static void hle_glDepthFunc(Cpu *c) { g_gl.p_glDepthFunc(U32(0)); }
static void hle_glDepthMask(Cpu *c) { g_gl.p_glDepthMask(U8(0)); }
static void hle_glDisable(Cpu *c) { g_gl.p_glDisable(U32(0)); }
static void hle_glDisableVertexAttribArray(Cpu *c) { g_gl.p_glDisableVertexAttribArray(U32(0)); }
static void hle_glDrawArrays(Cpu *c) { g_gl.p_glDrawArrays(U32(0), I32(1), I32(2)); }
static void hle_glDrawBuffers(Cpu *c) { g_gl.p_glDrawBuffers(I32(0), (const GLenum *)CPTR(1)); }
static void hle_glDrawElements(Cpu *c) { g_gl.p_glDrawElements(U32(0), I32(1), U32(2), CPTR(3)); }
static void hle_glEnable(Cpu *c) { g_gl.p_glEnable(U32(0)); }
static void hle_glEnableVertexAttribArray(Cpu *c) { g_gl.p_glEnableVertexAttribArray(U32(0)); }
static void hle_glFinish(Cpu *c) { (void)c; g_gl.p_glFinish(); }
static void hle_glFramebufferRenderbuffer(Cpu *c) { g_gl.p_glFramebufferRenderbuffer(U32(0), U32(1), U32(2), U32(3)); }
static void hle_glFramebufferTexture2D(Cpu *c) { g_gl.p_glFramebufferTexture2D(U32(0), U32(1), U32(2), U32(3), I32(4)); }
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
static void hle_glGetIntegerv(Cpu *c) { g_gl.p_glGetIntegerv(U32(0), (GLint *)PTR(1)); }
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
    g_gl.p_glLinkProgram(prog);
    if (g_gl.p_glGetProgramiv) {
        GLint ok = 0;
        g_gl.p_glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (ok) {
            int n = __atomic_add_fetch(&g_prog_ok, 1, __ATOMIC_RELAXED);
            if (n == 1 || n % 25 == 0) LOG("[gl] programs linked ok=%d fail=%d\n", n, g_prog_fail);
        } else {
            int nf = __atomic_add_fetch(&g_prog_fail, 1, __ATOMIC_RELAXED);
            char info[2048] = "";
            if (g_gl.p_glGetProgramInfoLog) g_gl.p_glGetProgramInfoLog(prog, sizeof info - 1, NULL, info);
            LOG("[gl] PROGRAM LINK FAILED #%u (fail_count=%d): %s\n", prog, nf, info[0] ? info : "(empty)");
        }
    }
}

static void hle_glPolygonOffset(Cpu *c) { g_gl.p_glPolygonOffset(F32(0), F32(1)); }
static void hle_glReadPixels(Cpu *c) { g_gl.p_glReadPixels(I32(0), I32(1), I32(2), I32(3), U32(4), U32(5), PTR(6)); }
static void hle_glRenderbufferStorage(Cpu *c) { g_gl.p_glRenderbufferStorage(U32(0), U32(1), I32(2), I32(3)); }
static void hle_glScissor(Cpu *c) { g_gl.p_glScissor(I32(0), I32(1), I32(2), I32(3)); }
static void hle_glStencilFunc(Cpu *c) { g_gl.p_glStencilFunc(U32(0), I32(1), U32(2)); }
static void hle_glStencilMask(Cpu *c) { g_gl.p_glStencilMask(U32(0)); }
static void hle_glStencilOp(Cpu *c) { g_gl.p_glStencilOp(U32(0), U32(1), U32(2)); }
static void hle_glTexImage2D(Cpu *c) {
    g_gl.p_glTexImage2D(U32(0), I32(1), I32(2), I32(3), I32(4), I32(5), U32(6), U32(7), STK_PTR(0));
}
static void hle_glTexParameterf(Cpu *c) { g_gl.p_glTexParameterf(U32(0), U32(1), F32(0)); }
static void hle_glTexParameterfv(Cpu *c) { g_gl.p_glTexParameterfv(U32(0), U32(1), (const GLfloat *)CPTR(2)); }
static void hle_glTexParameteri(Cpu *c) { g_gl.p_glTexParameteri(U32(0), U32(1), I32(2)); }

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

static void hle_glUseProgram(Cpu *c) { g_gl.p_glUseProgram(U32(0)); }
static void hle_glVertexAttrib1fv(Cpu *c) { g_gl.p_glVertexAttrib1fv(U32(0), (const GLfloat *)CPTR(1)); }
static void hle_glVertexAttrib2fv(Cpu *c) { g_gl.p_glVertexAttrib2fv(U32(0), (const GLfloat *)CPTR(1)); }
static void hle_glVertexAttrib3fv(Cpu *c) { g_gl.p_glVertexAttrib3fv(U32(0), (const GLfloat *)CPTR(1)); }
static void hle_glVertexAttrib4fv(Cpu *c) { g_gl.p_glVertexAttrib4fv(U32(0), (const GLfloat *)CPTR(1)); }
static void hle_glVertexAttribPointer(Cpu *c) {
    g_gl.p_glVertexAttribPointer(U32(0), I32(1), U32(2), U8(3), I32(4), CPTR(5));
}
static void hle_glViewport(Cpu *c) { g_gl.p_glViewport(I32(0), I32(1), I32(2), I32(3)); }

// Adjacent safety helpers
static void hle_glGetError(Cpu *c) { c->x[0] = (uint64_t)(g_gl.p_glGetError ? g_gl.p_glGetError() : 0); }
static void hle_glFlush(Cpu *c) { (void)c; if (g_gl.p_glFlush) g_gl.p_glFlush(); }
static void hle_glClear(Cpu *c) { g_gl.p_glClear(U32(0)); }
static void hle_glClearColor(Cpu *c) { g_gl.p_glClearColor(F32(0), F32(1), F32(2), F32(3)); }
static void hle_glClearDepthf(Cpu *c) { g_gl.p_glClearDepthf(F32(0)); }
static void hle_glClearStencil(Cpu *c) { g_gl.p_glClearStencil(I32(0)); }
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
    GL_ENT(glBlendEquationSeparate),
    GL_ENT(glBlendFuncSeparate),
    GL_ENT(glBufferData),
    GL_ENT(glClear),
    GL_ENT(glClearBufferfv),
    GL_ENT(glClearBufferiv),
    GL_ENT(glClearColor),
    GL_ENT(glClearDepthf),
    GL_ENT(glClearStencil),
    GL_ENT(glColorMask),
    GL_ENT(glCompileShader),
    GL_ENT(glCompressedTexImage2D),
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
    GL_ENT(glDisable),
    GL_ENT(glDisableVertexAttribArray),
    GL_ENT(glDrawArrays),
    GL_ENT(glDrawBuffers),
    GL_ENT(glDrawElements),
    GL_ENT(glEnable),
    GL_ENT(glEnableVertexAttribArray),
    GL_ENT(glFinish),
    GL_ENT(glFlush),
    GL_ENT(glFramebufferRenderbuffer),
    GL_ENT(glFramebufferTexture2D),
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
    GL_ENT(glLinkProgram),
    GL_ENT(glPixelStorei),
    GL_ENT(glPolygonOffset),
    GL_ENT(glReadPixels),
    GL_ENT(glRenderbufferStorage),
    GL_ENT(glScissor),
    GL_ENT(glShaderSource),
    GL_ENT(glStencilFunc),
    GL_ENT(glStencilMask),
    GL_ENT(glStencilOp),
    GL_ENT(glTexImage2D),
    GL_ENT(glTexParameterf),
    GL_ENT(glTexParameterfv),
    GL_ENT(glTexParameteri),
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
                if (!e->has_fallback) return 0;
            }
        }
        return hle_register(e->name, e->fn);
    }
    return 0;
}
