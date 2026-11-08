#include "common.h"

#include <atomic>
#include <cstdlib>
#include "runtime/cbwatch.h"
#include "runtime/sysutil_events.h"

#include <cstring>

namespace hle {

namespace {

constexpr int32_t SCE_NP_MANAGER_STATUS_OFFLINE = -1;
constexpr int32_t SCE_NP_MANAGER_STATUS_ONLINE = 3;
constexpr int32_t CELL_NET_CTL_STATE_Disconnected = 0;
/* 3, not 4. The title's own consumer (out/ppu/mc_003.cpp f_00074080) loads
 * the state cellNetCtlGetState wrote and compares it against 3 to decide
 * "online"; refs/rpcs3 cellNetCtl.h agrees (Disconnected 0, Connecting 1,
 * IPObtaining 2, IPObtained 3). Reporting 4 made every "am I connected" test
 * in the title false even with PS3BASE_NET_CONNECTED set. */
constexpr int32_t CELL_NET_CTL_STATE_IPObtained = 3;
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

/* Raise a system command to every registered sysutil callback.
 *
 * CellSysutilCallback is void(u64 status, u64 param, void* userdata):
 * the EVENT is the FIRST argument and the title's own pointer is the THIRD.
 * This call used to pass them the other way round -- userdata in r3, event in
 * r4 -- so every sysutil delivery reached the title with a heap pointer where
 * the event code belonged. All four registered handlers dispatch on r3
 * (0x0021a3d4 compares it against 0x801/0x802, 0x0021d850 against
 * 0x121/0x122/0x131/0x132/0x141/0x142/0x101), so not one system event has
 * ever been recognised since this module existed. */
static void sysutil_send(uint64_t event, uint64_t param = 0) {
    for (int slot = 0; slot < 4; slot++) {
        const uint32_t fn = callback_function(slot);
        if (!fn) continue;
        const uint32_t arg = callback_arg(slot);
        sysutil_post([fn, arg, event, param](ppu::Context& c) {
            call_guest(c, fn, event, param, arg);
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
    /* piste 43: sceNpCommerce2 state. The title opens a local store session
     * while loading a save; with no implementation at all it saw a failed
     * session and aborted the load with "Connection to the server was lost". */
    bool commerce2_init = false;
    uint32_t commerce2_ctx_id = 0;
    uint32_t commerce2_ctx_cb = 0;
    uint32_t commerce2_ctx_arg = 0;
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
    /* How many events each registration has been given. The census can only
     * say "registered and never delivered"; these say whether the delivery we
     * added actually happened, and how many times. */
    uint32_t manager_delivered = 0;
    uint32_t basic_delivered = 0;
    uint32_t netctl_delivered = 0;
};

NpState g_np;

/* RPCS3 np_handler::get_psn_status in its two offline-capable modes. The
 * simulated mode is a sign-in the SDK itself can produce; it is not a lie
 * about an error path, which is what the forcing is. */
int32_t np_psn_status() {
    /* Offline is the DEFAULT, and an opt-in switch selects the simulated
     * sign-in. The reverse was a regression: with ONLINE reported, the title
     * believes it has a session, starts one, and then dies on
     * "Connection to the server was lost -- exiting to the main menu". That
     * happened AFTER the world was created, so a short proof run never saw it.
     *
     * Offline is also the mode the loading proof used
     * (docs/loading-43/REPORT.md), so making it the default keeps both paths:
     * creating a world and loading one. PS3BASE_NP_ONLINE=1 opts into the
     * simulated sign-in for anyone who wants to test that branch. */
    static const bool online = std::getenv("PS3BASE_NP_ONLINE") != nullptr;
    return online ? SCE_NP_MANAGER_STATUS_ONLINE : SCE_NP_MANAGER_STATUS_OFFLINE;
}

/* SCE_NP_MANAGER_EVENT_GOT_TICKET (255) is the only event RPCS3's header
 * names, and it is a ticket event, not a sign-in one. The sign-in event is the
 * manager STATUS itself: the title's own manager callback
 * (code 0x0021a370) compares its first argument against 3 and 255, and on 3 it
 * calls 0x00215db8 -- sceNpMatching2Init2, sceNpManagerGetNpId,
 * sceNpMatching2CreateContext, sceNpMatching2ContextStartAsync. That is the
 * whole NP setup, reached from the callback instead of from the poll loop.
 * A port that has already made this work offline delivers the status the same
 * way: refs/jdc-ps3recomp/libs/network/sceNp.c, np_fire_manager_cb_once. */
constexpr int32_t kBasicEndOfInitialPresence = 6;   /* SCE_NP_BASIC_EVENT_* */
constexpr int32_t kNetCtlEventEstablish = 1;        /* CELL_NET_CTL_EVENT_*  */

/* Deliver the manager's current sign-in state to the title's callback, once per
 * registration. Runs on the guest thread that polls cellSysutilCheckCallback,
 * which is the thread a real firmware delivers on. */
void np_deliver_manager_status() {
    if (!g_np.manager_cb) return;
    const uint32_t cb = g_np.manager_cb, arg = g_np.manager_cb_arg;
    const int32_t st = np_psn_status();
    g_np.manager_delivered++;
    sysutil_post([cb, arg, st](ppu::Context& c) {
        /* SceNpManagerCallback = void(s32 event, s32 result, void* arg) */
        call_guest(c, cb, static_cast<uint32_t>(st), 0, arg);
    });
    LOG_INFO("sceNp", "manager callback 0x%08x <- event %d (%s), arg 0x%08x",
             cb, st, st == SCE_NP_MANAGER_STATUS_ONLINE ? "online" : "offline", arg);
}

/* SCE_NP_BASIC_EVENT_END_OF_INITIAL_PRESENCE: what a registered basic handler
 * is told once the initial presence pass is over. RPCS3 sends exactly this
 * (np_handler.cpp, handle_initial_presence) and nothing else without a
 * network. It is a "you may stop waiting" event, not a social one. */
void np_deliver_basic_presence_end() {
    if (!g_np.basic_cb) return;
    const uint32_t cb = g_np.basic_cb, arg = g_np.basic_cb_arg;
    g_np.basic_delivered++;
    sysutil_post([cb, arg](ppu::Context& c) {
        /* SceNpBasicEventHandler = void(s32 event, s32 retCode, u32 reqId, void* arg) */
        call_guest(c, cb, static_cast<uint32_t>(kBasicEndOfInitialPresence), 0, 0, arg);
    });
    LOG_INFO("sceNp", "basic handler 0x%08x <- END_OF_INITIAL_PRESENCE, arg 0x%08x", cb, arg);
}

}  // namespace npdetail

using namespace npdetail;

/* cellNetCtl. RPCS3 tracks an init flag, hands out a handler id, and reports
 * the connection state the emulated console is in. The dialog and its events
 * stay exactly as they were: they are what makes the title offer
 * "Continue playing offline". */
HLE_FUNC(cellNetCtl, cellNetCtlInit) { g_np.netctl_init = true; ret(ctx, 0); }
HLE_FUNC(cellNetCtl, cellNetCtlTerm) { g_np.netctl_init = false; ret(ctx, 0); }
/* The transition a handler receives when the interface comes up:
 * CellNetCtlHandler = void(s32 prev_state, s32 new_state, s32 event,
 *                           s32 error_code, void* arg)
 * -- five arguments, with the caller's pointer LAST. Compare
 * CellSysutilCallback, whose userdata is the THIRD: getting these two
 * conventions confused is exactly the defect that made every sysutil
 * delivery unrecognisable. */
void np_deliver_netctl_establish() {
    if (!g_np.netctl_handler) return;
    const uint32_t h = g_np.netctl_handler, arg = g_np.netctl_arg;
    g_np.netctl_delivered++;
    sysutil_post([h, arg](ppu::Context& c) {
        call_guest(c, h, CELL_NET_CTL_STATE_Disconnected /* prev */,
                   CELL_NET_CTL_STATE_IPObtained /* new */,
                   kNetCtlEventEstablish, 0, arg);
    });
    LOG_INFO("cellNetCtl", "handler 0x%08x <- ESTABLISH 0->3, arg 0x%08x", h, arg);
}

/* cellNetCtlAddHandler(handler, arg, s32* hid).
 * The transition is delivered only when there is one to report. The state this
 * runtime reports is Disconnected unless PS3BASE_NET_CONNECTED is set, and a
 * Disconnected interface never raises ESTABLISH -- so by default the handler
 * correctly stays silent, and RPCS3 agrees (its AddHandler is a todo stub).
 * PS3BASE_NETCTL_ESTABLISH=1 delivers the transition anyway, so the delivery
 * path itself can be measured rather than assumed. */
HLE_FUNC(cellNetCtl, cellNetCtlAddHandler) {
    /* Census: record the registered handler so the inventory reports it. */
    /* (handler, arg, hid): the handler is argument 0, not 1. The census was
     * keyed on argument 1 -- the title's own pointer -- and filed a stack
     * address as the "opd", so the one registration it exists to make visible
     * was reported under the wrong number. */
    cbwatch::note_register("netctl-handler", a32(ctx, 0), a32(ctx, 1));
    if (!g_np.netctl_init) return ret(ctx, CELL_NET_CTL_ERROR_NOT_INITIALIZED);
    const uint32_t hid = a32(ctx, 2);
    if (!hid) return ret(ctx, CELL_NET_CTL_ERROR_INVALID_ADDR);
    g_np.netctl_handler = a32(ctx, 0);
    g_np.netctl_arg = a32(ctx, 1);
    vm::write32(hid, 1);
    LOG_INFO("cellNetCtl", "AddHandler handler=0x%08x arg=0x%08x -> hid 1", a32(ctx, 0), a32(ctx, 1));
    static const bool force_establish = std::getenv("PS3BASE_NETCTL_ESTABLISH") != nullptr;
    if (force_establish || std::getenv("PS3BASE_NET_CONNECTED")) np_deliver_netctl_establish();
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
 * state machine waiting, so the registration is followed by the event itself:
 * the sign-in status. RPCS3 does not need to send it because its manager is a
 * live object; refs/jdc-ps3recomp/libs/network/sceNp.c fires it from the
 * registration for exactly this failure (np_fire_manager_cb_once), and its
 * comment names the symptom: an event-driven title never leaves
 * "initializing" and never reaches its teardown/savedata phase. */
HLE_FUNC(sceNp, sceNpManagerRegisterCallback) {
    cbwatch::note_register("np-manager", a32(ctx, 0), a32(ctx, 1));
    const uint32_t cb = a32(ctx, 0), arg = a32(ctx, 1);
    uint32_t code = 0, toc = 0;
    if (cb && vm::range_mapped(cb, 8)) { code = vm::read32(cb); toc = vm::read32(cb + 4); }
    g_np.manager_cb = cb;
    g_np.manager_cb_arg = arg;
    LOG_INFO("sceNp", "ManagerRegisterCallback opd=0x%08x code=0x%08x toc=0x%08x arg=0x%08x",
             cb, code, toc, arg);
    np_deliver_manager_status();
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
    /* (context, handler, arg): the handler is argument 1, the context
     * pointer is argument 0. Keying the census on argument 0 filed this
     * registration under the SceNpCommunicationId address, so the delivery
     * below landed on a different key and the ledger still read 0. */
    cbwatch::note_register("np-basic", a32(ctx, 1), a32(ctx, 2));
    const uint32_t cp = a32(ctx, 0), cb = a32(ctx, 1), arg = a32(ctx, 2);
    g_np.basic_cb = cb;
    g_np.basic_cb_arg = arg;
    LOG_INFO("sceNp", "BasicRegisterHandler ctx=0x%08x cb=0x%08x arg=0x%08x", cp, cb, arg);
    np_deliver_basic_presence_end();
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
    /* piste 43: this must be 0. It was hardcoded to 1, which the title reads as
     * "this local player has chat restrictions", and it then refuses EVERY
     * attempt to start a world -- including loading an existing save -- with
     * "Failed to create game ... Online is disabled on their Sony Entertainment
     * Network account due to chat restrictions. Uncheck the 'Online Game' box
     * in 'More Options' to start an offline game." That dialog has no offline
     * path out of it, so the load never begins.
     *
     * refs/rpcs3/.../sceNp.cpp:4271 reads the *parental-control* information and
     * sets *isRestricted = 0 when nothing restricts chat. We have no parental
     * control to consult, so an unrestricted local profile is the correct
     * emulation, not a forcing. */
    if (uint32_t p = a32(ctx, 0)) vm::write32(p, 0);
    LOG_INFO("sceNp", "ManagerGetChatRestrictionFlag -> 0");
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

/* piste 43 -- sceNpCommerce2, which had NO implementation at all.
 *
 * Measured failure: with the chat-restriction flag corrected, pressing Load on
 * an existing save read the whole save (METADATA, THUMB, GAMEDATA 0x3a4eaf
 * bytes) and then the title raised "Network Error -- Connection to the server
 * was lost. Exiting to the main menu." The log at that instant showed three
 * unimplemented imports in a row:
 *
 *   unimplemented sceNpCommerce2::sceNpCommerce2Init
 *   unimplemented sceNpCommerce2::sceNpCommerce2CreateCtx
 *   unimplemented sceNpCommerce2::sceNpCommerce2CreateSessionStart
 *
 * An unimplemented import resolves nowhere and returns nothing defined, so the
 * title saw a failed store session and abandoned the load. RPCS3 implements
 * these as local, offline objects: Init just sets a flag, CreateCtx allocates an
 * id, CreateSessionStart queues the "session done" callback with no network at
 * all. References:
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/sceNpCommerce2.cpp:129 (Init),
 *   :157 (CreateCtx), :262 (CreateSessionStart), :306 (CreateSessionFinish).
 * The return codes are RPCS3's: a second Init is ALREADY_INITIALIZED, a call
 * before Init is NOT_INITIALIZED. */
namespace {
constexpr int64_t SCE_NP_COMMERCE2_ERROR_NOT_INITIALIZED = static_cast<int32_t>(0x8002b701);
constexpr int64_t SCE_NP_COMMERCE2_ERROR_ALREADY_INITIALIZED = static_cast<int32_t>(0x8002b702);
constexpr int64_t SCE_NP_COMMERCE2_ERROR_CTX_NOT_FOUND = static_cast<int32_t>(0x8002b70b);
constexpr int64_t SCE_NP_COMMERCE2_ERROR_CTX_MAX = static_cast<int32_t>(0x8002b70a);
constexpr int64_t SCE_NP_COMMERCE2_ERROR_INVALID_ARGUMENT = static_cast<int32_t>(0x8002b703);
/* SCE_NP_COMMERCE2_EVENT_CREATE_SESSION_DONE, the event RPCS3 delivers. */
constexpr uint32_t SCE_NP_COMMERCE2_EVENT_CREATE_SESSION_DONE = 0x4;
}  // namespace

HLE_FUNC(sceNpCommerce2, sceNpCommerce2Init) {
    LOG_INFO("sceNp", "Commerce2Init");
    if (g_np.commerce2_init) return ret(ctx, SCE_NP_COMMERCE2_ERROR_ALREADY_INITIALIZED);
    /* RPCS3 requires the NP manager to be initialised first. Our g_np.init is
     * set by sceNpInit, which the title calls during boot. */
    if (!g_np.init) return ret(ctx, SCE_NP_ERROR_NOT_INITIALIZED);
    g_np.commerce2_init = true;
    ret(ctx, 0);
}
HLE_FUNC(sceNpCommerce2, sceNpCommerce2Term) {
    LOG_INFO("sceNp", "Commerce2Term");
    g_np.commerce2_init = false;
    ret(ctx, 0);
}
HLE_FUNC(sceNpCommerce2, sceNpCommerce2CreateCtx) {
    const uint32_t version = a32(ctx, 0);
    const uint32_t np_id = a32(ctx, 1);
    const uint32_t handler = a32(ctx, 2);
    const uint32_t arg = a32(ctx, 3);
    const uint32_t ctx_id_out = a32(ctx, 4);
    LOG_INFO("sceNp", "Commerce2CreateCtx version=%u npId=0x%08x handler=0x%08x arg=0x%08x out=0x%08x",
             version, np_id, handler, arg, ctx_id_out);
    if (!g_np.commerce2_init) return ret(ctx, SCE_NP_COMMERCE2_ERROR_NOT_INITIALIZED);
    if (!np_id) return ret(ctx, SCE_NP_COMMERCE2_ERROR_INVALID_ARGUMENT);
    /* One context is enough for a single local store session; RPCS3's CTX_MAX
     * check is a TODO there too. */
    g_np.commerce2_ctx_id = g_np.commerce2_ctx_id ? g_np.commerce2_ctx_id : 1;
    g_np.commerce2_ctx_cb = handler;
    g_np.commerce2_ctx_arg = arg;
    if (ctx_id_out) vm::write32(ctx_id_out, g_np.commerce2_ctx_id);
    ret(ctx, 0);
}
HLE_FUNC(sceNpCommerce2, sceNpCommerce2DestroyCtx) {
    LOG_INFO("sceNp", "Commerce2DestroyCtx ctx=%u", a32(ctx, 0));
    if (!g_np.commerce2_init) return ret(ctx, SCE_NP_COMMERCE2_ERROR_NOT_INITIALIZED);
    g_np.commerce2_ctx_id = 0;
    g_np.commerce2_ctx_cb = 0;
    ret(ctx, 0);
}
HLE_FUNC(sceNpCommerce2, sceNpCommerce2CreateSessionStart) {
    const uint32_t ctx_id = a32(ctx, 0);
    LOG_INFO("sceNp", "Commerce2CreateSessionStart ctx=%u", ctx_id);
    if (!g_np.commerce2_init) return ret(ctx, SCE_NP_COMMERCE2_ERROR_NOT_INITIALIZED);
    if (!g_np.commerce2_ctx_id) return ret(ctx, SCE_NP_COMMERCE2_ERROR_CTX_NOT_FOUND);
    /* RPCS3 queues the completion callback immediately: the session is a local
     * object, so it succeeds without touching the network. Deliver the same
     * event through the sysutil queue, exactly as the context callbacks above. */
    const uint32_t cb = g_np.commerce2_ctx_cb, arg = g_np.commerce2_ctx_arg;
    if (cb) {
        sysutil_post([cb, arg, ctx_id](ppu::Context& c) {
            call_guest(c, cb, ctx_id, SCE_NP_COMMERCE2_EVENT_CREATE_SESSION_DONE, 0, arg);
        });
    }
    ret(ctx, 0);
}
HLE_FUNC(sceNpCommerce2, sceNpCommerce2CreateSessionAbort) {
    LOG_INFO("sceNp", "Commerce2CreateSessionAbort ctx=%u", a32(ctx, 0));
    if (!g_np.commerce2_init) return ret(ctx, SCE_NP_COMMERCE2_ERROR_NOT_INITIALIZED);
    ret(ctx, 0);
}
HLE_FUNC(sceNpCommerce2, sceNpCommerce2CreateSessionFinish) {
    const uint32_t ctx_id = a32(ctx, 0);
    const uint32_t info = a32(ctx, 1);
    LOG_INFO("sceNp", "Commerce2CreateSessionFinish ctx=%u info=0x%08x", ctx_id, info);
    if (!g_np.commerce2_init) return ret(ctx, SCE_NP_COMMERCE2_ERROR_NOT_INITIALIZED);
    if (!g_np.commerce2_ctx_id) return ret(ctx, SCE_NP_COMMERCE2_ERROR_CTX_NOT_FOUND);
    /* Currency and decimals, zeroed: no store is reachable offline, and RPCS3
     * memsets the same 8 bytes for a pre-3.00 SDK title like this one. */
    if (info) std::memset(vm::ptr(info), 0, 44);
    ret(ctx, 0);
}

}
