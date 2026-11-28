// Lightweight F9 single-frame OpenGL HLE tracer and shader dumper (YW1_GL_TRACE_HOTKEY=1).
#include "gl_trace.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "../rt.h"

#ifndef GL_NO_ERROR
#define GL_NO_ERROR 0
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT 0x8D00
#endif
#ifndef GL_STENCIL_ATTACHMENT
#define GL_STENCIL_ATTACHMENT 0x8D20
#endif
#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COLOR_BUFFER_BIT
#define GL_COLOR_BUFFER_BIT 0x00004000
#endif
#ifndef GL_DEPTH_BUFFER_BIT
#define GL_DEPTH_BUFFER_BIT 0x00000100
#endif
#ifndef GL_STENCIL_BUFFER_BIT
#define GL_STENCIL_BUFFER_BIT 0x00000400
#endif

#define MAX_SHADERS     1024
#define MAX_PROGRAMS    512
#define MAX_OBJS        4096
#define MAX_FBOS        512
#define MAX_EPA_MISSES  512

typedef struct {
    GLuint id;
    GLenum type;
    uint64_t hash;
    bool has_fc, has_ts, has_tf, has_dx, has_gs;
    bool compile_ok;
    bool dumped;
    char *src;
    size_t len;
    char *compile_log;
    char dump_rel[128];
} TraceShader;

typedef struct {
    GLuint id;
    GLuint shaders[8];
    int n_shaders;
    bool link_ok;
    char *link_log;
    int n_samplers;
    char sampler_names[16][64];
    GLenum sampler_types[16];
    GLint sampler_locs[16];
} TraceProgram;

typedef struct {
    GLuint id;
    GLenum fmt;
    GLsizei guest_w, guest_h;
    GLsizei real_w, real_h;
    bool scaled;
    bool is_fbo_rt;
    bool copytex_dst;
    bool used;
} TraceObj;

typedef struct {
    bool used;
    bool is_rbo;
    GLuint obj_id;
} TraceAttach;

typedef struct {
    GLuint fbo;
    bool used;
    TraceAttach color[4];
    TraceAttach depth;
    TraceAttach stencil;
    TraceAttach depth_stencil;
} TraceFbo;

static pthread_mutex_t g_trace_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_int g_hotkey_enabled = -1;
static atomic_bool g_trace_pending = false;
static atomic_bool g_trace_active = false;
static atomic_uint_fast32_t g_stashed_err = 0;

static FILE *g_fp = NULL;
static char g_trace_dir[512];
static uint64_t g_trace_frame = 0;
static uint64_t g_seq = 0;
static uint64_t g_frame_draws = 0;
static uint64_t g_frame_errs = 0;
static int g_frame_shaders_dumped = 0;

static TraceShader g_shaders[MAX_SHADERS];
static TraceProgram g_programs[MAX_PROGRAMS];
static TraceObj g_tex_meta[MAX_OBJS];
static TraceObj g_rbo_meta[MAX_OBJS];
static TraceFbo g_fbo_meta[MAX_FBOS];
static char g_epa_miss_names[MAX_EPA_MISSES][64];
static int g_n_epa_misses = 0;

// Pipeline snapshot tracked on guest render thread
static GLuint g_cur_prog = 0;
static int g_active_tex_unit = 0;
static GLuint g_bound_tex2d[16];
static GLboolean g_scissor_en = 0, g_stencil_en = 0, g_blend_en = 0, g_depth_en = 0, g_cull_en = 0;
static GLboolean g_cmask[4] = {1, 1, 1, 1};
static GLboolean g_dmask = 1;
static GLuint g_smask = 0xFFFFFFFFu, g_sval_mask = 0xFFFFFFFFu;
static GLenum g_sfunc = 0x0207 /* GL_ALWAYS */, g_sfail = 0x1E00, g_szfail = 0x1E00, g_szpass = 0x1E00;
static GLint g_sref = 0, g_clear_stencil = 0;
static GLenum g_blend_eq_rgb = 0x8006, g_blend_eq_a = 0x8006;
static GLenum g_blend_src_rgb = 1, g_blend_dst_rgb = 0, g_blend_src_a = 1, g_blend_dst_a = 0;
static GLint g_vp_guest[4], g_vp_real[4], g_sc_guest[4], g_sc_real[4];
static bool g_vp_scaled = false, g_sc_scaled = false;

static uint64_t fnv1a64(const char *s, size_t n) {
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < n; i++) {
        h ^= (uint8_t)s[i];
        h *= 1099511628211ull;
    }
    return h;
}

static const char *resolve_trace_dir(void) {
    if (g_trace_dir[0]) return g_trace_dir;
    const char *env = getenv("YW1_GL_TRACE_DIR");
    if (env && *env) {
        snprintf(g_trace_dir, sizeof g_trace_dir, "%s", env);
        return g_trace_dir;
    }
    snprintf(g_trace_dir, sizeof g_trace_dir, ".");
    return g_trace_dir;
}

static void ensure_dir_exists(const char *path) {
#ifdef _WIN32
    CreateDirectoryA(path, NULL);
#else
    (void)path;
#endif
}

bool gl_trace_hotkey_enabled(void) {
    int v = atomic_load_explicit(&g_hotkey_enabled, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("YW1_GL_TRACE_HOTKEY");
        v = (e && *e && strcmp(e, "0") != 0) ? 1 : 0;
        atomic_store_explicit(&g_hotkey_enabled, v, memory_order_relaxed);
    }
    return v == 1;
}

bool gl_trace_is_active(void) {
    return gl_trace_hotkey_enabled() && atomic_load_explicit(&g_trace_active, memory_order_acquire);
}

void gl_trace_request(void) {
    if (!gl_trace_hotkey_enabled()) return;
    atomic_store_explicit(&g_trace_pending, true, memory_order_release);
    LOG("[gl-trace] F9 armed: next full frame will be captured to %s\\lens_trace.txt\n", resolve_trace_dir());
}

const char *gl_trace_dir_path(void) {
    const char *d = resolve_trace_dir();
    ensure_dir_exists(d);
    return d;
}

uint64_t gl_trace_current_frame(void) {
    return g_trace_frame;
}

GLenum gl_trace_pop_stashed_error(void) {
    return (GLenum)atomic_exchange_explicit(&g_stashed_err, 0, memory_order_acq_rel);
}

void gl_trace_record_error_fn(GLenum err) {
    if (err == GL_NO_ERROR) return;
    atomic_compare_exchange_strong(&g_stashed_err, &(uint32_t){0}, (uint32_t)err);
}

static const char *err_note(GLenum err) {
    static char buf[48];
    if (err == GL_NO_ERROR) return "";
    g_frame_errs++;
    gl_trace_record_error_fn(err);
    const char *nm = (err == 0x0500) ? "INVALID_ENUM"
                   : (err == 0x0501) ? "INVALID_VALUE"
                   : (err == 0x0502) ? "INVALID_OPERATION"
                   : (err == 0x0505) ? "OUT_OF_MEMORY"
                   : (err == 0x0506) ? "INVALID_FRAMEBUFFER_OPERATION"
                                     : "ERR";
    snprintf(buf, sizeof buf, "  [glErr=0x%04X %s]", (unsigned)err, nm);
    return buf;
}

static TraceObj *obj_slot(TraceObj *tab, GLuint id, bool create) {
    if (!id) return NULL;
    uint32_t idx = (uint32_t)(id * 2654435761u) & (MAX_OBJS - 1);
    for (uint32_t i = 0; i < MAX_OBJS; i++) {
        TraceObj *s = &tab[(idx + i) & (MAX_OBJS - 1)];
        if (s->used && s->id == id) return s;
        if (!s->used) {
            if (!create) return NULL;
            memset(s, 0, sizeof *s);
            s->id = id;
            s->used = true;
            return s;
        }
    }
    return NULL;
}

static TraceFbo *fbo_slot(GLuint fbo, bool create) {
    if (!fbo) return NULL;
    uint32_t idx = (uint32_t)(fbo * 2654435761u) & (MAX_FBOS - 1);
    for (uint32_t i = 0; i < MAX_FBOS; i++) {
        TraceFbo *s = &g_fbo_meta[(idx + i) & (MAX_FBOS - 1)];
        if (s->used && s->fbo == fbo) return s;
        if (!s->used) {
            if (!create) return NULL;
            memset(s, 0, sizeof *s);
            s->fbo = fbo;
            s->used = true;
            return s;
        }
    }
    return NULL;
}

static TraceShader *find_shader(GLuint id, bool create) {
    if (!id) return NULL;
    int free_idx = -1;
    for (int i = 0; i < MAX_SHADERS; i++) {
        if (g_shaders[i].id == id) return &g_shaders[i];
        if (g_shaders[i].id == 0 && free_idx < 0) free_idx = i;
    }
    if (create && free_idx >= 0) {
        memset(&g_shaders[free_idx], 0, sizeof g_shaders[free_idx]);
        g_shaders[free_idx].id = id;
        return &g_shaders[free_idx];
    }
    return NULL;
}

static TraceProgram *find_program(GLuint id, bool create) {
    if (!id) return NULL;
    int free_idx = -1;
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        if (g_programs[i].id == id) return &g_programs[i];
        if (g_programs[i].id == 0 && free_idx < 0) free_idx = i;
    }
    if (create && free_idx >= 0) {
        memset(&g_programs[free_idx], 0, sizeof g_programs[free_idx]);
        g_programs[free_idx].id = id;
        return &g_programs[free_idx];
    }
    return NULL;
}

static const char *fmt_name(GLenum f) {
    switch (f) {
    case 0x8058: return "RGBA8";
    case 0x8C43: return "SRGB8_A8";
    case 0x881A: return "RGBA16F";
    case 0x8814: return "RGBA32F";
    case 0x8C3A: return "R11F_G11F_B10F";
    case 0x8229: return "R8";
    case 0x822B: return "RG8";
    case 0x822D: return "R16F";
    case 0x822F: return "RG16F";
    case 0x88F0: return "DEPTH24_STENCIL8";
    case 0x81A6: return "DEPTH24";
    case 0x8CAC: return "DEPTH32F";
    case 0x8CAD: return "DEPTH32F_STENCIL8";
    case 0x1908: return "RGBA";
    case 0x1902: return "DEPTH_COMPONENT";
    case 0x84F9: return "DEPTH_STENCIL";
    default: return "";
    }
}

static const char *cap_name(GLenum cap) {
    switch (cap) {
    case 0x0B44: return "CULL_FACE";
    case 0x0B71: return "DEPTH_TEST";
    case 0x0B90: return "STENCIL_TEST";
    case 0x0BE2: return "BLEND";
    case 0x0C11: return "SCISSOR_TEST";
    case 0x0BD0: return "DITHER";
    case 0x8037: return "POLYGON_OFFSET_FILL";
    default: return "CAP";
    }
}

static const char *cmp_name(GLenum f) {
    switch (f) {
    case 0x0200: return "NEVER";
    case 0x0201: return "LESS";
    case 0x0202: return "EQUAL";
    case 0x0203: return "LEQUAL";
    case 0x0204: return "GREATER";
    case 0x0205: return "NOTEQUAL";
    case 0x0206: return "GEQUAL";
    case 0x0207: return "ALWAYS";
    default: return "?";
    }
}

static const char *op_name(GLenum o) {
    switch (o) {
    case 0x0000: return "ZERO";
    case 0x1E00: return "KEEP";
    case 0x1E01: return "REPLACE";
    case 0x1E02: return "INCR";
    case 0x1E03: return "DECR";
    case 0x150A: return "INVERT";
    case 0x8507: return "INCR_WRAP";
    case 0x8508: return "DECR_WRAP";
    default: return "?";
    }
}

static const char *prim_name(GLenum m) {
    switch (m) {
    case 0: return "POINTS";
    case 1: return "LINES";
    case 3: return "LINE_STRIP";
    case 4: return "TRIANGLES";
    case 5: return "TRIANGLE_STRIP";
    case 6: return "TRIANGLE_FAN";
    default: return "PRIM";
    }
}

static void dump_shader_if_needed(GLuint prog_id, TraceShader *s) {
    if (!s || !s->src || s->dumped) return;
    const char *base = resolve_trace_dir();
    char sh_dir[512];
    snprintf(sh_dir, sizeof sh_dir, "%s\\lens_shaders", base);
    ensure_dir_exists(sh_dir);
    const char *stg = (s->type == GL_VERTEX_SHADER) ? "vs" : (s->type == GL_FRAGMENT_SHADER) ? "fs" : "sh";
    snprintf(s->dump_rel, sizeof s->dump_rel, "lens_shaders/prog_%u_%s_%u_%016llx.glsl",
             prog_id, stg, s->id, (unsigned long long)s->hash);
    char full_path[640];
    snprintf(full_path, sizeof full_path, "%s\\%s", base, s->dump_rel);
    FILE *sf = fopen(full_path, "wb");
    if (sf) {
        fwrite(s->src, 1, s->len, sf);
        fclose(sf);
        s->dumped = true;
        g_frame_shaders_dumped++;
    }
}

static void format_attach(const char *label, const TraceAttach *a, char *out, size_t cap) {
    if (!a->used) { out[0] = 0; return; }
    TraceObj *o = obj_slot(a->is_rbo ? g_rbo_meta : g_tex_meta, a->obj_id, false);
    if (o) {
        const char *fn = fmt_name(o->fmt);
        snprintf(out, cap, " %s=%s#%u(%dx%d->%dx%d,%s0x%04x,s=%d)",
                 label, a->is_rbo ? "rbo" : "tex", a->obj_id,
                 (int)o->guest_w, (int)o->guest_h, (int)o->real_w, (int)o->real_h,
                 fn[0] ? fn : "", (unsigned)o->fmt, o->scaled ? 1 : 0);
    } else {
        snprintf(out, cap, " %s=%s#%u(?)", label, a->is_rbo ? "rbo" : "tex", a->obj_id);
    }
}

void gl_trace_on_epa_miss(const char *name) {
    if (!gl_trace_hotkey_enabled() || !name || !*name) return;
    pthread_mutex_lock(&g_trace_lock);
    for (int i = 0; i < g_n_epa_misses; i++) {
        if (strcmp(g_epa_miss_names[i], name) == 0) {
            pthread_mutex_unlock(&g_trace_lock);
            return;
        }
    }
    if (g_n_epa_misses < MAX_EPA_MISSES) {
        snprintf(g_epa_miss_names[g_n_epa_misses++], 64, "%s", name);
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_on_create_shader(GLenum type, GLuint shader) {
    if (!gl_trace_hotkey_enabled() || !shader) return;
    pthread_mutex_lock(&g_trace_lock);
    TraceShader *s = find_shader(shader, true);
    if (s) s->type = type;
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_on_delete_shader(GLuint shader) {
    // Keep compiled source metadata alive after glDeleteShader (games commonly delete
    // shader objects immediately after glLinkProgram while still using the program).
    (void)shader;
}

void gl_trace_on_shader_source(GLuint shader, const char *full_src, size_t len,
                               bool has_fc, bool has_ts, bool has_tf, bool has_dx, bool has_gs) {
    if (!gl_trace_hotkey_enabled() || !shader || !full_src) return;
    pthread_mutex_lock(&g_trace_lock);
    TraceShader *s = find_shader(shader, true);
    if (s) {
        while (len > 0 && full_src[len - 1] == '\0') len--;
        free(s->src);
        s->src = malloc(len + 1);
        if (s->src) {
            memcpy(s->src, full_src, len);
            s->src[len] = 0;
            s->len = len;
            s->hash = fnv1a64(full_src, len);
        }
        s->has_fc = has_fc;
        s->has_ts = has_ts;
        s->has_tf = has_tf;
        s->has_dx = has_dx;
        s->has_gs = has_gs;
        s->dumped = false;
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_on_compile_shader(GLuint shader, GLint ok, const char *info_log) {
    if (!gl_trace_hotkey_enabled() || !shader) return;
    pthread_mutex_lock(&g_trace_lock);
    TraceShader *s = find_shader(shader, true);
    if (s) {
        s->compile_ok = (ok != 0);
        free(s->compile_log);
        s->compile_log = (!ok && info_log && *info_log) ? strdup(info_log) : NULL;
    }
    if (g_fp && !ok) {
        fprintf(g_fp, "[#%04llu] COMPILE_FAIL shader=%u log=%s\n",
                (unsigned long long)++g_seq, shader, (info_log && *info_log) ? info_log : "(empty)");
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_on_create_program(GLuint prog) {
    if (!gl_trace_hotkey_enabled() || !prog) return;
    pthread_mutex_lock(&g_trace_lock);
    find_program(prog, true);
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_on_delete_program(GLuint prog) {
    if (!gl_trace_hotkey_enabled() || !prog) return;
    pthread_mutex_lock(&g_trace_lock);
    TraceProgram *p = find_program(prog, false);
    if (p) {
        free(p->link_log);
        memset(p, 0, sizeof *p);
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_on_attach_shader(GLuint prog, GLuint shader) {
    if (!gl_trace_hotkey_enabled() || !prog || !shader) return;
    pthread_mutex_lock(&g_trace_lock);
    TraceProgram *p = find_program(prog, true);
    if (p) {
        for (int i = 0; i < p->n_shaders; i++) {
            if (p->shaders[i] == shader) {
                pthread_mutex_unlock(&g_trace_lock);
                return;
            }
        }
        if (p->n_shaders < 8) p->shaders[p->n_shaders++] = shader;
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_on_link_program(GLuint prog, GLint ok, const char *info_log,
                              int n_samplers, const char (*sampler_names)[64],
                              const GLenum *sampler_types, const GLint *sampler_locs) {
    if (!gl_trace_hotkey_enabled() || !prog) return;
    pthread_mutex_lock(&g_trace_lock);
    TraceProgram *p = find_program(prog, true);
    if (p) {
        p->link_ok = (ok != 0);
        free(p->link_log);
        p->link_log = (!ok && info_log && *info_log) ? strdup(info_log) : NULL;
        p->n_samplers = (n_samplers > 16) ? 16 : (n_samplers < 0 ? 0 : n_samplers);
        for (int i = 0; i < p->n_samplers; i++) {
            snprintf(p->sampler_names[i], 64, "%s", sampler_names[i]);
            p->sampler_types[i] = sampler_types[i];
            p->sampler_locs[i] = sampler_locs[i];
        }
    }
    if (g_fp && !ok) {
        fprintf(g_fp, "[#%04llu] LINK_FAIL prog=%u log=%s\n",
                (unsigned long long)++g_seq, prog, (info_log && *info_log) ? info_log : "(empty)");
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_on_tex_image_2d(GLuint tex_id, GLenum target, GLint int_fmt, GLsizei guest_w, GLsizei guest_h,
                              GLsizei real_w, GLsizei real_h, bool scaled, GLenum err) {
    if (!gl_trace_hotkey_enabled() || !tex_id) return;
    pthread_mutex_lock(&g_trace_lock);
    TraceObj *o = obj_slot(g_tex_meta, tex_id, true);
    if (o) {
        o->fmt = (GLenum)int_fmt;
        o->guest_w = guest_w;
        o->guest_h = guest_h;
        o->real_w = real_w;
        o->real_h = real_h;
        o->scaled = scaled;
        o->is_fbo_rt = false;
        o->copytex_dst = false;
    }
    if (g_fp) {
        fprintf(g_fp, "[#%04llu] glTexImage2D(target=0x%04x, tex=%u, fmt=%s(0x%04x), guest=%dx%d -> real=%dx%d, scaled=%d)%s\n",
                (unsigned long long)++g_seq, (unsigned)target, tex_id, fmt_name((GLenum)int_fmt),
                (unsigned)int_fmt, (int)guest_w, (int)guest_h, (int)real_w, (int)real_h, scaled ? 1 : 0, err_note(err));
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_on_rbo_storage(GLuint rbo_id, GLenum fmt, GLsizei guest_w, GLsizei guest_h,
                             GLsizei real_w, GLsizei real_h, bool scaled, GLenum err) {
    if (!gl_trace_hotkey_enabled() || !rbo_id) return;
    pthread_mutex_lock(&g_trace_lock);
    TraceObj *o = obj_slot(g_rbo_meta, rbo_id, true);
    if (o) {
        o->fmt = fmt;
        o->guest_w = guest_w;
        o->guest_h = guest_h;
        o->real_w = real_w;
        o->real_h = real_h;
        o->scaled = scaled;
    }
    if (g_fp) {
        fprintf(g_fp, "[#%04llu] glRenderbufferStorage(rbo=%u, fmt=%s(0x%04x), guest=%dx%d -> real=%dx%d, scaled=%d)%s\n",
                (unsigned long long)++g_seq, rbo_id, fmt_name(fmt), (unsigned)fmt,
                (int)guest_w, (int)guest_h, (int)real_w, (int)real_h, scaled ? 1 : 0, err_note(err));
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_on_fbo_attach(GLuint guest_fbo, GLenum attachment, bool is_rbo, GLuint obj_id, GLenum err) {
    if (!gl_trace_hotkey_enabled() || !guest_fbo) return;
    pthread_mutex_lock(&g_trace_lock);
    TraceFbo *f = fbo_slot(guest_fbo, true);
    if (f) {
        TraceAttach *dst = NULL;
        if (attachment >= GL_COLOR_ATTACHMENT0 && attachment < GL_COLOR_ATTACHMENT0 + 4) {
            dst = &f->color[attachment - GL_COLOR_ATTACHMENT0];
        } else if (attachment == GL_DEPTH_ATTACHMENT) {
            dst = &f->depth;
        } else if (attachment == GL_STENCIL_ATTACHMENT) {
            dst = &f->stencil;
        } else if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
            dst = &f->depth_stencil;
        }
        if (dst) {
            dst->used = (obj_id != 0);
            dst->is_rbo = is_rbo;
            dst->obj_id = obj_id;
        }
    }
    if (!is_rbo && obj_id) {
        TraceObj *to = obj_slot(g_tex_meta, obj_id, true);
        if (to) to->is_fbo_rt = true;
    }
    if (g_fp) {
        const char *att_s = (attachment == GL_COLOR_ATTACHMENT0) ? "COLOR0"
                          : (attachment == GL_DEPTH_ATTACHMENT) ? "DEPTH"
                          : (attachment == GL_STENCIL_ATTACHMENT) ? "STENCIL"
                          : (attachment == GL_DEPTH_STENCIL_ATTACHMENT) ? "DEPTH_STENCIL"
                                                                        : "ATTACH";
        fprintf(g_fp, "[#%04llu] %s(fbo=%u, %s(0x%04x), %s=%u)%s\n",
                (unsigned long long)++g_seq,
                is_rbo ? "glFramebufferRenderbuffer" : "glFramebufferTexture2D",
                guest_fbo, att_s, (unsigned)attachment, is_rbo ? "rbo" : "tex", obj_id, err_note(err));
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_on_delete_fbo(GLuint fbo) {
    if (!gl_trace_hotkey_enabled() || !fbo) return;
    pthread_mutex_lock(&g_trace_lock);
    TraceFbo *f = fbo_slot(fbo, false);
    if (f) memset(f, 0, sizeof *f);
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_on_copy_tex_sub_image_2d(GLuint tex_id, GLuint read_fbo, GLenum target, GLint level,
                                       GLint xoff, GLint yoff, GLint x, GLint y, GLsizei w, GLsizei h,
                                       bool read_fbo_scaled, GLenum err) {
    if (!gl_trace_hotkey_enabled()) return;
    pthread_mutex_lock(&g_trace_lock);
    TraceObj *to = obj_slot(g_tex_meta, tex_id, true);
    if (to) to->copytex_dst = true;
    if (g_fp) {
        fprintf(g_fp,
                "[#%04llu] glCopyTexSubImage2D(target=0x%04x, lvl=%d, dst_tex=%u (%dx%d s=%d), "
                "read_fbo=%u(scaled=%d), dst=(%d,%d), src_rect=(%d,%d,%dx%d))%s\n",
                (unsigned long long)++g_seq, (unsigned)target, level, tex_id,
                to ? (int)to->real_w : 0, to ? (int)to->real_h : 0, (to && to->scaled) ? 1 : 0,
                read_fbo, read_fbo_scaled ? 1 : 0, xoff, yoff, x, y, (int)w, (int)h, err_note(err));
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_bind_fbo(GLenum target, GLuint guest_fb, GLuint real_fb, bool draw_scaled,
                           GLuint vfbo_id, int vfbo_w, int vfbo_h, int res_scale_pct,
                           GLuint vfbo_color, GLuint vfbo_depth, GLenum err) {
    if (!gl_trace_is_active()) return;
    pthread_mutex_lock(&g_trace_lock);
    if (g_fp) {
        const char *tgt = (target == GL_FRAMEBUFFER) ? "FRAMEBUFFER"
                        : (target == GL_DRAW_FRAMEBUFFER) ? "DRAW_FRAMEBUFFER"
                        : (target == GL_READ_FRAMEBUFFER) ? "READ_FRAMEBUFFER"
                                                          : "FBO_TARGET";
        if (guest_fb == 0 && vfbo_id != 0) {
            fprintf(g_fp,
                    "[#%04llu] glBindFramebuffer(%s, guest=0 -> real=%u [VIRTUAL], guest=1920x1080 real=%dx%d, "
                    "res_scale=%.1fx scaled=%d, color=tex#%u(RGBA8), depth_stencil=rbo#%u(D24S8))%s\n",
                    (unsigned long long)++g_seq, tgt, real_fb, vfbo_w, vfbo_h,
                    (double)res_scale_pct / 100.0, (res_scale_pct > 100) ? 1 : 0,
                    vfbo_color, vfbo_depth, err_note(err));
        } else {
            TraceFbo *f = fbo_slot(guest_fb, false);
            char c0[96] = "", c1[96] = "", d[96] = "", st[96] = "", ds[96] = "";
            int gw = 0, gh = 0, rw = 0, rh = 0;
            if (f) {
                format_attach("c0", &f->color[0], c0, sizeof c0);
                format_attach("c1", &f->color[1], c1, sizeof c1);
                format_attach("d", &f->depth, d, sizeof d);
                format_attach("s", &f->stencil, st, sizeof st);
                format_attach("ds", &f->depth_stencil, ds, sizeof ds);
                TraceObj *ref = f->color[0].used
                                    ? obj_slot(f->color[0].is_rbo ? g_rbo_meta : g_tex_meta, f->color[0].obj_id, false)
                                    : (f->depth_stencil.used
                                           ? obj_slot(f->depth_stencil.is_rbo ? g_rbo_meta : g_tex_meta, f->depth_stencil.obj_id, false)
                                           : (f->depth.used ? obj_slot(f->depth.is_rbo ? g_rbo_meta : g_tex_meta, f->depth.obj_id, false) : NULL));
                if (ref) { gw = ref->guest_w; gh = ref->guest_h; rw = ref->real_w; rh = ref->real_h; }
            }
            fprintf(g_fp,
                    "[#%04llu] glBindFramebuffer(%s, guest=%u -> real=%u, guest=%dx%d real=%dx%d, res_scale=%d%% scaled=%d,%s%s%s%s%s)%s\n",
                    (unsigned long long)++g_seq, tgt, guest_fb, real_fb, gw, gh, rw, rh,
                    res_scale_pct, draw_scaled ? 1 : 0, c0, c1, d, st, ds, err_note(err));
        }
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_viewport(GLint gx, GLint gy, GLsizei gw, GLsizei gh,
                           GLint rx, GLint ry, GLsizei rw, GLsizei rh,
                           bool scaled, GLuint draw_fbo, GLenum err) {
    if (!gl_trace_hotkey_enabled()) return;
    pthread_mutex_lock(&g_trace_lock);
    g_vp_guest[0] = gx; g_vp_guest[1] = gy; g_vp_guest[2] = gw; g_vp_guest[3] = gh;
    g_vp_real[0] = rx; g_vp_real[1] = ry; g_vp_real[2] = rw; g_vp_real[3] = rh;
    g_vp_scaled = scaled;
    if (g_fp) {
        fprintf(g_fp, "[#%04llu] glViewport(guest=(%d,%d,%dx%d) -> driver=(%d,%d,%dx%d), scaled=%d, draw_fbo=%u)%s\n",
                (unsigned long long)++g_seq, gx, gy, (int)gw, (int)gh, rx, ry, (int)rw, (int)rh,
                scaled ? 1 : 0, draw_fbo, err_note(err));
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_scissor(GLint gx, GLint gy, GLsizei gw, GLsizei gh,
                          GLint rx, GLint ry, GLsizei rw, GLsizei rh,
                          bool scaled, GLuint draw_fbo, GLenum err) {
    if (!gl_trace_hotkey_enabled()) return;
    pthread_mutex_lock(&g_trace_lock);
    g_sc_guest[0] = gx; g_sc_guest[1] = gy; g_sc_guest[2] = gw; g_sc_guest[3] = gh;
    g_sc_real[0] = rx; g_sc_real[1] = ry; g_sc_real[2] = rw; g_sc_real[3] = rh;
    g_sc_scaled = scaled;
    if (g_fp) {
        fprintf(g_fp, "[#%04llu] glScissor(guest=(%d,%d,%dx%d) -> driver=(%d,%d,%dx%d), scaled=%d, draw_fbo=%u)%s\n",
                (unsigned long long)++g_seq, gx, gy, (int)gw, (int)gh, rx, ry, (int)rw, (int)rh,
                scaled ? 1 : 0, draw_fbo, err_note(err));
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_enable_cap(GLenum cap, bool enable, GLenum err) {
    if (!gl_trace_hotkey_enabled()) return;
    pthread_mutex_lock(&g_trace_lock);
    if (cap == 0x0C11) g_scissor_en = enable;
    else if (cap == 0x0B90) g_stencil_en = enable;
    else if (cap == 0x0BE2) g_blend_en = enable;
    else if (cap == 0x0B71) g_depth_en = enable;
    else if (cap == 0x0B44) g_cull_en = enable;
    if (g_fp) {
        fprintf(g_fp, "[#%04llu] %s(%s 0x%04x)%s\n",
                (unsigned long long)++g_seq, enable ? "glEnable" : "glDisable",
                cap_name(cap), (unsigned)cap, err_note(err));
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_clear(GLbitfield mask, GLenum err) {
    if (!gl_trace_is_active()) return;
    pthread_mutex_lock(&g_trace_lock);
    if (g_fp) {
        fprintf(g_fp, "[#%04llu] glClear(0x%x [%s%s%s], cmask=%d%d%d%d dmask=%d smask=0x%x clear_s=%d)%s\n",
                (unsigned long long)++g_seq, (unsigned)mask,
                (mask & GL_COLOR_BUFFER_BIT) ? "C" : "",
                (mask & GL_DEPTH_BUFFER_BIT) ? "D" : "",
                (mask & GL_STENCIL_BUFFER_BIT) ? "S" : "",
                g_cmask[0], g_cmask[1], g_cmask[2], g_cmask[3], g_dmask,
                (unsigned)g_smask, g_clear_stencil, err_note(err));
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_clear_buffer(const char *kind, GLenum buffer, GLint drawbuffer, GLenum err) {
    if (!gl_trace_is_active()) return;
    pthread_mutex_lock(&g_trace_lock);
    if (g_fp) {
        fprintf(g_fp, "[#%04llu] %s(buf=0x%04x, idx=%d)%s\n",
                (unsigned long long)++g_seq, kind, (unsigned)buffer, drawbuffer, err_note(err));
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_clear_buffer_f(const char *kind, GLenum buffer, GLint drawbuffer, const GLfloat *val, GLenum err) {
    if (!gl_trace_is_active()) return;
    pthread_mutex_lock(&g_trace_lock);
    if (g_fp) {
        if (!val) {
            fprintf(g_fp, "[#%04llu] %s(buf=0x%04x, idx=%d, val=NULL)%s\n",
                    (unsigned long long)++g_seq, kind, (unsigned)buffer, drawbuffer, err_note(err));
        } else if (buffer == 0x1800) {
            fprintf(g_fp, "[#%04llu] %s(buf=0x%04x, idx=%d, val=(%.3f,%.3f,%.3f,%.3f))%s\n",
                    (unsigned long long)++g_seq, kind, (unsigned)buffer, drawbuffer,
                    val[0], val[1], val[2], val[3], err_note(err));
        } else {
            fprintf(g_fp, "[#%04llu] %s(buf=0x%04x, idx=%d, val=%.4f)%s\n",
                    (unsigned long long)++g_seq, kind, (unsigned)buffer, drawbuffer, val[0], err_note(err));
        }
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_clear_buffer_i(const char *kind, GLenum buffer, GLint drawbuffer, const GLint *val, GLenum err) {
    if (!gl_trace_is_active()) return;
    pthread_mutex_lock(&g_trace_lock);
    if (g_fp) {
        if (!val) {
            fprintf(g_fp, "[#%04llu] %s(buf=0x%04x, idx=%d, val=NULL)%s\n",
                    (unsigned long long)++g_seq, kind, (unsigned)buffer, drawbuffer, err_note(err));
        } else if (buffer == 0x1800) {
            fprintf(g_fp, "[#%04llu] %s(buf=0x%04x, idx=%d, val=(%d,%d,%d,%d))%s\n",
                    (unsigned long long)++g_seq, kind, (unsigned)buffer, drawbuffer,
                    val[0], val[1], val[2], val[3], err_note(err));
        } else {
            fprintf(g_fp, "[#%04llu] %s(buf=0x%04x, idx=%d, val=%d)%s\n",
                    (unsigned long long)++g_seq, kind, (unsigned)buffer, drawbuffer, val[0], err_note(err));
        }
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_clear_buffer_u(const char *kind, GLenum buffer, GLint drawbuffer, const GLuint *val, GLenum err) {
    if (!gl_trace_is_active()) return;
    pthread_mutex_lock(&g_trace_lock);
    if (g_fp) {
        if (!val) {
            fprintf(g_fp, "[#%04llu] %s(buf=0x%04x, idx=%d, val=NULL)%s\n",
                    (unsigned long long)++g_seq, kind, (unsigned)buffer, drawbuffer, err_note(err));
        } else if (buffer == 0x1800) {
            fprintf(g_fp, "[#%04llu] %s(buf=0x%04x, idx=%d, val=(%u,%u,%u,%u))%s\n",
                    (unsigned long long)++g_seq, kind, (unsigned)buffer, drawbuffer,
                    val[0], val[1], val[2], val[3], err_note(err));
        } else {
            fprintf(g_fp, "[#%04llu] %s(buf=0x%04x, idx=%d, val=%u)%s\n",
                    (unsigned long long)++g_seq, kind, (unsigned)buffer, drawbuffer, val[0], err_note(err));
        }
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_clear_buffer_fi(GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil, GLenum err) {
    if (!gl_trace_is_active()) return;
    pthread_mutex_lock(&g_trace_lock);
    if (g_fp) {
        fprintf(g_fp, "[#%04llu] glClearBufferfi(buf=0x%04x, idx=%d, depth=%.4f, stencil=%d)%s\n",
                (unsigned long long)++g_seq, (unsigned)buffer, drawbuffer, depth, stencil, err_note(err));
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_clear_color(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    if (!gl_trace_is_active()) return;
    pthread_mutex_lock(&g_trace_lock);
    if (g_fp) fprintf(g_fp, "[#%04llu] glClearColor(%.3f, %.3f, %.3f, %.3f)\n", (unsigned long long)++g_seq, r, g, b, a);
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_clear_depth(GLfloat d) {
    if (!gl_trace_is_active()) return;
    pthread_mutex_lock(&g_trace_lock);
    if (g_fp) fprintf(g_fp, "[#%04llu] glClearDepthf(%.4f)\n", (unsigned long long)++g_seq, d);
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_clear_stencil(GLint s) {
    if (!gl_trace_hotkey_enabled()) return;
    pthread_mutex_lock(&g_trace_lock);
    g_clear_stencil = s;
    if (g_fp) fprintf(g_fp, "[#%04llu] glClearStencil(%d)\n", (unsigned long long)++g_seq, s);
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_color_mask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    if (!gl_trace_hotkey_enabled()) return;
    pthread_mutex_lock(&g_trace_lock);
    g_cmask[0] = r; g_cmask[1] = g; g_cmask[2] = b; g_cmask[3] = a;
    if (g_fp) fprintf(g_fp, "[#%04llu] glColorMask(%d, %d, %d, %d)\n", (unsigned long long)++g_seq, r, g, b, a);
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_depth_mask(GLboolean flag) {
    if (!gl_trace_hotkey_enabled()) return;
    pthread_mutex_lock(&g_trace_lock);
    g_dmask = flag;
    if (g_fp) fprintf(g_fp, "[#%04llu] glDepthMask(%d)\n", (unsigned long long)++g_seq, flag);
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_depth_func(GLenum func) {
    if (!gl_trace_is_active()) return;
    pthread_mutex_lock(&g_trace_lock);
    if (g_fp) fprintf(g_fp, "[#%04llu] glDepthFunc(%s 0x%04x)\n", (unsigned long long)++g_seq, cmp_name(func), (unsigned)func);
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_stencil_func(GLenum func, GLint ref, GLuint mask) {
    if (!gl_trace_hotkey_enabled()) return;
    pthread_mutex_lock(&g_trace_lock);
    g_sfunc = func; g_sref = ref; g_sval_mask = mask;
    if (g_fp) {
        fprintf(g_fp, "[#%04llu] glStencilFunc(%s 0x%04x, ref=%d, mask=0x%x)\n",
                (unsigned long long)++g_seq, cmp_name(func), (unsigned)func, ref, (unsigned)mask);
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_stencil_op(GLenum sfail, GLenum dpfail, GLenum dppass) {
    if (!gl_trace_hotkey_enabled()) return;
    pthread_mutex_lock(&g_trace_lock);
    g_sfail = sfail; g_szfail = dpfail; g_szpass = dppass;
    if (g_fp) {
        fprintf(g_fp, "[#%04llu] glStencilOp(sfail=%s, dpfail=%s, dppass=%s)\n",
                (unsigned long long)++g_seq, op_name(sfail), op_name(dpfail), op_name(dppass));
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_stencil_mask(GLuint mask) {
    if (!gl_trace_hotkey_enabled()) return;
    pthread_mutex_lock(&g_trace_lock);
    g_smask = mask;
    if (g_fp) fprintf(g_fp, "[#%04llu] glStencilMask(0x%x)\n", (unsigned long long)++g_seq, (unsigned)mask);
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_stencil_func_sep(GLenum face, GLenum func, GLint ref, GLuint mask) {
    if (!gl_trace_hotkey_enabled()) return;
    pthread_mutex_lock(&g_trace_lock);
    if (face == 0x0404 || face == 0x0408) {
        g_sfunc = func; g_sref = ref; g_sval_mask = mask;
    }
    if (g_fp) {
        fprintf(g_fp, "[#%04llu] glStencilFuncSeparate(face=0x%04x, %s 0x%04x, ref=%d, mask=0x%x)\n",
                (unsigned long long)++g_seq, (unsigned)face, cmp_name(func), (unsigned)func, ref, (unsigned)mask);
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_stencil_op_sep(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass) {
    if (!gl_trace_hotkey_enabled()) return;
    pthread_mutex_lock(&g_trace_lock);
    if (face == 0x0404 || face == 0x0408) {
        g_sfail = sfail; g_szfail = dpfail; g_szpass = dppass;
    }
    if (g_fp) {
        fprintf(g_fp, "[#%04llu] glStencilOpSeparate(face=0x%04x, sfail=%s, dpfail=%s, dppass=%s)\n",
                (unsigned long long)++g_seq, (unsigned)face, op_name(sfail), op_name(dpfail), op_name(dppass));
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_stencil_mask_sep(GLenum face, GLuint mask) {
    if (!gl_trace_hotkey_enabled()) return;
    pthread_mutex_lock(&g_trace_lock);
    if (face == 0x0404 || face == 0x0408) g_smask = mask;
    if (g_fp) {
        fprintf(g_fp, "[#%04llu] glStencilMaskSeparate(face=0x%04x, mask=0x%x)\n",
                (unsigned long long)++g_seq, (unsigned)face, (unsigned)mask);
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_copy_tex_image_2d(GLuint tex_id, GLuint read_fbo, GLenum target, GLint level,
                                    GLenum int_fmt, GLint x, GLint y, GLsizei w, GLsizei h,
                                    bool read_fbo_scaled, GLenum err) {
    if (!gl_trace_hotkey_enabled()) return;
    pthread_mutex_lock(&g_trace_lock);
    TraceObj *to = obj_slot(g_tex_meta, tex_id, true);
    if (to) {
        to->fmt = int_fmt;
        to->guest_w = w; to->guest_h = h;
        to->real_w = w; to->real_h = h;
        to->scaled = false;
        to->copytex_dst = true;
    }
    if (g_fp) {
        fprintf(g_fp,
                "[#%04llu] glCopyTexImage2D(target=0x%04x, lvl=%d, dst_tex=%u, fmt=%s(0x%04x), "
                "read_fbo=%u(scaled=%d), src_rect=(%d,%d,%dx%d))%s\n",
                (unsigned long long)++g_seq, (unsigned)target, level, tex_id,
                fmt_name(int_fmt), (unsigned)int_fmt, read_fbo, read_fbo_scaled ? 1 : 0,
                x, y, (int)w, (int)h, err_note(err));
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_blit_framebuffer(GLuint read_fbo, GLuint draw_fbo,
                                   GLint sx0, GLint sy0, GLint sx1, GLint sy1,
                                   GLint dx0, GLint dy0, GLint dx1, GLint dy1,
                                   GLbitfield mask, GLenum filter, GLenum err) {
    if (!gl_trace_is_active()) return;
    pthread_mutex_lock(&g_trace_lock);
    if (g_fp) {
        fprintf(g_fp,
                "[#%04llu] glBlitFramebuffer(read=%u(%d,%d..%d,%d) -> draw=%u(%d,%d..%d,%d), mask=0x%x, filter=0x%04x)%s\n",
                (unsigned long long)++g_seq, read_fbo, sx0, sy0, sx1, sy1,
                draw_fbo, dx0, dy0, dx1, dy1, (unsigned)mask, (unsigned)filter, err_note(err));
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_blend_eq(GLenum rgb, GLenum alpha) {
    if (!gl_trace_hotkey_enabled()) return;
    pthread_mutex_lock(&g_trace_lock);
    g_blend_eq_rgb = rgb; g_blend_eq_a = alpha;
    if (g_fp) fprintf(g_fp, "[#%04llu] glBlendEquationSeparate(rgb=0x%04x, a=0x%04x)\n",
                      (unsigned long long)++g_seq, (unsigned)rgb, (unsigned)alpha);
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_blend_func(GLenum src_rgb, GLenum dst_rgb, GLenum src_a, GLenum dst_a) {
    if (!gl_trace_hotkey_enabled()) return;
    pthread_mutex_lock(&g_trace_lock);
    g_blend_src_rgb = src_rgb; g_blend_dst_rgb = dst_rgb; g_blend_src_a = src_a; g_blend_dst_a = dst_a;
    if (g_fp) {
        fprintf(g_fp, "[#%04llu] glBlendFuncSeparate(srcRGB=0x%04x, dstRGB=0x%04x, srcA=0x%04x, dstA=0x%04x)\n",
                (unsigned long long)++g_seq, (unsigned)src_rgb, (unsigned)dst_rgb, (unsigned)src_a, (unsigned)dst_a);
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_use_program(GLuint prog, GLenum err) {
    if (!gl_trace_hotkey_enabled()) return;
    pthread_mutex_lock(&g_trace_lock);
    g_cur_prog = prog;
    if (g_fp) {
        TraceProgram *p = find_program(prog, false);
        if (p) {
            bool any_fc = false, any_ts = false, any_tf = false, any_dx = false, any_gs = false;
            char sh_desc[512] = "";
            int pos = 0;
            for (int i = 0; i < p->n_shaders; i++) {
                TraceShader *s = find_shader(p->shaders[i], false);
                if (!s) continue;
                dump_shader_if_needed(prog, s);
                any_fc |= s->has_fc; any_ts |= s->has_ts; any_tf |= s->has_tf; any_dx |= s->has_dx; any_gs |= s->has_gs;
                pos += snprintf(sh_desc + pos, sizeof sh_desc - pos, "%s%s",
                                pos ? ", " : "", s->dump_rel[0] ? s->dump_rel : "(no-src)");
                if (pos >= (int)sizeof sh_desc - 32) break;
            }
            char samp_desc[384] = "";
            int spos = 0;
            for (int i = 0; i < p->n_samplers; i++) {
                spos += snprintf(samp_desc + spos, sizeof samp_desc - spos, "%s%s@%d",
                                 spos ? ", " : "", p->sampler_names[i], p->sampler_locs[i]);
                if (spos >= (int)sizeof samp_desc - 32) break;
            }
            fprintf(g_fp,
                    "[#%04llu] glUseProgram(%u) link_ok=%d fc=%d ts=%d tf=%d df=%d gauss=%d samplers=[%s] shaders=[%s]%s\n",
                    (unsigned long long)++g_seq, prog, p->link_ok ? 1 : 0,
                    any_fc ? 1 : 0, any_ts ? 1 : 0, any_tf ? 1 : 0, any_dx ? 1 : 0, any_gs ? 1 : 0,
                    samp_desc, sh_desc, err_note(err));
        } else {
            fprintf(g_fp, "[#%04llu] glUseProgram(%u) [untracked]%s\n", (unsigned long long)++g_seq, prog, err_note(err));
        }
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_active_texture(GLenum tex_unit) {
    if (!gl_trace_hotkey_enabled()) return;
    pthread_mutex_lock(&g_trace_lock);
    int u = (int)tex_unit - GL_TEXTURE0;
    if (u >= 0 && u < 16) g_active_tex_unit = u;
    if (g_fp) fprintf(g_fp, "[#%04llu] glActiveTexture(TEXTURE%d)\n", (unsigned long long)++g_seq, u);
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_bind_texture(GLenum target, GLuint tex, GLenum err) {
    if (!gl_trace_hotkey_enabled()) return;
    pthread_mutex_lock(&g_trace_lock);
    if (target == 0x0DE1 /* GL_TEXTURE_2D */ && g_active_tex_unit >= 0 && g_active_tex_unit < 16) {
        g_bound_tex2d[g_active_tex_unit] = tex;
    }
    if (g_fp) {
        TraceObj *o = obj_slot(g_tex_meta, tex, false);
        if (o) {
            const char *fn = fmt_name(o->fmt);
            fprintf(g_fp,
                    "[#%04llu] glBindTexture(u=%d, target=0x%04x, tex=%u, guest=%dx%d real=%dx%d, fmt=%s(0x%04x), scaled=%d, fbo_rt=%d, copytex=%d)%s\n",
                    (unsigned long long)++g_seq, g_active_tex_unit, (unsigned)target, tex,
                    (int)o->guest_w, (int)o->guest_h, (int)o->real_w, (int)o->real_h,
                    fn, (unsigned)o->fmt, o->scaled ? 1 : 0, o->is_fbo_rt ? 1 : 0, o->copytex_dst ? 1 : 0, err_note(err));
        } else {
            fprintf(g_fp, "[#%04llu] glBindTexture(u=%d, target=0x%04x, tex=%u, untracked)%s\n",
                    (unsigned long long)++g_seq, g_active_tex_unit, (unsigned)target, tex, err_note(err));
        }
    }
    pthread_mutex_unlock(&g_trace_lock);
}

static void log_draw_common(const char *fn, GLenum mode, GLint first_or_count, GLsizei count_or_type,
                            const void *indices_or_null, bool indexed, GLuint draw_fbo, GLenum err) {
    if (!gl_trace_is_active()) return;
    pthread_mutex_lock(&g_trace_lock);
    g_frame_draws++;
    if (g_fp) {
        fprintf(g_fp,
                "[#%04llu] %s(%s, %s%d, fbo=%u prog=%u st=%d(%s ref=%d m=0x%x wm=0x%x) sc=%d bl=%d d=%d/%d cm=%d%d%d%d t0=%u t1=%u)%s\n",
                (unsigned long long)++g_seq, fn, prim_name(mode),
                indexed ? "cnt=" : "first=",
                indexed ? (int)count_or_type : first_or_count,
                draw_fbo, g_cur_prog,
                g_stencil_en ? 1 : 0, cmp_name(g_sfunc), g_sref, (unsigned)g_sval_mask, (unsigned)g_smask,
                g_scissor_en ? 1 : 0, g_blend_en ? 1 : 0,
                g_depth_en ? 1 : 0, g_dmask ? 1 : 0,
                g_cmask[0], g_cmask[1], g_cmask[2], g_cmask[3],
                g_bound_tex2d[0], g_bound_tex2d[1], err_note(err));
        (void)indices_or_null;
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_draw_arrays(GLenum mode, GLint first, GLsizei count, GLuint draw_fbo, GLenum err) {
    if (!gl_trace_is_active()) return;
    pthread_mutex_lock(&g_trace_lock);
    g_frame_draws++;
    if (g_fp) {
        fprintf(g_fp,
                "[#%04llu] glDrawArrays(%s, first=%d, cnt=%d, fbo=%u prog=%u st=%d(%s ref=%d wm=0x%x) sc=%d bl=%d d=%d/%d cm=%d%d%d%d t0=%u t1=%u)%s\n",
                (unsigned long long)++g_seq, prim_name(mode), first, (int)count,
                draw_fbo, g_cur_prog,
                g_stencil_en ? 1 : 0, cmp_name(g_sfunc), g_sref, (unsigned)g_smask,
                g_scissor_en ? 1 : 0, g_blend_en ? 1 : 0,
                g_depth_en ? 1 : 0, g_dmask ? 1 : 0,
                g_cmask[0], g_cmask[1], g_cmask[2], g_cmask[3],
                g_bound_tex2d[0], g_bound_tex2d[1], err_note(err));
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_log_draw_elements(GLenum mode, GLsizei count, GLenum type, const void *indices,
                                GLuint draw_fbo, GLenum err) {
    log_draw_common("glDrawElements", mode, 0, count, indices, true, draw_fbo, err);
    (void)type;
}

void gl_trace_log_blit_vfbo(GLuint vfbo_id, int src_w, int src_h, int dx, int dy, int dw, int dh) {
    if (!gl_trace_is_active()) return;
    pthread_mutex_lock(&g_trace_lock);
    if (g_fp) {
        fprintf(g_fp, "[#%04llu] [BLIT_VFBO] src=vfbo#%u(%dx%d) -> window(%d,%d,%dx%d)\n",
                (unsigned long long)++g_seq, vfbo_id, src_w, src_h, dx, dy, dw, dh);
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_on_frame_swap_end(uint64_t just_finished_frame, int res_scale_pct, int vfbo_w, int vfbo_h,
                                GLuint draw_fbo, GLuint read_fbo, GLuint vfbo_id,
                                GLuint vfbo_color, GLuint vfbo_depth) {
    if (!gl_trace_hotkey_enabled()) return;
    pthread_mutex_lock(&g_trace_lock);
    if (atomic_load_explicit(&g_trace_active, memory_order_acquire)) {
        if (g_fp) {
            fprintf(g_fp,
                    "[#%04llu] === END FRAME %llu (eglSwapBuffers): draws=%llu glErrors=%llu shaders_dumped=%d ===\n",
                    (unsigned long long)++g_seq, (unsigned long long)g_trace_frame,
                    (unsigned long long)g_frame_draws, (unsigned long long)g_frame_errs, g_frame_shaders_dumped);
            fclose(g_fp);
            g_fp = NULL;
        }
        atomic_store_explicit(&g_trace_active, false, memory_order_release);
        LOG("[gl-trace] captured frame %llu -> %s\\lens_trace.txt (draws=%llu shaders_dumped=%d glErrors=%llu)\n",
            (unsigned long long)g_trace_frame, resolve_trace_dir(),
            (unsigned long long)g_frame_draws, g_frame_shaders_dumped, (unsigned long long)g_frame_errs);
    } else {
        static int64_t s_auto_frame = -2;
        if (s_auto_frame == -2) {
            const char *af = getenv("YW1_GL_TRACE_FRAME");
            s_auto_frame = (af && *af) ? strtoll(af, NULL, 10) : -1;
        }
        if (s_auto_frame > 0 && just_finished_frame + 1 == (uint64_t)s_auto_frame) {
            atomic_store_explicit(&g_trace_pending, true, memory_order_release);
        }
        if (atomic_exchange_explicit(&g_trace_pending, false, memory_order_acq_rel)) {
        const char *dir = resolve_trace_dir();
        ensure_dir_exists(dir);
        char tr_path[640];
        snprintf(tr_path, sizeof tr_path, "%s\\lens_trace.txt", dir);
        g_fp = fopen(tr_path, "w");
        g_trace_frame = just_finished_frame + 1;
        g_seq = 0;
        g_frame_draws = 0;
        g_frame_errs = 0;
        g_frame_shaders_dumped = 0;
        if (g_fp) {
            fprintf(g_fp, "=== YokaiWatch1Recomp Single-Frame GL Trace | FRAME=%llu res_scale=%.1fx vfbo=#%u(%dx%d color=%u ds=%u) ===\n",
                    (unsigned long long)g_trace_frame, (double)res_scale_pct / 100.0,
                    vfbo_id, vfbo_w, vfbo_h, vfbo_color, vfbo_depth);
            fprintf(g_fp, "Initial state: draw_fbo=%u read_fbo=%u prog=%u vp_guest=(%d,%d,%dx%d) vp_real=(%d,%d,%dx%d,s=%d) sc_real=(%d,%d,%dx%d,s=%d) "
                          "sc=%d st=%d(func=%s ref=%d m=0x%x wm=0x%x op=%s/%s/%s) bl=%d(eq=0x%x/0x%x fn=0x%x,0x%x,0x%x,0x%x) cull=%d d=%d/%d cm=%d%d%d%d\n",
                    draw_fbo, read_fbo, g_cur_prog,
                    g_vp_guest[0], g_vp_guest[1], g_vp_guest[2], g_vp_guest[3],
                    g_vp_real[0], g_vp_real[1], g_vp_real[2], g_vp_real[3], g_vp_scaled ? 1 : 0,
                    g_sc_real[0], g_sc_real[1], g_sc_real[2], g_sc_real[3], g_sc_scaled ? 1 : 0,
                    g_scissor_en, g_stencil_en, cmp_name(g_sfunc), g_sref, (unsigned)g_sval_mask, (unsigned)g_smask,
                    op_name(g_sfail), op_name(g_szfail), op_name(g_szpass),
                    g_blend_en, (unsigned)g_blend_eq_rgb, (unsigned)g_blend_eq_a,
                    (unsigned)g_blend_src_rgb, (unsigned)g_blend_dst_rgb, (unsigned)g_blend_src_a, (unsigned)g_blend_dst_a,
                    g_cull_en, g_depth_en, g_dmask,
                    g_cmask[0], g_cmask[1], g_cmask[2], g_cmask[3]);
            fprintf(g_fp, "eglGetProcAddress misses seen (%d):", g_n_epa_misses);
            for (int i = 0; i < g_n_epa_misses; i++) fprintf(g_fp, " %s", g_epa_miss_names[i]);
            fprintf(g_fp, "\n--------------------------------------------------------------------------------\n");
            atomic_store_explicit(&g_trace_active, true, memory_order_release);
        } else {
            LOG("[gl-trace] ERROR: failed to open %s\n", tr_path);
        }
        }
    }
    pthread_mutex_unlock(&g_trace_lock);
}

void gl_trace_reset(void) {
    pthread_mutex_lock(&g_trace_lock);
    if (g_fp) {
        fclose(g_fp);
        g_fp = NULL;
    }
    atomic_store_explicit(&g_trace_active, false, memory_order_release);
    atomic_store_explicit(&g_trace_pending, false, memory_order_release);
    atomic_store_explicit(&g_stashed_err, 0, memory_order_release);
    for (int i = 0; i < MAX_SHADERS; i++) {
        free(g_shaders[i].src);
        free(g_shaders[i].compile_log);
    }
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        free(g_programs[i].link_log);
    }
    memset(g_shaders, 0, sizeof g_shaders);
    memset(g_programs, 0, sizeof g_programs);
    memset(g_tex_meta, 0, sizeof g_tex_meta);
    memset(g_rbo_meta, 0, sizeof g_rbo_meta);
    memset(g_fbo_meta, 0, sizeof g_fbo_meta);
    memset(g_epa_miss_names, 0, sizeof g_epa_miss_names);
    g_n_epa_misses = 0;
    pthread_mutex_unlock(&g_trace_lock);
}
