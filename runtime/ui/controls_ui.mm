// Controls & Key/Gamepad/Mouse/Hotkey remapping UI (M3-A2 multi-language, table-aligned).
// Used both in the pre-launch Settings window (launcher.mm) and the in-game Settings menu (menu.mm).
#include <SDL3/SDL.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"
#include "ui_i18n.h"

extern "C" {
#include "../input_bind.h"
#include "../settings.h"
int input_pad_family(void);
SDL_JoystickID input_active_pad(void);
void input_apply_background(void);
}

static int g_back_frame = -10;
extern "C" bool controls_ui_took_back(void) { return g_back_frame == ImGui::GetFrameCount(); }

static const char *tr_mouse_label(int code) {
    switch (code) {
    case KEY_MOUSE_LEFT: return ui_tr(STR_MOUSE_L);
    case KEY_MOUSE_RIGHT: return ui_tr(STR_MOUSE_R);
    case KEY_MOUSE_MIDDLE: return ui_tr(STR_MOUSE_M);
    case KEY_MOUSE_X1: return ui_tr(STR_MOUSE_X1);
    case KEY_MOUSE_X2: return ui_tr(STR_MOUSE_X2);
    case KEY_WHEEL_UP: return ui_tr(STR_WHEEL_UP);
    case KEY_WHEEL_DOWN: return ui_tr(STR_WHEEL_DOWN);
    default: return nullptr;
    }
}

static const char *display_key_label(int code) {
    if (code <= 0) return ui_tr(STR_UNBOUND);
    if (const char *m = tr_mouse_label(code)) return m;
    return bind_key_label(code);
}

static const char *display_pad_label(int host, int fam) {
    switch (host) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return ui_tr(STR_DPAD_UP);
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return ui_tr(STR_DPAD_DOWN);
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return ui_tr(STR_DPAD_LEFT);
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return ui_tr(STR_DPAD_RIGHT);
    case PADB_LS_UP: return ui_tr(STR_LS_UP);
    case PADB_LS_DOWN: return ui_tr(STR_LS_DOWN);
    case PADB_LS_LEFT: return ui_tr(STR_LS_LEFT);
    case PADB_LS_RIGHT: return ui_tr(STR_LS_RIGHT);
    case PADB_RS_UP: return ui_tr(STR_RS_UP);
    case PADB_RS_DOWN: return ui_tr(STR_RS_DOWN);
    case PADB_RS_LEFT: return ui_tr(STR_RS_LEFT);
    case PADB_RS_RIGHT: return ui_tr(STR_RS_RIGHT);
    default: return bind_pad_label(host, fam);
    }
}

static const char *display_slot_name(int slot) {
    switch (slot) {
    case BIND_A: return ui_tr(STR_SLOT_A);
    case BIND_B: return ui_tr(STR_SLOT_B);
    case BIND_X: return ui_tr(STR_SLOT_X);
    case BIND_Y: return ui_tr(STR_SLOT_Y);
    case BIND_L: return ui_tr(STR_SLOT_L);
    case BIND_R: return ui_tr(STR_SLOT_R);
    case BIND_ZL: return ui_tr(STR_SLOT_ZL);
    case BIND_ZR: return ui_tr(STR_SLOT_ZR);
    case BIND_LSTICK: return ui_tr(STR_SLOT_LSTICK);
    case BIND_RSTICK: return ui_tr(STR_SLOT_RSTICK);
    case BIND_PLUS: return ui_tr(STR_SLOT_PLUS);
    case BIND_MINUS: return ui_tr(STR_SLOT_MINUS);
    case BIND_UP: return ui_tr(STR_DPAD_UP);
    case BIND_DOWN: return ui_tr(STR_DPAD_DOWN);
    case BIND_LEFT: return ui_tr(STR_DPAD_LEFT);
    case BIND_RIGHT: return ui_tr(STR_DPAD_RIGHT);
    case BIND_LS_UP: return ui_tr(STR_LS_UP);
    case BIND_LS_DOWN: return ui_tr(STR_LS_DOWN);
    case BIND_LS_LEFT: return ui_tr(STR_LS_LEFT);
    case BIND_LS_RIGHT: return ui_tr(STR_LS_RIGHT);
    case BIND_RS_UP: return ui_tr(STR_RS_UP);
    case BIND_RS_DOWN: return ui_tr(STR_RS_DOWN);
    case BIND_RS_LEFT: return ui_tr(STR_RS_LEFT);
    case BIND_RS_RIGHT: return ui_tr(STR_RS_RIGHT);
    default: return bind_input_name(slot);
    }
}

static const char *display_hotkey_name(int hk) {
    switch (hk) {
    case HOTKEY_MENU: return ui_tr(STR_HK_MENU);
    case HOTKEY_HUD: return ui_tr(STR_HK_HUD);
    case HOTKEY_SPEED: return ui_tr(STR_HK_SPEED);
    case HOTKEY_FULLSCREEN: return ui_tr(STR_HK_FULLSCREEN);
    default: return bind_hotkey_name(hk);
    }
}

static void watch_results(int fam, char *msg, size_t cap, bool *is_warn) {
    static int seen = -1;
    int rdev, rslot, rother, serial, kind = bind_last_result(&rdev, &rslot, &rother, &serial);
    if (seen < 0) seen = serial;
    if (serial == seen) return;
    seen = serial;
    msg[0] = 0;
    *is_warn = false;
    if (kind == 1) {
        if (rdev == BIND_DEV_HOTKEY)
            snprintf(msg, cap, ui_tr(STR_CAPTURE_SET_HK_FMT), display_hotkey_name(rslot),
                     display_key_label(bind_hotkey_get(rslot)));
        else
            snprintf(msg, cap, ui_tr(STR_CAPTURE_SET_BIND_FMT), display_slot_name(rslot),
                     ui_tr(rdev == BIND_DEV_PAD ? STR_DEV_PAD : STR_DEV_KEY),
                     rdev == BIND_DEV_PAD ? display_pad_label(g_bind_pad[rslot], fam)
                                          : display_key_label(g_bind_key[rslot]));
        settings_save();
    } else if (kind == 2) {
        *is_warn = true;
        if (rdev == BIND_DEV_HOTKEY)
            snprintf(msg, cap, ui_tr(STR_CAPTURE_CONFLICT_HK_FMT), display_hotkey_name(rother),
                     display_key_label(bind_hotkey_get(rother)));
        else
            snprintf(msg, cap, ui_tr(STR_CAPTURE_CONFLICT_BIND_FMT), display_slot_name(rother),
                     rdev == BIND_DEV_PAD ? display_pad_label(g_bind_pad[rother], fam)
                                          : display_key_label(g_bind_key[rother]));
        settings_save();
    } else if (kind == 3) {
        snprintf(msg, cap, "%s", ui_tr(STR_CAPTURE_CANCELLED));
    } else if (kind == 4) {
        snprintf(msg, cap, "%s", ui_tr(STR_CAPTURE_TIMEOUT));
    } else if (kind == 5) {
        if (rdev == BIND_DEV_HOTKEY) snprintf(msg, cap, "%s", ui_tr(STR_RESET_DONE_HK));
        else if (rdev == BIND_DEV_PAD) snprintf(msg, cap, "%s", ui_tr(STR_RESET_DONE_PAD));
        else snprintf(msg, cap, "%s", ui_tr(STR_RESET_DONE_KEY));
        settings_save();
    }
}

static int active_pad_family(std::string &out_status) {
    int np = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&np);
    SDL_JoystickID act = input_active_pad();
    if (!act && np > 0 && ids) act = ids[0];
    int fam = bind_label_family();
    if (np > 0 && ids) {
        const char *nm = SDL_GetGamepadNameForID(act);
        if (act) fam = input_family_of_type(SDL_GetGamepadTypeForID(act));
        const char *pad_name = (nm && *nm) ? nm : ui_tr(STR_PAD_GENERIC);
        char buf[256];
        if (np == 1)
            snprintf(buf, sizeof buf, ui_tr(STR_PAD_CONNECTED_ONE_FMT), pad_name);
        else
            snprintf(buf, sizeof buf, ui_tr(STR_PAD_CONNECTED_MULTI_FMT), pad_name, np);
        out_status = buf;
    } else {
        out_status = ui_tr(STR_PAD_NONE);
    }
    SDL_free(ids);
    return fam;
}

static bool styled_preset_btn(const char *label, bool active) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.36f, 0.44f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.46f, 0.56f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.88f, 0.96f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
    }
    bool hit = ImGui::Button(label, ImVec2(-FLT_MIN, 0));
    if (active) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
    }
    return hit;
}

static bool capture_btn(const char *id, const char *label, bool waiting, int ms_left, bool modified) {
    char txt[128];
    if (waiting) {
        char prefix[64];
        snprintf(prefix, sizeof prefix, ui_tr(STR_PRESS_KEY_FMT), (ms_left + 999) / 1000);
        snprintf(txt, sizeof txt, "%s##%s", prefix, id);
    } else {
        snprintf(txt, sizeof txt, "%s##%s", label, id);
    }

    if (waiting) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.38f, 0.12f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.46f, 0.16f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.96f, 0.78f, 1.0f));
    } else if (modified) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.34f, 0.90f, 0.96f, 1.0f));
    }
    bool hit = ImGui::Button(txt, ImVec2(-FLT_MIN, 0));
    if (waiting) ImGui::PopStyleColor(3);
    else if (modified) ImGui::PopStyleColor();
    return hit;
}

extern "C" void controls_tab_draw(float scale) {
    (void)scale;
    std::string pad_status;
    int fam = active_pad_family(pad_status);
    int cd = -1, cs = -1, ms = 0;
    bind_capture_active(&cd, &cs, &ms);

    static char msg[256];
    static int msg_lang = -1;
    static bool msg_warn = false;
    if (msg_lang != ui_resolve_lang()) { msg[0] = 0; msg_lang = ui_resolve_lang(); }
    watch_results(fam, msg, sizeof msg, &msg_warn);

    // Cancel capture with controller back face button if not handled directly
    if (cd >= 0 && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
        bind_capture_cancel();
        g_back_frame = ImGui::GetFrameCount();
    }

    const ImVec4 gold(0.95f, 0.79f, 0.36f, 1.0f);
    const ImVec4 cyan(0.34f, 0.90f, 0.96f, 1.0f);
    const ImVec4 ok_col(0.45f, 0.92f, 0.62f, 1.0f);
    const ImVec4 muted(0.66f, 0.74f, 0.80f, 1.0f);

    // 1. Top gamepad status banner
    ImGui::BeginChild("##padbar", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
    bool has_pad = SDL_HasGamepad();
    ImGui::TextColored(has_pad ? ok_col : muted, "%s", pad_status.c_str());
    ImGui::PushTextWrapPos(0);
    if (cd >= 0) {
        ImGui::TextColored(gold, ui_tr(STR_CAPTURE_WAIT_FMT), (ms + 999) / 1000);
    } else if (msg[0]) {
        ImGui::TextColored(msg_warn ? ImVec4(1.0f, 0.74f, 0.35f, 1.0f) : cyan, "%s", msg);
    } else {
        ImGui::TextDisabled("%s", ui_tr(STR_CAPTURE_HINT));
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::Spacing();

    // 2. Presets, Stick Deadzones & Rumble (strict BeginTable alignment)
    ImGui::TextColored(gold, "%s", ui_tr(STR_SEC_PAD_PRESET));
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::BeginTable("##pad_presets_tbl", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (styled_preset_btn(ui_tr(STR_PRESET_NINTENDO), g_settings.pad_layout == PAD_LAYOUT_NINTENDO)) {
            bind_capture_cancel();
            bind_preset_pad_nintendo();
        }
        ImGui::TableNextColumn();
        if (styled_preset_btn(ui_tr(STR_PRESET_XBOX), g_settings.pad_layout == PAD_LAYOUT_XBOX)) {
            bind_capture_cancel();
            bind_preset_pad_xbox();
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();

    bool changed = false;
    if (ImGui::BeginTable("##ctrl_params_tbl", 4, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("l1", ImGuiTableColumnFlags_WidthStretch, 0.90f);
        ImGui::TableSetupColumn("v1", ImGuiTableColumnFlags_WidthStretch, 1.00f);
        ImGui::TableSetupColumn("l2", ImGuiTableColumnFlags_WidthStretch, 0.90f);
        ImGui::TableSetupColumn("v2", ImGuiTableColumnFlags_WidthStretch, 1.00f);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(ui_tr(STR_SLD_LS_DZ));
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderInt("##lsdz", &g_settings.ls_deadzone, 0, 40, "%d%%")) changed = true;

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(ui_tr(STR_SLD_RS_DZ));
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderInt("##rsdz", &g_settings.rs_deadzone, 0, 40, "%d%%")) changed = true;

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(ui_tr(STR_SLD_TRIG_THRESH));
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderInt("##trigth", &g_settings.trigger_threshold, 10, 90, "%d%%")) changed = true;

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(ui_tr(STR_SLD_RUMBLE_STR));
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderInt("##rumblestr", &g_settings.rumble_strength, 0, 100, "%d%%")) changed = true;

        ImGui::EndTable();
    }

    if (ImGui::BeginTable("##ctrl_checks_tbl", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::Checkbox(ui_tr(STR_CHK_RUMBLE), &g_settings.rumble)) changed = true;
        ImGui::TableNextColumn();
        if (ImGui::Checkbox(ui_tr(STR_CHK_BG_INPUT), &g_settings.bg_input)) {
            input_apply_background();
            changed = true;
        }
        ImGui::EndTable();
    }
    if (changed) settings_save();
    ImGui::Spacing();

    // 3. Switch Logical Button Mapping Table
    ImGui::TextColored(gold, "%s", ui_tr(STR_SEC_SWITCH_BINDS));
    ImGui::Separator();
    ImGui::Spacing();

    static const int k_slots[] = {
        BIND_A, BIND_B, BIND_X, BIND_Y,
        BIND_L, BIND_R, BIND_ZL, BIND_ZR,
        BIND_LSTICK, BIND_RSTICK, BIND_PLUS, BIND_MINUS,
        BIND_UP, BIND_DOWN, BIND_LEFT, BIND_RIGHT,
        BIND_LS_UP, BIND_LS_DOWN, BIND_LS_LEFT, BIND_LS_RIGHT,
        BIND_RS_UP, BIND_RS_DOWN, BIND_RS_LEFT, BIND_RS_RIGHT,
    };

    if (ImGui::BeginTable("##switch_binds", 3,
                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(ui_tr(STR_COL_SWITCH_BTN), ImGuiTableColumnFlags_WidthStretch, 1.15f);
        ImGui::TableSetupColumn(ui_tr(STR_COL_PAD_BIND), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(ui_tr(STR_COL_KEY_BIND), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();

        for (int slot : k_slots) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(display_slot_name(slot));

            ImGui::TableNextColumn();
            bool wait_pad = (cd == BIND_DEV_PAD && cs == slot);
            bool mod_pad = !bind_is_default_slot(BIND_DEV_PAD, slot);
            char id_pad[32];
            snprintf(id_pad, sizeof id_pad, "p%d", slot);
            if (capture_btn(id_pad, display_pad_label(g_bind_pad[slot], fam), wait_pad, ms, mod_pad)) {
                if (wait_pad) bind_capture_cancel();
                else { msg[0] = 0; bind_capture_begin(BIND_DEV_PAD, slot); }
            }

            ImGui::TableNextColumn();
            bool wait_key = (cd == BIND_DEV_KEY && cs == slot);
            bool mod_key = !bind_is_default_slot(BIND_DEV_KEY, slot);
            char id_key[32];
            snprintf(id_key, sizeof id_key, "k%d", slot);
            if (capture_btn(id_key, display_key_label(g_bind_key[slot]), wait_key, ms, mod_key)) {
                if (wait_key) bind_capture_cancel();
                else { msg[0] = 0; bind_capture_begin(BIND_DEV_KEY, slot); }
            }
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::BeginTable("##reset_binds_tbl", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(bind_is_default(BIND_DEV_PAD));
        if (ImGui::Button(ui_tr(STR_BTN_RESET_PAD))) {
            bind_capture_cancel();
            bind_reset(BIND_DEV_PAD);
        }
        ImGui::EndDisabled();
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(bind_is_default(BIND_DEV_KEY));
        if (ImGui::Button(ui_tr(STR_BTN_RESET_KEY))) {
            bind_capture_cancel();
            bind_reset(BIND_DEV_KEY);
        }
        ImGui::EndDisabled();
        ImGui::EndTable();
    }
    ImGui::Spacing();

    // 4. Global Hotkeys Table
    ImGui::TextColored(gold, "%s", ui_tr(STR_SEC_HOTKEYS));
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::BeginTable("##hotkeys_tbl", 2,
                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(ui_tr(STR_COL_ACTION), ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn(ui_tr(STR_COL_HOTKEY), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();

        static const int k_def_hk[HOTKEY_COUNT] = {
            SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F11
        };
        for (int i = 0; i < HOTKEY_COUNT; i++) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(display_hotkey_name(i));
            ImGui::TableNextColumn();
            bool wait_hk = (cd == BIND_DEV_HOTKEY && cs == i);
            int cur_sc = bind_hotkey_get(i);
            bool mod_hk = (cur_sc != k_def_hk[i]);
            char id_hk[32];
            snprintf(id_hk, sizeof id_hk, "hk%d", i);
            if (capture_btn(id_hk, display_key_label(cur_sc), wait_hk, ms, mod_hk)) {
                if (wait_hk) bind_capture_cancel();
                else { msg[0] = 0; bind_capture_begin(BIND_DEV_HOTKEY, i); }
            }
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(bind_hotkeys_is_default());
    if (ImGui::Button(ui_tr(STR_BTN_RESET_HK))) {
        bind_capture_cancel();
        bind_reset_hotkeys();
    }
    ImGui::EndDisabled();
}
