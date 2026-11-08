#include "common.h"

#include <atomic>
#include <cstdlib>
#include "runtime/sysutil_events.h"

#include <cstring>

namespace hle {

namespace {

constexpr int32_t SCE_NP_MANAGER_STATUS_OFFLINE = -1;
constexpr int32_t SCE_NP_MANAGER_STATUS_ONLINE = 3;
constexpr int32_t CELL_NET_CTL_STATE_Disconnected = 0;
constexpr int32_t CELL_NET_CTL_STATE_IPObtained = 4;
constexpr int64_t SCE_NP_ERROR_NOT_INITIALIZED = static_cast<int32_t>(0x8002aa01);
constexpr int64_t SCE_NP_ERROR_ALREADY_INITIALIZED = static_cast<int32_t>(0x8002aa02);
constexpr int64_t SCE_NP_ERROR_INVALID_ARGUMENT = static_cast<int32_t>(0x8002aa03);
constexpr int64_t SCE_NP_ERROR_OFFLINE = static_cast<int32_t>(0x8002aa0c);
constexpr int64_t CELL_NET_CTL_ERROR_NOT_CONNECTED = static_cast<int32_t>(0x80130106);
constexpr int64_t CELL_NET_CTL_ERROR_NOT_INITIALIZED = static_cast<int32_t>(0x80130101);
constexpr int64_t CELL_NET_CTL_ERROR_INVALID_ADDR = static_cast<int32_t>(0x80130103);

}

/* The socket layer. RPCS3 answers these with 0 (success) rather than an error,
 * so a title that gates its initialisation on them proceeds. Enumerating saves
 * and starting the world both happen after network setup, so an error here
 * would silently skip code paths that have nothing to do with the network.
 * Reference: refs/rpcs3/rpcs3/Emu/Cell/Modules/sys_net_.cpp.
 * PS3BASE_NET_LOG reports every call and its return. */
#define NET_ENTER(name, fmt, ...)                                              \
    do {                                                                       \
        static const bool on = std::getenv("PS3BASE_NET_LOG") != nullptr;      \
        if (on) LOG_INFO("sys_net", name " " fmt, __VA_ARGS__);                \
    } while (0)

HLE_FUNC(sys_net, sys_net_initialize_network_ex) {
    NET_ENTER("initialize_network_ex", "pool 0x%x size %u", a32(ctx, 0), a32(ctx, 1));
    ret(ctx, 0);
}

HLE_FUNC(sys_net, sys_net_socket) {
    const int32_t family = static_cast<int32_t>(a32(ctx, 0));
    const int32_t type = static_cast<int32_t>(a32(ctx, 1));
    const int32_t proto = static_cast<int32_t>(a32(ctx, 2));
    NET_ENTER("socket", "family %d type %d proto %d -> fd 3", family, type, proto);
    /* A concrete descriptor, not 0: 0 is stdin on this ABI and a title that
     * checks for >= 0 then binds would bind the wrong thing. */
    ret(ctx, 3);
}

HLE_FUNC(sys_net, sys_net_bind) {
    NET_ENTER("bind", "s %d addr 0x%x len %u -> 0", a32(ctx, 0), a32(ctx, 1), a32(ctx, 2));
    ret(ctx, 0);
}

HLE_FUNC(sys_net, sys_net_setsockopt) {
    NET_ENTER("setsockopt", "s %d level %d opt %d -> 0",
              a32(ctx, 0), a32(ctx, 1), a32(ctx, 2));
    ret(ctx, 0);
}

HLE_FUNC(sys_net, sys_net_socketclose) {
    NET_ENTER("socketclose", "s %d -> 0", a32(ctx, 0));
    ret(ctx, 0);
}
HLE_FUNC(sys_net, sys_net_finalize_network) { ret(ctx, 0); }
/* Events the network dialogs raise; the title polls for them through
 * cellSysutilCheckCallback. */
static constexpr int32_t CELL_NET_CTL_ERROR_DIALOG_CANCELED = static_cast<int32_t>(0x80130107);

static constexpr uint64_t CELL_SYSUTIL_NET_CTL_NETSTART_LOADED   = 0x0801;
static constexpr uint64_t CELL_SYSUTIL_NET_CTL_NETSTART_FINISHED = 0x0802;
static constexpr uint64_t CELL_SYSUTIL_NET_CTL_NETSTART_UNLOADED = 0x0803;

/* Raise a system command to every registered sysutil callback. */
static void sysutil_send(uint64_t event) {
    for (int slot = 0; slot < 4; slot++) {
        const uint32_t fn = callback_function(slot);
        if (!fn) continue;
        const uint32_t arg = callback_arg(slot);
        sysutil_post([fn, arg, event](ppu::Context& c) {
            call_guest(c, fn, arg, event, 0);
        });
    }
}

/* PS3BASE_NP_LOG: report the NP entry points and their arguments. */
#define NP_ENTER(name)                                                        \
    do {                                                                      \
        static const bool on = std::getenv("PS3BASE_NP_LOG") != nullptr;      \
        if (on) LOG_INFO("sceNp", name " r3=0x%x r4=0x%x", a32(ctx, 0), a32(ctx, 1)); \
    } while (0)

namespace npdetail {

constexpr int64_t kNotInit = static_cast<int64_t>(static_cast<int32_t>(0x8002aa01));
constexpr int64_t kAlreadyInit = static_cast<int64_t>(static_cast<int32_t>(0x8002aa02));
constexpr int64_t kInvalidArg = static_cast<int64_t>(static_cast<int32_t>(0x8002aa03));
/* sceNp2.cpp: SCE_NP_MATCHING2_ERROR_* */
constexpr int64_t kMatch2NotInit = static_cast<int64_t>(static_cast<int32_t>(0x80022303));
constexpr int64_t kMatch2InvalidArg = static_cast<int64_t>(static_cast<int32_t>(0x8002230a));
constexpr int64_t kMatch2NotSignedIn = static_cast<int64_t>(static_cast<int32_t>(0x80022334));
constexpr int64_t kMatch2CtxNotFound = static_cast<int64_t>(static_cast<int32_t>(0x80022306));
constexpr int64_t kMatch2CtxStarted = static_cast<int64_t>(static_cast<int32_t>(0x80022307));
/* sceNpBasic: SCE_NP_BASIC_ERROR_NO_EVENT */
constexpr int32_t kBasicNoEvent = static_cast<int32_t>(0x8002a66b);

struct NpState {
    bool init = false;
    bool np2_init = false;
    bool match2_init = false;
    bool netctl_init = false;
    uint32_t manager_cb = 0;
    uint32_t manager_cb_arg = 0;
    uint32_t basic_cb = 0;
    uint32_t basic_cb_arg = 0;
    uint32_t netctl_handler = 0;
    uint32_t netctl_arg = 0;
    bool ctx_created = false;
    bool ctx_started = false;
    uint32_t ctx_id = 0;
    uint32_t ctx_cb = 0;
    uint32_t ctx_cb_arg = 0;
};

NpState g_np;

/* RPCS3 np_handler::get_psn_status in its two offline-capable modes. The
 * simulated mode is a sign-in the SDK itself can produce; it is not a lie
 * about an error path, which is what the forcing is. */
int32_t np_psn_status() {
    static const bool offline = std::getenv("PS3BASE_NP_OFFLINE") != nullptr;
    return offline ? SCE_NP_MANAGER_STATUS_OFFLINE : SCE_NP_MANAGER_STATUS_ONLINE;
}

}  // namespace npdetail

using namespace npdetail;

/* cellNetCtl. RPCS3 tracks an init flag, hands out a handler id, and reports
 * the connection state the emulated console is in. The dialog and its events
 * stay exactly as they were: they are what makes the title offer
 * "Continue playing offline". */
HLE_FUNC(cellNetCtl, cellNetCtlInit) { g_np.netctl_init = true; ret(ctx, 0); }
HLE_FUNC(cellNetCtl, cellNetCtlTerm) { g_np.netctl_init = false; ret(ctx, 0); }
/* cellNetCtlAddHandler(handler, arg, s32* hid). The handler is
 * void(event, errorCode, arg); it is never called while the state is
 * Disconnected, which is the truthful offline behaviour. */
HLE_FUNC(cellNetCtl, cellNetCtlAddHandler) {
    if (!g_np.netctl_init) return ret(ctx, CELL_NET_CTL_ERROR_NOT_INITIALIZED);
    const uint32_t hid = a32(ctx, 2);
    if (!hid) return ret(ctx, CELL_NET_CTL_ERROR_INVALID_ADDR);
    g_np.netctl_handler = a32(ctx, 0);
    g_np.netctl_arg = a32(ctx, 1);
    vm::write32(hid, 1);
    LOG_INFO("cellNetCtl", "AddHandler handler=0x%08x arg=0x%08x -> hid 1", a32(ctx, 0), a32(ctx, 1));
    ret(ctx, 0);
}
HLE_FUNC(cellNetCtl, cellNetCtlDelHandler) {
    LOG_INFO("cellNetCtl", "DelHandler(%d)", s32(ctx, 0));
    g_np.netctl_handler = 0;
    ret(ctx, 0);
}
HLE_FUNC(cellNetCtl, cellNetCtlGetState) {
    /* Report the caller a handful of times; 17k identical lines tell nothing. */
    static std::atomic<int> seen{0};
    const int n = seen.fetch_add(1);
    if (n < 6 || (n % 20000) == 0)
        LOG_INFO("cellNetCtl", "GetState called #%d lr=0x%08x thread=0x%x", n,
                 static_cast<uint32_t>(ctx.cia), ctx.thread_id);
    const int32_t state = std::getenv("PS3BASE_NET_CONNECTED") ? CELL_NET_CTL_STATE_IPObtained
                                                                : CELL_NET_CTL_STATE_Disconnected;
    if (uint32_t p = a32(ctx, 0)) vm::write32(p, state);
    ret(ctx, 0);
}
HLE_FUNC(cellNetCtl, cellNetCtlGetInfo) { ret(ctx, CELL_NET_CTL_ERROR_NOT_CONNECTED); }

/* The "connect to PSN" dialog the title raises before starting a world. There is
 * no network, so it is driven to its offline conclusion: LOADED then FINISHED
 * from the load call, "cancelled" plus UNLOADED from the unload call. The title
 * then offers "Continue playing offline".
 * Reference: refs/rpcs3/rpcs3/Emu/Cell/Modules/cellNetCtl.cpp, netstart_hack and
 * cellNetCtlNetStartDialogUnloadAsync. */
HLE_FUNC(cellNetCtl, cellNetCtlNetStartDialogLoadAsync) {
    const uint32_t param = a32(ctx, 0);
    if (param) {
        LOG_INFO("cellNetCtl", "NetStartDialogLoadAsync size %d type %d flag %d",
                 static_cast<int32_t>(vm::read32(param)),
                 static_cast<int32_t>(vm::read32(param + 4)),
                 static_cast<int32_t>(vm::read32(param + 8)));
    }
    sysutil_send(CELL_SYSUTIL_NET_CTL_NETSTART_LOADED);
    sysutil_send(CELL_SYSUTIL_NET_CTL_NETSTART_FINISHED);
    LOG_INFO("cellNetCtl", "NetStartDialogLoadAsync -> LOADED + FINISHED (offline)");
    ret(ctx, 0);
}

HLE_FUNC(cellNetCtl, cellNetCtlNetStartDialogAbortAsync) { ret(ctx, 0); }

HLE_FUNC(cellNetCtl, cellNetCtlNetStartDialogUnloadAsync) {
    LOG_INFO("cellNetCtl", "NetStartDialogUnloadAsync called result=0x%08x", a32(ctx, 0));
    const uint32_t result = a32(ctx, 0);
    /* CellNetCtlNetStartDialogResult: s32 size, s32 result. Offline means the
     * dialog was cancelled, which is the title's "play offline" path. */
    if (result) {
        vm::write32(result + 0, 8);
        /* PS3BASE_NET_CONNECTED reports the dialog as succeeded so the title
         * takes its online path instead of "continue offline"; every other
         * handler already agrees with that state. */
        const int64_t code = std::getenv("PS3BASE_NET_CONNECTED")
                                 ? 0
                                 : static_cast<uint32_t>(CELL_NET_CTL_ERROR_DIALOG_CANCELED);
        vm::write32(result + 4, static_cast<uint32_t>(code));
    }
    sysutil_send(CELL_SYSUTIL_NET_CTL_NETSTART_UNLOADED);
    LOG_INFO("cellNetCtl", "NetStartDialogUnloadAsync -> CANCELED + UNLOADED");
    ret(ctx, 0);
}
HLE_FUNC(cellNetCtl, cellNetCtlGetNatInfo) { ret(ctx, CELL_NET_CTL_ERROR_NOT_CONNECTED); }

/* ------------------------------------------------------------------------
 * NP, the honest way.
 *
 * The generic unimplemented handler returned 0 for sceNpMatching2Init2
 * (import 308) and the title then believed the matching utility was up. That
 * is exactly what forcing #1 substitutes for, and the forcing is a debt
 * (docs/forcings.md).
 *
 * How NP decides the world server thread, read from the lifted code
 * out/ppu/mc_020.cpp f_00216260:
 *
 *   if ([obj+1066] != 0) return 1;                  // already set up
 *   if ([obj+96]   == 0) r3 = sceNpMatching2Init2;  // import 308
 *                        if (r3 < 0) return 0;      // init failed
 *   r3 = sceNpManagerGetNpId(&npid);                // import 280
 *   if (r3 < 0) return 0;                           // NOT SIGNED IN
 *   r3 = sceNpMatching2CreateContext(...);          // import 298
 *   if (r3 < 0) return 0;
 *   r3 = f_00215c40(obj);                           // imports 294/304/299/305
 *   if (r3 != 0) return 0;
 *   r3 = sceNpMatching2ContextStartAsync(ctx, 10s); // import 293
 *   if (r3 >= 0) { [obj+1066] = 1; return 1; }
 *   return 0;
 *
 * f_00216560 stores that return at [obj+593]; f_00216258 -- the forcing site
 * -- is exactly ld8[obj+593]. So forcing #1 says "pretend GetNpId
 * succeeded". On real hardware that is the SIGNED IN state.
 *
 * RPCS3 has both states. np_psn_status::disabled is signed out; psn_fake hands
 * the title a synthetic identity so the NP stack reports success without any
 * network. The second is the honest replacement for the forcing: it drives the
 * same game code with values the SDK really returns, instead of lying at the
 * leaf. References: refs/rpcs3/rpcs3/Emu/Cell/Modules/sceNp.cpp
 * (sceNpManagerGetNpId, sceNpManagerGetStatus), sceNp2.cpp
 * (sceNpMatching2Init2, sceNpMatching2CreateContext,
 * sceNpMatching2ContextStartAsync), Emu/NP/np_handler.cpp (get_psn_status).
 *
 * PS3BASE_NP_OFFLINE=1 switches the same binary to the signed-out state, so
 * both branches can be compared without a rebuild.
 * ---------------------------------------------------------------------- */



HLE_FUNC(sceNp2, sceNp2Init) {
    NP_ENTER("sceNp2Init");
    /* RPCS3: sceNp2Init wraps sceNpInit, then marks NP2 up. */
    const bool was = g_np.init;
    g_np.init = true;
    g_np.np2_init = true;
    LOG_INFO("sceNp", "sceNp2Init poolsize=0x%x pool=0x%08x -> 0", a32(ctx, 0), a32(ctx, 1));
    ret(ctx, was ? kAlreadyInit : 0);
}
HLE_FUNC(sceNp2, sceNp2Term) { g_np.np2_init = false; g_np.init = false; ret(ctx, 0); }

/* sceNpMatching2Init2(stackSize, priority, param) -- import 308.
 * RPCS3 requires sceNp2Init first, then marks the matching utility up. It
 * creates no thread and no heap. Before this existed the generic handler
 * returned 0 for every call and the title entered its online branch with
 * nothing behind it. */
HLE_FUNC(sceNp2, sceNpMatching2Init2) {
    const uint32_t stack = a32(ctx, 0), prio = a32(ctx, 1), param = a32(ctx, 2);
    LOG_INFO("sceNp", "Matching2Init2 stack=0x%x prio=%d param=0x%08x", stack, static_cast<int32_t>(prio), param);
    if (!g_np.np2_init) return ret(ctx, kMatch2NotInit);
    if (g_np.match2_init) return ret(ctx, static_cast<int64_t>(static_cast<int32_t>(0x80022302)));
    g_np.match2_init = true;
    ret(ctx, 0);
}
HLE_FUNC(sceNp2, sceNpMatching2Term2) { g_np.match2_init = false; ret(ctx, 0); }
HLE_FUNC(sceNp2, sceNpMatching2Term) { g_np.match2_init = false; ret(ctx, 0); }

/* sceNpMatching2CreateContext(npId, commId, passPhrase, ctxId, option).
 * RPCS3 returns SCE_NP_MATCHING2_ERROR_NOT_NP_SIGN_IN when the manager is not
 * ONLINE, and a context id otherwise. The id is only ever handed back to us,
 * so 1 is a valid one. */
HLE_FUNC(sceNp2, sceNpMatching2CreateContext) {
    const uint32_t npid = a32(ctx, 0), commid = a32(ctx, 1);
    const uint32_t pass = a32(ctx, 2), outid = a32(ctx, 3);
    const int32_t opt = s32(ctx, 4);
    std::string comm;
    if (commid && vm::range_mapped(commid, 9)) {
        char buf[10] = {};
        std::memcpy(buf, vm::ptr(commid), 9);
        comm = buf;
    }
    LOG_INFO("sceNp", "Matching2CreateContext npId=0x%08x comm=0x%08x('%s') pass=0x%08x ctxId=0x%08x opt=%d",
             npid, commid, comm.c_str(), pass, outid, opt);
    if (!g_np.match2_init) return ret(ctx, kMatch2NotInit);
    if (!npid || !commid || !pass || !outid) return ret(ctx, kMatch2InvalidArg);
    if (np_psn_status() != SCE_NP_MANAGER_STATUS_ONLINE) return ret(ctx, kMatch2NotSignedIn);
    g_np.ctx_created = true;
    g_np.ctx_id = 1;
    vm::write32(outid, 1);
    LOG_INFO("sceNp", "Matching2CreateContext -> 0 (ctx id 1)");
    ret(ctx, 0);
}
HLE_FUNC(sceNp2, sceNpMatching2DestroyContext) {
    const uint32_t ctxid = a32(ctx, 0);
    LOG_INFO("sceNp", "Matching2DestroyContext(%u)", ctxid);
    g_np.ctx_created = false;
    g_np.ctx_started = false;
    ret(ctx, 0);
}
/* SceNpMatching2ContextCallback = void(ctxId, event, eventCause, errorCode, arg)
 * RPCS3 raises SCE_NP_MATCHING2_CONTEXT_EVENT_Start (0x6f02) with cause
 * SCE_NP_MATCHING2_EVENT_CAUSE_CONTEXT_ACTION (11) when the start succeeds. */
constexpr int32_t kMatch2CtxEventStart = 0x6f02;
constexpr int32_t kMatch2CauseContextAction = 11;

HLE_FUNC(sceNp2, sceNpMatching2ContextStart) {
    const uint32_t ctxid = a32(ctx, 0);
    LOG_INFO("sceNp", "Matching2ContextStart(%u)", ctxid);
    if (!g_np.match2_init) return ret(ctx, kMatch2NotInit);
    if (!g_np.ctx_created) return ret(ctx, kMatch2CtxNotFound);
    if (g_np.ctx_started) return ret(ctx, kMatch2CtxStarted);
    g_np.ctx_started = true;
    if (g_np.ctx_cb) {
        const uint32_t cb = g_np.ctx_cb, arg = g_np.ctx_cb_arg;
        sysutil_post([cb, arg, ctxid](ppu::Context& c) {
            call_guest(c, cb, ctxid, kMatch2CtxEventStart, kMatch2CauseContextAction, 0, arg);
        });
    }
    ret(ctx, 0);
}
HLE_FUNC(sceNp2, sceNpMatching2ContextStartAsync) {
    const uint32_t ctxid = a32(ctx, 0);
    const uint32_t timeout = a32(ctx, 1);
    LOG_INFO("sceNp", "Matching2ContextStartAsync(%u, %u)", ctxid, timeout);
    if (!g_np.match2_init) return ret(ctx, kMatch2NotInit);
    if (!g_np.ctx_created) return ret(ctx, kMatch2CtxNotFound);
    if (g_np.ctx_started) return ret(ctx, kMatch2CtxStarted);
    g_np.ctx_started = true;
    if (g_np.ctx_cb) {
        const uint32_t cb = g_np.ctx_cb, arg = g_np.ctx_cb_arg;
        sysutil_post([cb, arg, ctxid](ppu::Context& c) {
            call_guest(c, cb, ctxid, kMatch2CtxEventStart, kMatch2CauseContextAction, 0, arg);
        });
    }
    ret(ctx, 0);
}
HLE_FUNC(sceNp2, sceNpMatching2RegisterContextCallback) {
    const uint32_t ctxid = a32(ctx, 0), cb = a32(ctx, 1), arg = a32(ctx, 2);
    LOG_INFO("sceNp", "Matching2RegisterContextCallback ctx=%u cb=0x%08x arg=0x%08x", ctxid, cb, arg);
    g_np.ctx_cb = cb;
    g_np.ctx_cb_arg = arg;
    ret(ctx, 0);
}
HLE_FUNC(sceNp2, sceNpMatching2RegisterSignalingCallback) {
    LOG_INFO("sceNp", "Matching2RegisterSignalingCallback ctx=%u cb=0x%08x arg=0x%08x",
             a32(ctx, 0), a32(ctx, 1), a32(ctx, 2));
    ret(ctx, 0);
}
HLE_FUNC(sceNp2, sceNpMatching2RegisterRoomEventCallback) {
    LOG_INFO("sceNp", "Matching2RegisterRoomEventCallback ctx=%u cb=0x%08x arg=0x%08x",
             a32(ctx, 0), a32(ctx, 1), a32(ctx, 2));
    ret(ctx, 0);
}
HLE_FUNC(sceNp2, sceNpMatching2SetDefaultRequestOptParam) {
    LOG_INFO("sceNp", "Matching2SetDefaultRequestOptParam ctx=%u opt=0x%08x", a32(ctx, 0), a32(ctx, 1));
    if (!g_np.match2_init) return ret(ctx, kMatch2NotInit);
    if (!a32(ctx, 1)) return ret(ctx, kMatch2InvalidArg);
    ret(ctx, 0);
}
/* sceNpMatching2GetServerIdListLocal(ctxId, serverId, serverIdNum).
 * RPCS3 returns not_an_error(server list size). With no connection the local
 * list is empty, so 0 (which is also 0) is the honest answer. */
HLE_FUNC(sceNp2, sceNpMatching2GetServerIdListLocal) {
    LOG_INFO("sceNp", "Matching2GetServerIdListLocal ctx=%u out=0x%08x num=%u",
             a32(ctx, 0), a32(ctx, 1), a32(ctx, 2));
    if (!g_np.match2_init) return ret(ctx, kMatch2NotInit);
    ret(ctx, 0);
}
HLE_FUNC(sceNp2, sceNpMatching2SignalingGetConnectionStatus) {
    LOG_INFO("sceNp", "Matching2SignalingGetConnectionStatus ctx=%u room=%u", a32(ctx, 0), a32(ctx, 1));
    if (!g_np.match2_init) return ret(ctx, kMatch2NotInit);
    /* No signaling session can exist without a network. */
    ret(ctx, static_cast<int64_t>(static_cast<int32_t>(0x8002230f)));
}
HLE_FUNC(sceNp2, sceNpMatching2SignalingGetLocalNetInfo) { ret(ctx, 0); }
HLE_FUNC(sceNp2, sceNpMatching2GetEventData) { ret(ctx, 0); }
HLE_FUNC(sceNp2, sceNpMatching2ClearEventData) { ret(ctx, 0); }
HLE_FUNC(sceNp, sceNpInit) {
    NP_ENTER("sceNpInit");
    const bool was = g_np.init;
    g_np.init = true;
    LOG_INFO("sceNp", "sceNpInit poolsize=0x%x pool=0x%08x -> 0", a32(ctx, 0), a32(ctx, 1));
    ret(ctx, was ? kAlreadyInit : 0);
}
HLE_FUNC(sceNp, sceNpTerm) { g_np.init = false; ret(ctx, 0); }
HLE_FUNC(sceNp, sceNpLookupInit) { ret(ctx, 0); }
/* sceNpManagerRegisterCallback(callback, arg).
 * SceNpManagerCallback = void(event, result, arg). Delivering nothing leaves a
 * state machine waiting; the manager is registered by the title and then
 * polled. */
HLE_FUNC(sceNp, sceNpManagerRegisterCallback) {
    const uint32_t cb = a32(ctx, 0), arg = a32(ctx, 1);
    uint32_t code = 0, toc = 0;
    if (cb && vm::range_mapped(cb, 8)) { code = vm::read32(cb); toc = vm::read32(cb + 4); }
    g_np.manager_cb = cb;
    g_np.manager_cb_arg = arg;
    LOG_INFO("sceNp", "ManagerRegisterCallback opd=0x%08x code=0x%08x toc=0x%08x arg=0x%08x",
             cb, code, toc, arg);
    ret(ctx, 0);
}
HLE_FUNC(sceNp, sceNpManagerUnregisterCallback) { g_np.manager_cb = 0; ret(ctx, 0); }
HLE_FUNC(sceNp, sceNpBasicRegisterContextSensitiveHandler) {
    const uint32_t cp = a32(ctx, 0), cb = a32(ctx, 1), arg = a32(ctx, 2);
    g_np.basic_cb = cb;
    g_np.basic_cb_arg = arg;
    LOG_INFO("sceNp", "BasicRegisterContextSensitiveHandler ctx=0x%08x cb=0x%08x arg=0x%08x", cp, cb, arg);
    ret(ctx, 0);
}
HLE_FUNC(sceNp, sceNpBasicRegisterHandler) {
    const uint32_t cp = a32(ctx, 0), cb = a32(ctx, 1), arg = a32(ctx, 2);
    g_np.basic_cb = cb;
    g_np.basic_cb_arg = arg;
    LOG_INFO("sceNp", "BasicRegisterHandler ctx=0x%08x cb=0x%08x arg=0x%08x", cp, cb, arg);
    ret(ctx, 0);
}
HLE_FUNC(sceNp, sceNpBasicGetEvent) { ret(ctx, kBasicNoEvent); }
HLE_FUNC(sceNp, sceNpManagerGetStatus) {
    const int32_t st = np_psn_status();
    if (uint32_t p = a32(ctx, 0)) vm::write32(p, static_cast<uint32_t>(st));
    LOG_INFO("sceNp", "ManagerGetStatus -> %d (%s)", st, st == SCE_NP_MANAGER_STATUS_ONLINE ? "online" : "offline");
    ret(ctx, 0);
}
/* sceNpManagerGetNpId(SceNpId* npId).
 * RPCS3: NOT_INITIALIZED / INVALID_ARGUMENT, then OFFLINE when signed out and
 * the identity otherwise. This is the call that decides whether the world
 * server thread is created at all (f_00216260 returns 0 on a negative r3). */
HLE_FUNC(sceNp, sceNpManagerGetNpId) {
    const uint32_t p = a32(ctx, 0);
    if (!g_np.init) return ret(ctx, kNotInit);
    if (!p) return ret(ctx, kInvalidArg);
    if (np_psn_status() != SCE_NP_MANAGER_STATUS_ONLINE) {
        LOG_INFO("sceNp", "ManagerGetNpId -> 0x%08x (OFFLINE: not signed in)", static_cast<uint32_t>(0x8002aa0c));
        return ret(ctx, static_cast<int64_t>(static_cast<int32_t>(0x8002aa0c)));
    }
    /* SceNpId: SceNpOnlineId handle { char data[17]; u8 dummy[3]; }, opt[8]. */
    uint8_t* d = vm::ptr(p);
    std::memset(d, 0, 28);
    std::memcpy(d, "mcps3base", 9);
    LOG_INFO("sceNp", "ManagerGetNpId -> 0, handle 'mcps3base' (simulated sign-in)");
    ret(ctx, 0);
}
HLE_FUNC(sceNp, sceNpManagerGetOnlineId) {
    if (!g_np.init) return ret(ctx, kNotInit);
    if (np_psn_status() != SCE_NP_MANAGER_STATUS_ONLINE)
        return ret(ctx, static_cast<int64_t>(static_cast<int32_t>(0x8002aa0c)));
    if (uint32_t p = a32(ctx, 0)) std::memset(vm::ptr(p), 0, 32);
    ret(ctx, 0);
}
HLE_FUNC(sceNp, sceNpManagerGetAccountAge) {
    if (!g_np.init) return ret(ctx, kNotInit);
    if (np_psn_status() != SCE_NP_MANAGER_STATUS_ONLINE)
        return ret(ctx, static_cast<int64_t>(static_cast<int32_t>(0x8002aa0c)));
    if (uint32_t p = a32(ctx, 0)) vm::write32(p, 0);
    ret(ctx, 0);
}
HLE_FUNC(sceNp, sceNpManagerGetAccountRegion) {
    if (uint32_t p = a32(ctx, 0)) std::memset(vm::ptr(p), 0, 4);
    ret(ctx, static_cast<int64_t>(static_cast<int32_t>(0x8002aa0c)));
}
HLE_FUNC(sceNp, sceNpManagerGetTicket) {
    ret(ctx, static_cast<int64_t>(static_cast<int32_t>(0x8002aa0c)));
}
HLE_FUNC(sceNp, sceNpScoreInit) {
    LOG_INFO("sceNp", "ScoreInit titleCtx=0x%08x pool=0x%08x size=0x%x", a32(ctx, 0), a32(ctx, 1), a32(ctx, 2));
    if (uint32_t p = a32(ctx, 0)) vm::write32(p, 1);
    ret(ctx, 0);
}
HLE_FUNC(sceNp, sceNpBasicSetPresenceDetails2) {
    LOG_INFO("sceNp", "BasicSetPresenceDetails2 details=0x%08x options=0x%x", a32(ctx, 0), a32(ctx, 1));
    ret(ctx, 0);
}

HLE_FUNC(sceNp, sceNpManagerGetChatRestrictionFlag) {
    if (uint32_t p = a32(ctx, 0)) vm::write32(p, 1);
    ret(ctx, 0);
}
HLE_FUNC(sceNp, sceNpManagerGetContentRatingFlag) {
    if (uint32_t p = a32(ctx, 0)) vm::write32(p, 0);
    if (uint32_t p = a32(ctx, 1)) vm::write32(p, 0);
    ret(ctx, 0);
}

HLE_FUNC(sceNpTrophy, sceNpTrophyInit) { ret(ctx, 0); }
HLE_FUNC(sceNpTrophy, sceNpTrophyTerm) { ret(ctx, 0); }
HLE_FUNC(sceNpTrophy, sceNpTrophyCreateContext) {
    if (uint32_t p = a32(ctx, 0)) vm::write32(p, 1);
    ret(ctx, 0);
}
HLE_FUNC(sceNpTrophy, sceNpTrophyCreateHandle) {
    if (uint32_t p = a32(ctx, 0)) vm::write32(p, 1);
    ret(ctx, 0);
}
HLE_FUNC(sceNpTrophy, sceNpTrophyRegisterContext) {
    /* The status callback is
     *   s32 (u32 context, u32 status, s32 completed, s32 total, void* arg)
     * -- argument 5 is the caller's own pointer, not a user id. The handler
     * read a32(ctx, 3) as that pointer, which is the trophy handle, so the
     * callback received a pointer into nothing and dereferenced it. Passing the
     * real arg through is the whole fix. */
    const uint32_t context = a32(ctx, 0);
    const uint32_t cb = a32(ctx, 2);
    const uint32_t arg = a32(ctx, 3);   /* the title's opaque pointer */
    if (cb) {
        /* SCE_NP_TROPHY_STATUS_INSTALLED (0) then AVAILABLE (1); completed and
         * total are zero because there is no trophy pack on this machine. */
        call_guest(ctx, cb, context, 0, 0, 0, arg);
        call_guest(ctx, cb, context, 1, 0, 0, arg);
    }
    ret(ctx, 0);
}
/* sceNpTrophyGetGameInfo( context, handle, SceNpTrophyGameDetails* details,
 *                        SceNpTrophyGameData* data )
 *
 * Both structures are output-only. Leaving them unfilled was not a cosmetic
 * gap: the caller wrote the returned counts into its own list and walked it,
 * so a garbage count meant a walk off the end -- which is a vm::write32 fault
 * at address 0x18, three functions away from here.
 *
 *   SceNpTrophyGameDetails: u32 numTrophies, numPlatinum, numGold, numSilver,
 *                           numBronze; char title[128]; char description[1024];
 *                           u8 reserved[4]           (1200 bytes)
 *   SceNpTrophyGameData:    u32 unlocked*5          (20 bytes)
 */
HLE_FUNC(sceNpTrophy, sceNpTrophyGetGameInfo) {
    const uint32_t details = a32(ctx, 2);
    const uint32_t data = a32(ctx, 3);
    if (details) std::memset(vm::ptr(details), 0, 1200);
    if (data) std::memset(vm::ptr(data), 0, 20);
    LOG_INFO("trophy", "GetGameInfo -> 0 trophies (no pack installed)");
    ret(ctx, 0);
}

HLE_FUNC(sceNpTrophy, sceNpTrophyGetRequiredDiskSpace) {
    if (uint32_t p = a32(ctx, 2)) vm::write64(p, 0);
    ret(ctx, 0);
}
HLE_FUNC(sceNpTrophy, sceNpTrophyUnlockTrophy) {
    LOG_INFO("trophy", "unlock trophy %d", s32(ctx, 2));
    if (uint32_t p = a32(ctx, 3)) vm::write32(p, 0xFFFFFFFF);
    ret(ctx, 0);
}
HLE_FUNC(sceNpTrophy, sceNpTrophyGetTrophyUnlockState) {
    if (uint32_t p = a32(ctx, 2)) std::memset(vm::ptr(p), 0, 16);
    if (uint32_t p = a32(ctx, 3)) vm::write32(p, 0);
    ret(ctx, 0);
}
HLE_FUNC(sceNpTrophy, sceNpTrophyGetGameProgress) {
    if (uint32_t p = a32(ctx, 2)) vm::write32(p, 0);
    ret(ctx, 0);
}

}
