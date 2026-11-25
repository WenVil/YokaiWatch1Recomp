// Software keyboard (naming Link on a new file, ...). On the Switch nn::swkbd::ShowKeyboard launches the keyboard
// library applet; our am service has no applets, so the game crashed. ShowKeyboard is replaced here: the overlay
// (runtime/ui/menu.mm menu_keyboard_run) asks for the text - typed directly with a keyboard, an on-screen keyboard
// with a controller - while this (game) thread keeps re-presenting the last frame.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../gl/gl_backend.h"
#include "../hle.h"

int menu_keyboard_run(const char *initial, int max_len, char *out, int cap);  // runtime/ui/menu.mm

// nn::swkbd::ShowKeyboard(nn::swkbd::String *out, const nn::swkbd::ShowKeyboardArg &arg) -> nn::Result
// String = {void *ptr; size_t bufSize}
// ShowKeyboardArg (probed 2026-10-08, naming Link): +0x3ac text max length, +0x3b0 min length, +0x3c0 / +0x3c4
// initial text offset / length (UTF-16) in the work buffer at +0x4d0. The game reads the answer as UTF-8.
HLE_FUNC("_ZN2nn5swkbd12ShowKeyboardEPNS0_6StringERKNS0_15ShowKeyboardArgE", swkbd_show) {
    uint64_t out = c->x[0], arg = c->x[1];
    uint64_t ptr = LD64(out), size = LD64(out + 8);
    int max_len = (int)*(const int32_t *)(uintptr_t)(arg + 0x3ac);
    uint32_t off = *(const uint32_t *)(uintptr_t)(arg + 0x3c0), n = *(const uint32_t *)(uintptr_t)(arg + 0x3c4);
    uint64_t wb = LD64(arg + 0x4d0);
    char initial[96] = "";
    for (uint32_t i = 0; wb && i < n && i < sizeof initial - 1; i++) {
        uint16_t ch = *(const uint16_t *)(uintptr_t)(wb + off + 2 * i);
        initial[i] = ch < 0x80 ? (char)ch : '?';
    }
    char text[96];
    int ok = 0;
    if (gl_backend_is_active()) {
        const char *env_name = getenv("LARECOMP_KBD_AUTO");
        const char *chosen = (env_name && *env_name) ? env_name : (initial[0] ? initial : "Keita");
        int lim = (max_len > 0 && max_len < (int)sizeof text) ? max_len : (int)sizeof text - 1;
        snprintf(text, sizeof text, "%.*s", lim, chosen);
        ok = 1;
        LOG("[swkbd] GL fallback auto entry (max %d, initial \"%s\") -> \"%s\"\n", max_len, initial, text);
    } else {
        ok = menu_keyboard_run(initial, max_len, text, sizeof text);
    }
    if (!ok) snprintf(text, sizeof text, "%s", initial);  // cancelled: keep what was there
    LOG("[swkbd] name entry (max %d, initial \"%s\"): %s \"%s\"\n", max_len, initial, ok ? "entered" : "cancelled", text);
    if (ptr && size) {
        memset((void *)(uintptr_t)ptr, 0, size);
        snprintf((char *)(uintptr_t)ptr, size, "%s", text);
    }
    c->x[0] = 0;
}

// nn::ngc::ProfanityFilter (NgWord system data archive 0x0100000000000823 is absent on host).
// Stubbing Initialize/CheckProfanityWords/Finalize prevents nn::fs from aborting on missing NgWord.
HLE_FUNC("_ZN2nn3ngc15ProfanityFilterC1Ev", ngc_ctor1) { (void)c; }
HLE_FUNC("_ZN2nn3ngc15ProfanityFilterC2Ev", ngc_ctor2) { (void)c; }
HLE_FUNC("_ZN2nn3ngc15ProfanityFilterD1Ev", ngc_dtor1) { (void)c; }
HLE_FUNC("_ZN2nn3ngc15ProfanityFilterD2Ev", ngc_dtor2) { (void)c; }
HLE_FUNC("_ZN2nn3ngc15ProfanityFilter10InitializeEPvm", ngc_init) {
    LOG("[ngc] ProfanityFilter::Initialize(size=%llu) -> ok\n", (unsigned long long)c->x[2]);
    c->x[0] = 0;
}
HLE_FUNC("_ZN2nn3ngc15ProfanityFilter10InitializeEPvmb", ngc_init_b) {
    LOG("[ngc] ProfanityFilter::Initialize(size=%llu, check=%u) -> ok\n",
        (unsigned long long)c->x[2], (unsigned)(c->x[3] & 1));
    c->x[0] = 0;
}
HLE_FUNC("_ZN2nn3ngc15ProfanityFilter8FinalizeEv", ngc_finalize) { c->x[0] = 0; }
HLE_FUNC("_ZN2nn3ngc15ProfanityFilter19CheckProfanityWordsEPjjPPKcm", ngc_check_utf8) {
    if (c->x[1]) *(uint32_t *)(uintptr_t)c->x[1] = 0;
    LOG("[ngc] ProfanityFilter::CheckProfanityWords(count=%llu) -> clean\n", (unsigned long long)c->x[4]);
    c->x[0] = 0;
}
HLE_FUNC("_ZN2nn3ngc15ProfanityFilter19CheckProfanityWordsEPjjPPKDsm", ngc_check_utf16) {
    if (c->x[1]) *(uint32_t *)(uintptr_t)c->x[1] = 0;
    c->x[0] = 0;
}
