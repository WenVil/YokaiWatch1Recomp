// Offline-safe Switch network, local wireless, BCAT, Friend, socket, DNS, and SSL IPC stubs.
// Prevents aborts/hangs if game menus probe network, DLC/news delivery, local multiplayer, or friends.
#include <pthread.h>
#include <string.h>

#include "../hle.h"
#include "../ipc.h"

#define NIFM_ERR_OFFLINE MAKE_RESULT(110, 111)
#define NIFM_ERR_NO_NET MAKE_RESULT(110, 300)
#define BCAT_ERR_NO_DATA MAKE_RESULT(122, 4)
#define LDN_ERR_INVALID_STATE MAKE_RESULT(203, 32)
#define SSL_ERR_OFFLINE MAKE_RESULT(123, 1)

static uint32_t ev_handle(bool signaled) {
    uint32_t w, r;
    KEvent *ev = kernel_create_event_pair(&w, &r);
    if (signaled && ev) {
        pthread_mutex_lock(&g_klock);
        kernel_signal(ev);
        pthread_mutex_unlock(&g_klock);
    }
    return r;
}

static void zero_out_buf0(IpcReq *req) {
    IpcBuf *b = out_buf(req, 0);
    if (b && b->addr && b->size) memset((void *)(uintptr_t)b->addr, 0, b->size);
}

// ------------------------------------------------------------------ nifm:u
static void nifm_scan_request(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 0: case 1: break;  // Submit / Cancel
    case 2: OUT(uint32_t, 0) = 1; resp_size(resp, 4); break;  // GetSystemEventReadableHandle -> event or state
    case 3: resp->copy_handles[resp->ncopy++] = ev_handle(true); break;
    default: OUT(uint32_t, 0) = 0; resp_size(resp, 8); break;
    }
}

static void nifm_request(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 0: OUT(uint32_t, 0) = 1; resp_size(resp, 4); break;  // GetRequestState -> 1 (Error / finished)
    case 1: resp->result = NIFM_ERR_OFFLINE; break;           // GetResult -> offline
    case 2:  // GetSystemEventReadableHandles -> two readable event handles
        resp->copy_handles[resp->ncopy++] = ev_handle(true);
        resp->copy_handles[resp->ncopy++] = ev_handle(true);
        break;
    case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 11: case 12:
    case 13: case 14: case 15: case 16: case 21:
        break;
    case 17: memset(resp->data, 0, 0x24); resp_size(resp, 0x24); break;  // GetRequirement
    case 18: OUT(uint32_t, 0) = 0; resp_size(resp, 4); break;            // GetRevision
    case 19:  // GetAppletInfo
        zero_out_buf0(req);
        memset(resp->data, 0, 12);
        resp_size(resp, 12);
        break;
    case 20:  // GetAdditionalInfo
        memset(resp->data, 0, 4);
        resp_size(resp, 4);
        break;
    default:
        LOG("[nifm] IRequest cmd %u (ok)\n", req->cmd);
        resp_size(resp, 0x10);
    }
}

static void nifm_general(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 1: OUT(uint32_t, 0) = 1; resp_size(resp, 4); break;  // GetClientId
    case 2: ipc_return_object(resp, service_new("nifm:IScanRequest", nifm_scan_request, NULL)); break;
    case 4: ipc_return_object(resp, service_new("nifm:IRequest", nifm_request, NULL)); break;
    case 5: case 9: case 13: case 14: case 15: case 29:  // profile / IP / AP queries
        resp->result = NIFM_ERR_NO_NET;
        break;
    case 6: case 7: case 8: case 28: case 33:  // enumerations / scan data -> 0 items
        zero_out_buf0(req);
        OUT(int32_t, 0) = 0;
        resp_size(resp, 4);
        break;
    case 17: OUT(uint8_t, 0) = 1; resp_size(resp, 1); break;  // IsWirelessCommunicationEnabled
    case 18: memset(resp->data, 0, 3); resp_size(resp, 3); break;  // GetInternetConnectionStatus (disconnected)
    case 20: case 21: case 22: OUT(uint8_t, 0) = 0; resp_size(resp, 1); break;  // IsEthernet / IsAny*RequestAccepted
    case 31: resp->copy_handles[resp->ncopy++] = ev_handle(false); break;  // GetTelemetorySystemEventReadableHandle
    case 32: memset(resp->data, 0, 0x20); resp_size(resp, 0x20); break;
    default:
        OUT(uint64_t, 0) = 0;
        resp_size(resp, 8);
    }
}

SERVICE("nifm:u", nifm_u) {
    (void)s;
    if (req->cmd == 4 || req->cmd == 5) {  // CreateGeneralServiceOld / CreateGeneralService
        ipc_return_object(resp, service_new("nifm:IGeneralService", nifm_general, NULL));
        return;
    }
    LOG("[nifm] root cmd %u (ok)\n", req->cmd);
}

// ------------------------------------------------------------------ bcat:u
static void bcat_progress(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 0: resp->copy_handles[resp->ncopy++] = ev_handle(true); break;  // GetEvent
    case 1:  // GetImpl -> state=9 (Done), result=BCAT_ERR_NO_DATA
        memset(resp->data, 0, 0x30);
        OUT(int32_t, 0) = 9;
        OUT(uint32_t, 4) = BCAT_ERR_NO_DATA;
        resp_size(resp, 0x30);
        break;
    default: resp_size(resp, 0x10);
    }
}

static void bcat_file(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 0: resp->result = BCAT_ERR_NO_DATA; break;  // Open
    default: OUT(int64_t, 0) = 0; resp_size(resp, 16); break;
    }
}

static void bcat_dir(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 0: resp->result = BCAT_ERR_NO_DATA; break;  // Open
    default: OUT(int32_t, 0) = 0; resp_size(resp, 4); break;  // Read / GetCount -> 0
    }
}

static void bcat_storage(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 0: ipc_return_object(resp, service_new("bcat:IDeliveryCacheFileService", bcat_file, NULL)); break;
    case 1: ipc_return_object(resp, service_new("bcat:IDeliveryCacheDirectoryService", bcat_dir, NULL)); break;
    case 10: zero_out_buf0(req); OUT(int32_t, 0) = 0; resp_size(resp, 4); break;  // EnumerateDeliveryCacheDirectory
    default: LOG("[bcat] storage cmd %u (ok)\n", req->cmd); resp_size(resp, 8);
    }
}

static void bcat_service(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 10100: case 10101: case 20100: case 20101:  // RequestSyncDeliveryCache*
        ipc_return_object(resp, service_new("bcat:IDeliveryCacheProgressService", bcat_progress, NULL));
        break;
    case 10102: case 10200: case 20200: case 30100: case 30101: case 30102: case 30200: case 30300:
    case 90100: case 90200:
        zero_out_buf0(req);
        OUT(uint64_t, 0) = 0;
        resp_size(resp, 8);
        break;
    default:
        LOG("[bcat] service cmd %u (ok)\n", req->cmd);
        resp_size(resp, 8);
    }
}

SERVICE("bcat:u", bcat_u) {
    (void)s;
    switch (req->cmd) {
    case 0: ipc_return_object(resp, service_new("bcat:IBcatService", bcat_service, NULL)); break;
    case 1: case 2: ipc_return_object(resp, service_new("bcat:IDeliveryCacheStorageService", bcat_storage, NULL)); break;
    default: LOG("[bcat] creator cmd %u (ok)\n", req->cmd);
    }
}

// ------------------------------------------------------------------ ldn:u
static uint32_t g_ldn_state;

RT_ON_RESET(net_misc_reset) { g_ldn_state = 0; }

static void ldn_user_service(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 0: OUT(uint32_t, 0) = g_ldn_state; resp_size(resp, 4); break;  // GetState (0=None, 1=Initialized)
    case 1: case 4: case 5: case 101: resp->result = LDN_ERR_INVALID_STATE; break;
    case 2: OUT(uint32_t, 0) = 0; OUT(uint32_t, 4) = 0; resp_size(resp, 8); break;  // GetIpv4Address
    case 3: OUT(uint16_t, 0) = 0; resp_size(resp, 2); break;                        // GetDisconnectReason
    case 100: resp->copy_handles[resp->ncopy++] = ev_handle(false); break;          // AttachStateChangeEvent
    case 102: case 103: zero_out_buf0(req); OUT(int32_t, 0) = 0; resp_size(resp, 4); break;  // Scan* -> 0 networks
    case 200: case 202: case 300: case 302: resp->result = LDN_ERR_INVALID_STATE; break;     // CreateNetwork/Connect
    case 201: case 203: case 301: case 303: if (g_ldn_state) g_ldn_state = 1; break;         // Close*/Destroy/Disconnect
    case 400: g_ldn_state = 1; break;  // Initialize
    case 401: g_ldn_state = 0; break;  // Finalize
    default: OUT(uint64_t, 0) = 0; resp_size(resp, 8);
    }
}

SERVICE("ldn:u", ldn_u) {
    (void)s;
    if (req->cmd == 0) ipc_return_object(resp, service_new("ldn:IUserLocalCommunicationService", ldn_user_service, NULL));
    else LOG("[ldn] root cmd %u (ok)\n", req->cmd);
}

// ------------------------------------------------------------------ friend:u
static void friend_service(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 0: resp->copy_handles[resp->ncopy++] = ev_handle(true); break;  // GetCompletionEvent
    case 1: break;                                                       // Cancel
    default:
        zero_out_buf0(req);
        OUT(uint64_t, 0) = 0;
        resp_size(resp, 8);
        break;
    }
}

SERVICE("friend:u", friend_u) {
    (void)s;
    if (req->cmd == 0) ipc_return_object(resp, service_new("friend:IFriendService", friend_service, NULL));
    else LOG("[friend] root cmd %u (ok)\n", req->cmd);
}

// ------------------------------------------------------------------ bsd:u / bsd:s / sfdnsres / ssl
static void bsd_handle(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 0: OUT(uint64_t, 0) = 0; resp_size(resp, 8); break;  // RegisterClient
    case 1: case 27: break;                                   // StartMonitoring / Initialize
    case 26: OUT(int32_t, 0) = 0; OUT(int32_t, 4) = 0; resp_size(resp, 8); break;  // Close
    default:
        OUT(int32_t, 0) = -1;
        OUT(int32_t, 4) = 50;  // ENETDOWN / EHOSTUNREACH
        resp_size(resp, 8);
        break;
    }
}

SERVICE("bsd:u", bsd_u) { bsd_handle(s, req, resp); }
SERVICE("bsd:s", bsd_s) { bsd_handle(s, req, resp); }

SERVICE("sfdnsres", sfdnsres) {
    (void)s;
    zero_out_buf0(req);
    OUT(int32_t, 0) = 0;
    OUT(int32_t, 4) = 2;   // TRY_AGAIN / EAI_AGAIN
    OUT(int32_t, 8) = -3;
    resp_size(resp, 16);
}

static void ssl_connection(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    if (req->cmd == 2) resp->result = SSL_ERR_OFFLINE;  // DoHandshake
    else { OUT(uint64_t, 0) = 0; resp_size(resp, 8); }
}

static void ssl_context(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    if (req->cmd == 2) ipc_return_object(resp, service_new("ssl:ISslConnection", ssl_connection, NULL));
    else { OUT(uint64_t, 0) = 0; resp_size(resp, 8); }
}

SERVICE("ssl", ssl_service) {
    (void)s;
    switch (req->cmd) {
    case 0: ipc_return_object(resp, service_new("ssl:ISslContext", ssl_context, NULL)); break;
    case 1: case 2: case 3: zero_out_buf0(req); OUT(uint32_t, 0) = 0; resp_size(resp, 4); break;
    default: OUT(uint64_t, 0) = 0; resp_size(resp, 8); break;
    }
}
