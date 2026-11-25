// hid (shared memory, input state filled in later) and settings (set, set:sys).
#include <pthread.h>
#include <string.h>

#include "../hle.h"
#include "../ipc.h"
#include "../settings.h"
#include "../rumble.h"

static uint32_t new_event_handle(void) {
    uint32_t w, r;
    kernel_create_event_pair(&w, &r);
    return r;
}

// ------------------------------------------------------------------ hid
KSharedMem *g_hid_shmem;
static uint32_t hid_shmem_handle;
static pthread_mutex_t hid_touch_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t touch_sampling;
static uint64_t touch_slot_tail;
static bool touch_was_down;
static int32_t touch_last_x, touch_last_y;

// RingLifo<TouchScreenState<16>, 16> at g_hid_shmem + 0x400 (verified against YW1 sdk 0x34ab38 / 0x34b370).
// Header: +0x00 timestamp, +0x08 buffer_count(17), +0x10 tail, +0x18 count.
// Slot i at +0x20 + i*0x298: +0x00 seqlock, +0x08 sampling, +0x10 count, +0x18 touches[0..15] (stride 0x28).
static void push_touch_sample_locked(uint8_t *base, int32_t count, uint32_t attrs, int32_t x, int32_t y) {
    uint64_t next_tail = (touch_sampling == 0) ? 0 : ((touch_slot_tail + 1) % 17);
    uint64_t samp = ++touch_sampling;
    uint8_t *entry = base + 0x20 + next_tail * 0x298;
    memset(entry, 0, 0x298);
    __atomic_store_n((uint64_t *)(entry + 0x00), samp * 2 - 1, __ATOMIC_RELEASE);
    *(int64_t *)(entry + 0x08) = (int64_t)samp;
    *(int32_t *)(entry + 0x10) = count;
    if (count > 0) {
        uint8_t *t = entry + 0x18;
        *(uint64_t *)(t + 0x00) = 0;      // delta_time
        *(uint32_t *)(t + 0x08) = attrs;  // Switch attributes: bit0=Start, bit1=End
        *(uint32_t *)(t + 0x0C) = 0;      // finger_id
        *(int32_t *)(t + 0x10) = x;       // 0..1279
        *(int32_t *)(t + 0x14) = y;       // 0..719
        *(int32_t *)(t + 0x18) = 10;      // diameter_x
        *(int32_t *)(t + 0x1C) = 10;      // diameter_y
        *(int32_t *)(t + 0x20) = 0;       // rotation_angle
    }
    __atomic_store_n((uint64_t *)(entry + 0x00), samp * 2, __ATOMIC_RELEASE);
    __atomic_store_n((uint64_t *)(base + 0x10), next_tail, __ATOMIC_RELEASE);
    uint64_t valid = samp < 17 ? samp : 17;
    __atomic_store_n((uint64_t *)(base + 0x18), valid, __ATOMIC_RELEASE);
    touch_slot_tail = next_tail;
}

static void hid_shmem_map(KSharedMem *s) {
    pthread_mutex_lock(&hid_touch_lock);
    uint8_t *base = (uint8_t *)(uintptr_t)(s->guest_addr + 0x400);
    *(uint64_t *)(base + 0x00) = 0;
    *(uint64_t *)(base + 0x08) = 17;
    *(uint64_t *)(base + 0x10) = 0;
    *(uint64_t *)(base + 0x18) = 0;
    touch_sampling = 0;
    touch_slot_tail = 0;
    touch_was_down = false;
    push_touch_sample_locked(base, 0, 0, 0, 0);
    pthread_mutex_unlock(&hid_touch_lock);
    LOG("[hid] shared memory mapped at %llx (TouchScreen RingLifo @ +0x400)\n",
        (unsigned long long)s->guest_addr);
}

void hid_touch_write(bool down, int x, int y) {
    if (x < 0) x = 0; else if (x > 1279) x = 1279;
    if (y < 0) y = 0; else if (y > 719) y = 719;
    pthread_mutex_lock(&hid_touch_lock);
    if (g_hid_shmem && g_hid_shmem->guest_addr) {
        uint8_t *base = (uint8_t *)(uintptr_t)(g_hid_shmem->guest_addr + 0x400);
        if (down) {
            uint32_t attrs = touch_was_down ? 0u : 1u;
            push_touch_sample_locked(base, 1, attrs, x, y);
            if (!touch_was_down) LOG("[hid] touch down (%d, %d)\n", x, y);
            touch_was_down = true;
            touch_last_x = x;
            touch_last_y = y;
        } else if (touch_was_down) {
            push_touch_sample_locked(base, 1, 2u, touch_last_x, touch_last_y);
            push_touch_sample_locked(base, 0, 0, 0, 0);
            LOG("[hid] touch up (%d, %d)\n", touch_last_x, touch_last_y);
            touch_was_down = false;
        }
    }
    pthread_mutex_unlock(&hid_touch_lock);
}

RT_ON_RESET(hid_reset) {
    pthread_mutex_lock(&hid_touch_lock);
    g_hid_shmem = NULL;
    hid_shmem_handle = 0;
    touch_sampling = 0;
    touch_slot_tail = 0;
    touch_was_down = false;
    pthread_mutex_unlock(&hid_touch_lock);
}

static void applet_resource(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    if (req->cmd == 0) {  // GetSharedMemoryHandle
        if (!g_hid_shmem) g_hid_shmem = kernel_create_shared_memory(0x40000, hid_shmem_map, &hid_shmem_handle);
        resp->copy_handles[resp->ncopy++] = hid_shmem_handle;
    }
}

SERVICE("hid", hid) {
    (void)s;
    switch (req->cmd) {
    case 0: ipc_return_object(resp, service_new("IAppletResource", applet_resource, NULL)); break;
    case 40: case 106: resp->copy_handles[resp->ncopy++] = new_event_handle(); break;
    case 101: OUT(uint32_t, 0) = 0x1F; resp_size(resp, 4); break;  // GetSupportedNpadStyleSet
    case 121: OUT(int64_t, 0) = 0; resp_size(resp, 8); break;      // GetNpadJoyHoldType
    case 129: OUT(int64_t, 0) = 0; resp_size(resp, 8); break;      // GetNpadHandheldActivationMode
    case 201: {  // SendVibrationValue: handle, four floats, padding, ARUID
        if (req->data_size < 32) { resp->result = 0xD401; break; }
        uint32_t h; RumbleValue v;
        memcpy(&h, req->data, sizeof h);
        memcpy(&v, req->data + 4, sizeof v);
        input_rumble_send(h, &v);
        break;
    }
    case 202: {  // GetActualVibrationValue (best-effort requested state)
        if (req->data_size < 16) { resp->result = 0xD401; break; }
        RumbleValue v = input_rumble_get(IN(uint32_t, 0));
        memcpy(resp->data, &v, sizeof v); resp_size(resp, sizeof v);
        break;
    }
    case 204:
        if (req->data_size < 1) { resp->result = 0xD401; break; }
        input_rumble_permit((IN(uint8_t, 0) & 1) != 0); break;
    case 205: OUT(uint8_t, 0) = input_rumble_permitted(); resp_size(resp, 1); break;
    case 211:  // virtual player-1 vibration device is mounted, as advertised by HLE
        OUT(uint8_t, 0) = req->data_size >= 4 && rumble_side(IN(uint32_t, 0)) >= 0;
        resp_size(resp, 1); break;
    case 206: {  // SendVibrationValues: parallel handle/value pointer buffers
        IpcBuf *handles = in_buf(req, 0), *values = in_buf(req, 1);
        if (!handles || !values || handles->size % 4 || values->size % sizeof(RumbleValue) ||
            handles->size / 4 != values->size / sizeof(RumbleValue) ||
            handles->size / 4 > 64 || (handles->size && (!handles->addr || !values->addr))) {
            resp->result = 0xD401; break;
        }
        // Validate both lengths before sending anything. memcpy avoids assuming
        // that guest pointer buffers are aligned like host floats/uint32_t.
        for (uint64_t i = 0; i < handles->size / 4; i++) {
            uint32_t h; RumbleValue v;
            memcpy(&h, (const void *)(uintptr_t)(handles->addr + i * 4), sizeof h);
            memcpy(&v, (const void *)(uintptr_t)(values->addr + i * sizeof v), sizeof v);
            input_rumble_send(h, &v);
        }
        break;
    }
    case 203: case 207: case 208: case 209: case 210: break;  // activation HLE'd; GC ERM / permit sessions not implemented
    case 200: memset(resp->data, 0, 0x10); OUT(uint32_t, 0) = 1; resp_size(resp, 0x10); break;
    default: break;  // Activate*/Set*: accept
    }
}

SERVICE("hid:sys", hid_sys) { (void)s; (void)req; resp_size(resp, 0x10); }

// ------------------------------------------------------------------ settings
static const char *const languages[] = {"ja", "en-US", "fr", "de", "it", "es", "zh-CN", "ko", "nl",
                                         "pt", "ru", "zh-TW", "en-GB", "fr-CA", "es-419", "zh-Hans",
                                         "zh-Hant", "pt-BR"};
#define NLANG 18

static uint64_t lang_code(const char *s) {
    uint64_t v = 0;
    memcpy(&v, s, strlen(s));
    return v;
}

static void language_list(IpcReq *req, IpcResp *resp, int max) {
    IpcBuf *b = out_buf(req, 0);
    int n = 0;
    if (b) {
        uint64_t *out = (uint64_t *)(uintptr_t)b->addr;
        for (; n < max && n < NLANG && (uint64_t)(n + 1) * 8 <= b->size; n++) out[n] = lang_code(languages[n]);
    }
    OUT(int32_t, 0) = n;
    resp_size(resp, 4);
}

SERVICE("set", set) {
    (void)s;
    switch (req->cmd) {
    case 0: {  // GetLanguageCode
        const char *lc = settings_language();
        static char last_logged[16];
        if (strcmp(last_logged, lc) != 0) {
            snprintf(last_logged, sizeof last_logged, "%s", lc);
            LOG("[set] GetLanguageCode -> %s\n", lc);
        }
        OUT(uint64_t, 0) = lang_code(lc);
        resp_size(resp, 8);
        break;
    }
    case 1:  // GetAvailableLanguageCodes
    case 5: {  // GetAvailableLanguageCodes2
        IpcBuf *b = out_buf(req, 0);
        language_list(req, resp, NLANG);
        static bool logged_avail;
        if (!logged_avail) {
            logged_avail = true;
            LOG("[set] GetAvailableLanguageCodes(cmd=%u, buf=%llu) -> %d codes\n", req->cmd,
                (unsigned long long)(b ? b->size : 0), OUT(int32_t, 0));
        }
        break;
    }
    case 2: {                                                                      // MakeLanguageCode
        uint32_t i = IN(uint32_t, 0);
        OUT(uint64_t, 0) = i < NLANG ? lang_code(languages[i]) : 0;
        resp_size(resp, 8);
        break;
    }
    case 3: OUT(int32_t, 0) = NLANG; resp_size(resp, 4); break;
    case 4: OUT(uint32_t, 0) = 1; resp_size(resp, 4); break;                       // GetRegionCode: USA
    case 6: OUT(int32_t, 0) = NLANG; resp_size(resp, 4); break;
    case 8: OUT(uint8_t, 0) = 0; resp_size(resp, 1); break;                        // GetQuestFlag
    default: LOG("[set] cmd %u (ok)\n", req->cmd); resp_size(resp, 0x10);
    }
}

SERVICE("set:sys", set_sys) {
    (void)s;
    switch (req->cmd) {
    case 3: case 4: {  // GetFirmwareVersion(2) -> B buffer 0x100
        IpcBuf *b = out_buf(req, 0);
        if (b) {
            uint8_t *v = (uint8_t *)(uintptr_t)b->addr;
            memset(v, 0, 0x100);
            v[0] = 8; v[1] = 1; v[2] = 0; v[4] = 8; v[5] = 1; v[6] = 0;  // 8.1.0
            strcpy((char *)v + 0x68, "8.1.0");
            strcpy((char *)v + 0x80, "8.1.0-1.0");
        }
        break;
    }
    case 23: OUT(uint32_t, 0) = 1; resp_size(resp, 4); break;  // GetColorSetId: dark
    default: LOG("[set:sys] cmd %u (ok)\n", req->cmd); resp_size(resp, 0x10);
    }
}
