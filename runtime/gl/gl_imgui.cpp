// Shared Dear ImGui OpenGL 3.2+ (#version 150) renderer glue.
#include "gl_imgui.h"
#include "gl_backend.h"
#include "../platform.h"
#include "../rt.h"
#include "../settings.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"

#ifndef APIENTRY
#define APIENTRY
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
#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_READ_FRAMEBUFFER_BINDING
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#endif
#ifndef GL_DRAW_BUFFER
#define GL_DRAW_BUFFER 0x0C01
#endif
#ifndef GL_SCISSOR_BOX
#define GL_SCISSOR_BOX 0x0C10
#endif
#ifndef GL_SCISSOR_TEST
#define GL_SCISSOR_TEST 0x0C11
#endif
#ifndef GL_VIEWPORT
#define GL_VIEWPORT 0x0BA2
#endif
#ifndef GL_FRAMEBUFFER_SRGB
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif

extern "C" {
bool menu_prepare_frame(uint32_t target_h, bool loading);
void menu_render_opengl(int win_w, int win_h, int pix_w, int pix_h, float pixel_density);
void menu_set_open_for_test(bool open);
void menu_set_tab_for_test(int tab_idx);
}

typedef void(APIENTRY *PFN_glBindFramebuffer_t)(GLenum, GLuint);
typedef void(APIENTRY *PFN_glDrawBuffer_t)(GLenum);
typedef void(APIENTRY *PFN_glGetIntegerv_t)(GLenum, GLint *);
typedef GLboolean(APIENTRY *PFN_glIsEnabled_t)(GLenum);
typedef void(APIENTRY *PFN_glEnable_t)(GLenum);
typedef void(APIENTRY *PFN_glDisable_t)(GLenum);
typedef void(APIENTRY *PFN_glViewport_t)(GLint, GLint, GLsizei, GLsizei);
typedef void(APIENTRY *PFN_glScissor_t)(GLint, GLint, GLsizei, GLsizei);
typedef void(APIENTRY *PFN_glClearColor_t)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void(APIENTRY *PFN_glClear_t)(GLbitfield);
typedef GLenum(APIENTRY *PFN_glGetError_t)(void);

static struct {
    bool loaded;
    PFN_glBindFramebuffer_t p_glBindFramebuffer;
    PFN_glDrawBuffer_t p_glDrawBuffer;
    PFN_glGetIntegerv_t p_glGetIntegerv;
    PFN_glIsEnabled_t p_glIsEnabled;
    PFN_glEnable_t p_glEnable;
    PFN_glDisable_t p_glDisable;
    PFN_glViewport_t p_glViewport;
    PFN_glScissor_t p_glScissor;
    PFN_glClearColor_t p_glClearColor;
    PFN_glClear_t p_glClear;
    PFN_glGetError_t p_glGetError;
} s_gl;

static ImGuiContext *s_launcher_ctx = nullptr;
static SDL_Window *s_launcher_win = nullptr;
static std::atomic<uint64_t> s_overlay_gl_errors{0};
static std::atomic<uint64_t> s_overlay_frames{0};
static std::atomic<uint64_t> s_overlay_vtx_total{0};
static std::atomic<bool> s_window_apply_req{false};

static void ensure_gl_procs(void) {
    if (s_gl.loaded) return;
    s_gl.p_glBindFramebuffer = (PFN_glBindFramebuffer_t)gl_backend_get_proc_address("glBindFramebuffer");
    s_gl.p_glDrawBuffer = (PFN_glDrawBuffer_t)gl_backend_get_proc_address("glDrawBuffer");
    s_gl.p_glGetIntegerv = (PFN_glGetIntegerv_t)gl_backend_get_proc_address("glGetIntegerv");
    s_gl.p_glIsEnabled = (PFN_glIsEnabled_t)gl_backend_get_proc_address("glIsEnabled");
    s_gl.p_glEnable = (PFN_glEnable_t)gl_backend_get_proc_address("glEnable");
    s_gl.p_glDisable = (PFN_glDisable_t)gl_backend_get_proc_address("glDisable");
    s_gl.p_glViewport = (PFN_glViewport_t)gl_backend_get_proc_address("glViewport");
    s_gl.p_glScissor = (PFN_glScissor_t)gl_backend_get_proc_address("glScissor");
    s_gl.p_glClearColor = (PFN_glClearColor_t)gl_backend_get_proc_address("glClearColor");
    s_gl.p_glClear = (PFN_glClear_t)gl_backend_get_proc_address("glClear");
    s_gl.p_glGetError = (PFN_glGetError_t)gl_backend_get_proc_address("glGetError");
    s_gl.loaded = true;
}

static uint64_t drain_gl_errors_into(uint64_t frame_num, bool record_to_overlay) {
    if (!s_gl.p_glGetError) return 0;
    uint64_t n = 0;
    for (int i = 0; i < 8; i++) {
        GLenum err = s_gl.p_glGetError();
        if (err == GL_NO_ERROR) break;
        n++;
        if (record_to_overlay) {
            s_overlay_gl_errors.fetch_add(1, std::memory_order_relaxed);
            LOG("[gl_imgui] ERROR post-overlay glGetError() = 0x%04x (frame=%llu)\n",
                (unsigned)err, (unsigned long long)frame_num);
        }
        gl_hle_record_gl_error((unsigned)err, frame_num);
    }
    return n;
}

bool gl_imgui_launcher_init(void *sdl_window, void *gl_context) {
    SDL_Window *win = (SDL_Window *)sdl_window;
    if (!win || !gl_context) return false;
    gl_backend_make_current(false);
    ensure_gl_procs();

    s_launcher_win = win;
    s_launcher_ctx = ImGui::GetCurrentContext();
    if (!s_launcher_ctx) return false;

    if (!ImGui_ImplSDL3_InitForOpenGL(win, gl_context)) {
        LOG("[gl_imgui] ERROR: ImGui_ImplSDL3_InitForOpenGL failed\n");
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 150")) {
        LOG("[gl_imgui] ERROR: ImGui_ImplOpenGL3_Init failed\n");
        ImGui_ImplSDL3_Shutdown();
        return false;
    }
    drain_gl_errors_into(0, false);
    LOG("[gl_imgui] Stage A launcher ImGui OpenGL3+SDL3 initialized (glsl=#version 150)\n");
    return true;
}

void gl_imgui_launcher_shutdown(void) {
    if (!s_launcher_ctx) return;
    gl_backend_make_current(false);
    ImGui::SetCurrentContext(s_launcher_ctx);
    ImGuiIO &io = ImGui::GetIO();
    if (io.BackendRendererUserData) ImGui_ImplOpenGL3_Shutdown();
    if (io.BackendPlatformUserData) ImGui_ImplSDL3_Shutdown();
    s_launcher_ctx = nullptr;
    s_launcher_win = nullptr;
    // Release context from the main thread so the guest render thread can claim it cleanly.
    gl_backend_make_current(true);
    LOG("[gl_imgui] Stage A launcher ImGui shut down & GL context released\n");
}

bool gl_imgui_launcher_process_event(const SDL_Event *e) {
    if (!s_launcher_ctx || !e) return false;
    ImGui::SetCurrentContext(s_launcher_ctx);
    return ImGui_ImplSDL3_ProcessEvent(e);
}

void gl_imgui_launcher_begin_frame(void) {
    if (!s_launcher_ctx) return;
    gl_backend_make_current(false);
    gl_backend_apply_pending_vsync();
    ImGui::SetCurrentContext(s_launcher_ctx);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void gl_imgui_launcher_end_frame(int pix_w, int pix_h) {
    if (!s_launcher_ctx) return;
    ensure_gl_procs();
    ImGui::SetCurrentContext(s_launcher_ctx);
    ImGui::Render();

    if (s_gl.p_glBindFramebuffer) s_gl.p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (s_gl.p_glDrawBuffer) s_gl.p_glDrawBuffer(GL_BACK);
    if (s_gl.p_glIsEnabled && s_gl.p_glDisable && s_gl.p_glIsEnabled(GL_FRAMEBUFFER_SRGB)) {
        s_gl.p_glDisable(GL_FRAMEBUFFER_SRGB);
    }
    if (s_gl.p_glViewport) s_gl.p_glViewport(0, 0, pix_w, pix_h);
    if (s_gl.p_glClearColor && s_gl.p_glClear) {
        s_gl.p_glClearColor(0.05f, 0.06f, 0.09f, 1.0f);
        s_gl.p_glClear(GL_COLOR_BUFFER_BIT);
    }
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (s_launcher_win) SDL_GL_SwapWindow(s_launcher_win);
    drain_gl_errors_into(0, false);
}

bool gl_imgui_overlay_init(void) {
    ensure_gl_procs();
    ImGuiContext *ctx = ImGui::GetCurrentContext();
    if (!ctx) return false;
    ImGuiIO &io = ImGui::GetIO();
    if (io.BackendRendererUserData) return true;
    if (!ImGui_ImplOpenGL3_Init("#version 150")) {
        LOG("[gl_imgui] ERROR: Stage B ImGui_ImplOpenGL3_Init failed\n");
        return false;
    }
    LOG("[gl_imgui] Stage B overlay ImGui OpenGL3 initialized on render thread\n");
    return true;
}

void gl_imgui_overlay_shutdown(void) {
    ImGuiContext *ctx = ImGui::GetCurrentContext();
    if (!ctx) return;
    ImGuiIO &io = ImGui::GetIO();
    if (io.BackendRendererUserData) {
        ImGui_ImplOpenGL3_Shutdown();
    }
}

void gl_imgui_overlay_new_frame(void) {
    ImGui_ImplOpenGL3_NewFrame();
}

void gl_imgui_overlay_render_draw_data(void *draw_data_ptr, int pix_w, int pix_h, uint64_t frame_num) {
    ImDrawData *dd = (ImDrawData *)draw_data_ptr;
    if (!dd || dd->TotalVtxCount <= 0 || pix_w <= 0 || pix_h <= 0) return;
    ensure_gl_procs();

    // Flush any pre-existing guest errors before drawing overlay so attribution is exact.
    drain_gl_errors_into(frame_num, false);

    GLint prev_draw_fbo = 0, prev_read_fbo = 0, prev_draw_buf = 0;
    GLint prev_vp[4] = {0, 0, pix_w, pix_h};
    GLint prev_scissor_box[4] = {0, 0, pix_w, pix_h};
    GLboolean prev_scissor = GL_FALSE;
    GLboolean prev_srgb = GL_FALSE;

    if (s_gl.p_glGetIntegerv) {
        s_gl.p_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw_fbo);
        s_gl.p_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);
        s_gl.p_glGetIntegerv(GL_DRAW_BUFFER, &prev_draw_buf);
        s_gl.p_glGetIntegerv(GL_VIEWPORT, prev_vp);
        s_gl.p_glGetIntegerv(GL_SCISSOR_BOX, prev_scissor_box);
    }
    if (s_gl.p_glIsEnabled) {
        prev_scissor = s_gl.p_glIsEnabled(GL_SCISSOR_TEST);
        prev_srgb = s_gl.p_glIsEnabled(GL_FRAMEBUFFER_SRGB);
    }

    if (s_gl.p_glBindFramebuffer) s_gl.p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (s_gl.p_glDrawBuffer) s_gl.p_glDrawBuffer(GL_BACK);
    if (prev_srgb && s_gl.p_glDisable) s_gl.p_glDisable(GL_FRAMEBUFFER_SRGB);
    if (prev_scissor && s_gl.p_glDisable) s_gl.p_glDisable(GL_SCISSOR_TEST);
    if (s_gl.p_glViewport) s_gl.p_glViewport(0, 0, pix_w, pix_h);

    ImGui_ImplOpenGL3_RenderDrawData(dd);

    s_overlay_frames.fetch_add(1, std::memory_order_relaxed);
    s_overlay_vtx_total.fetch_add((uint64_t)dd->TotalVtxCount, std::memory_order_relaxed);

    if (prev_srgb && s_gl.p_glEnable) s_gl.p_glEnable(GL_FRAMEBUFFER_SRGB);
    if (s_gl.p_glViewport) s_gl.p_glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
    if (s_gl.p_glScissor) s_gl.p_glScissor(prev_scissor_box[0], prev_scissor_box[1], prev_scissor_box[2], prev_scissor_box[3]);
    if (s_gl.p_glEnable && s_gl.p_glDisable) {
        if (prev_scissor) s_gl.p_glEnable(GL_SCISSOR_TEST);
        else s_gl.p_glDisable(GL_SCISSOR_TEST);
    }
    if (s_gl.p_glBindFramebuffer) {
        s_gl.p_glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prev_read_fbo);
        s_gl.p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prev_draw_fbo);
    }
    if (prev_draw_fbo == 0 && s_gl.p_glDrawBuffer && prev_draw_buf != 0) {
        s_gl.p_glDrawBuffer((GLenum)prev_draw_buf);
    }

    drain_gl_errors_into(frame_num, true);
}

bool gl_imgui_fonts_rebuild(void) {
    ImGuiContext *ctx = ImGui::GetCurrentContext();
    if (!ctx) return false;
    ImGuiIO &io = ImGui::GetIO();
    if (!io.BackendRendererUserData) return true;
    ensure_gl_procs();
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    bool ok = ImGui_ImplOpenGL3_CreateFontsTexture();
    drain_gl_errors_into(0, false);
    if (!ok) {
        LOG("[gl_imgui] ERROR: ImGui_ImplOpenGL3_CreateFontsTexture failed\n");
    }
    return ok;
}

void ui_request_window_apply(void) {
    s_window_apply_req.store(true, std::memory_order_release);
}

bool gl_imgui_consume_window_apply(void) {
    return s_window_apply_req.exchange(false, std::memory_order_acq_rel);
}

void gl_imgui_render_in_game_overlay(uint64_t frame_num, int win_w, int win_h, int pix_w, int pix_h, float pixel_density) {
    static int test_checked = -1;
    if (test_checked < 0) {
        const char *env = getenv("YW1_OVERLAY_TEST");
        test_checked = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
    }
    if (test_checked == 1) {
        if (frame_num == 480) {
            g_settings.hud_mode = HUD_DETAILED;
            g_settings.show_fps = true;
            menu_set_open_for_test(true);
            menu_set_tab_for_test(0);
            LOG("[ui-test] frame=%llu YW1_OVERLAY_TEST opened overlay (tab=0 Display) + HUD_DETAILED\n",
                (unsigned long long)frame_num);
        } else if (frame_num == 500) {
            menu_set_tab_for_test(1);
            LOG("[ui-test] frame=%llu YW1_OVERLAY_TEST switched tab=1 (Audio)\n", (unsigned long long)frame_num);
        } else if (frame_num == 515) {
            menu_set_tab_for_test(8);
            LOG("[ui-test] frame=%llu YW1_OVERLAY_TEST switched tab=8 (Controls)\n", (unsigned long long)frame_num);
        } else if (frame_num == 530) {
            menu_set_tab_for_test(9);
            LOG("[ui-test] frame=%llu YW1_OVERLAY_TEST switched tab=9 (System)\n", (unsigned long long)frame_num);
        } else if (frame_num == 540) {
            menu_set_open_for_test(false);
            LOG("[ui-test] frame=%llu YW1_OVERLAY_TEST closed overlay, keeping HUD_DETAILED (frames=%llu glErrors=%llu)\n",
                (unsigned long long)frame_num,
                (unsigned long long)s_overlay_frames.load(std::memory_order_relaxed),
                (unsigned long long)s_overlay_gl_errors.load(std::memory_order_relaxed));
        }
    }

    if (!menu_prepare_frame((uint32_t)(pix_h > 0 ? pix_h : 720), gl_backend_is_loading())) return;
    menu_render_opengl(win_w, win_h, pix_w, pix_h, pixel_density);
}

uint64_t gl_imgui_gl_error_count(void) {
    return s_overlay_gl_errors.load(std::memory_order_relaxed);
}

void gl_imgui_overlay_log_summary(void) {
    LOG("[gl_imgui] SUMMARY overlay_frames=%llu total_vtx=%llu overlay_glErrors=%llu\n",
        (unsigned long long)s_overlay_frames.load(std::memory_order_relaxed),
        (unsigned long long)s_overlay_vtx_total.load(std::memory_order_relaxed),
        (unsigned long long)s_overlay_gl_errors.load(std::memory_order_relaxed));
}
