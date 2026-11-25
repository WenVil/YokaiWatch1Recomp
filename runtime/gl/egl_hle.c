// EGL HLE (14 subsdk0/opengl.nss symbols) bridged onto gl_backend / gl_hle.
#include <stdint.h>
#include <stdio.h>

#include "../cpu.h"
#include "../hle.h"
#include "../rt.h"
#include "gl_backend.h"

#define EGL_FAKE_DISPLAY ((uint64_t)1)
#define EGL_FAKE_CONFIG  ((uint64_t)1)
#define EGL_FAKE_CONTEXT ((uint64_t)1)
#define EGL_FAKE_SURFACE ((uint64_t)1)
#define EGL_NONE         0x3038

static void log_attribs(const char *fn, const int32_t *a) {
    if (!a) {
        LOG("[egl] %s attribs: (null)\n", fn);
        return;
    }
    char buf[512];
    int pos = 0;
    for (int i = 0; i < 32; i += 2) {
        int32_t k = a[i];
        if (k == EGL_NONE) break;
        int32_t v = a[i + 1];
        pos += snprintf(buf + pos, sizeof buf - pos, "%s0x%x=0x%x", pos ? ", " : "", (unsigned)k, (unsigned)v);
        if (pos >= (int)sizeof buf - 32) break;
    }
    LOG("[egl] %s attribs: {%s}\n", fn, pos ? buf : "empty");
}

HLE_FUNC("eglGetDisplay", hle_eglGetDisplay) {
    LOG("[egl] eglGetDisplay(%#llx) -> 1\n", (unsigned long long)c->x[0]);
    c->x[0] = EGL_FAKE_DISPLAY;
}

HLE_FUNC("eglInitialize", hle_eglInitialize) {
    int32_t *maj = (int32_t *)(uintptr_t)c->x[1];
    int32_t *min = (int32_t *)(uintptr_t)c->x[2];
    if (maj) *maj = 1;
    if (min) *min = 4;
    LOG("[egl] eglInitialize(dpy=%llu) -> 1.4\n", (unsigned long long)c->x[0]);
    c->x[0] = 1;
}

HLE_FUNC("eglTerminate", hle_eglTerminate) {
    LOG("[egl] eglTerminate(dpy=%llu)\n", (unsigned long long)c->x[0]);
    gl_backend_make_current(true);
    c->x[0] = 1;
}

HLE_FUNC("eglBindAPI", hle_eglBindAPI) {
    uint32_t api = (uint32_t)c->x[0];
    LOG("[egl] eglBindAPI(%#x)\n", api);
    c->x[0] = 1;
}

HLE_FUNC("eglChooseConfig", hle_eglChooseConfig) {
    const int32_t *attrib = (const int32_t *)(uintptr_t)c->x[1];
    uint64_t *configs = (uint64_t *)(uintptr_t)c->x[2];
    int32_t cfg_size = (int32_t)(uint32_t)c->x[3];
    int32_t *num_cfg = (int32_t *)(uintptr_t)c->x[4];
    log_attribs("eglChooseConfig", attrib);
    if (configs && cfg_size >= 1) configs[0] = EGL_FAKE_CONFIG;
    if (num_cfg) *num_cfg = 1;
    c->x[0] = 1;
}

HLE_FUNC("eglCreateContext", hle_eglCreateContext) {
    const int32_t *attrib = (const int32_t *)(uintptr_t)c->x[3];
    log_attribs("eglCreateContext", attrib);
    c->x[0] = EGL_FAKE_CONTEXT;
}

HLE_FUNC("eglDestroyContext", hle_eglDestroyContext) {
    LOG("[egl] eglDestroyContext(ctx=%llu)\n", (unsigned long long)c->x[1]);
    c->x[0] = 1;
}

HLE_FUNC("eglCreateWindowSurface", hle_eglCreateWindowSurface) {
    const int32_t *attrib = (const int32_t *)(uintptr_t)c->x[3];
    log_attribs("eglCreateWindowSurface", attrib);
    LOG("[egl] eglCreateWindowSurface(win=%#llx) -> 1\n", (unsigned long long)c->x[2]);
    c->x[0] = EGL_FAKE_SURFACE;
}

HLE_FUNC("eglDestroySurface", hle_eglDestroySurface) {
    LOG("[egl] eglDestroySurface(surf=%llu)\n", (unsigned long long)c->x[1]);
    c->x[0] = 1;
}

HLE_FUNC("eglMakeCurrent", hle_eglMakeCurrent) {
    uint64_t draw = c->x[1], read = c->x[2], ctx = c->x[3];
    bool make_null = (draw == 0 && read == 0 && ctx == 0);
    static int nlog;
    if (++nlog <= 6) {
        LOG("[egl] eglMakeCurrent(draw=%llu, read=%llu, ctx=%llu)\n",
            (unsigned long long)draw, (unsigned long long)read, (unsigned long long)ctx);
    }
    c->x[0] = gl_backend_make_current(make_null) ? 1 : 0;
}

HLE_FUNC("eglReleaseThread", hle_eglReleaseThread) {
    gl_backend_make_current(true);
    c->x[0] = 1;
}

HLE_FUNC("eglSwapBuffers", hle_eglSwapBuffers) {
    gl_backend_swap_buffers();
    c->x[0] = 1;
}

HLE_FUNC("eglSwapInterval", hle_eglSwapInterval) {
    int interval = (int)(int32_t)(uint32_t)c->x[1];
    LOG("[egl] eglSwapInterval(%d)\n", interval);
    gl_backend_set_swap_interval(interval);
    c->x[0] = 1;
}

HLE_FUNC("eglGetProcAddress", hle_eglGetProcAddress) {
    const char *name = (const char *)(uintptr_t)c->x[0];
    c->x[0] = gl_hle_lookup_trampoline(name);
}
