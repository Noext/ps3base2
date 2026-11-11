#include "common.h"
#include "commerce2_hle.h"
#include "np_hle.h"

#include <atomic>
#include <cstdlib>
#include "runtime/cbwatch.h"
#include "runtime/sysutil_events.h"
#include "runtime/vfs.h"

#include <cstring>

namespace hle {

namespace {

constexpr int32_t SCE_NP_MANAGER_STATUS_OFFLINE = -1;
constexpr int32_t SCE_NP_MANAGER_STATUS_ONLINE = 3;
constexpr int64_t SCE_NP_ERROR_NOT_INITIALIZED = static_cast<int32_t>(0x8002aa01);
constexpr int64_t SCE_NP_ERROR_ALREADY_INITIALIZED = static_cast<int32_t>(0x8002aa02);
constexpr int64_t SCE_NP_ERROR_INVALID_ARGUMENT = static_cast<int32_t>(0x8002aa03);
constexpr int64_t SCE_NP_ERROR_OFFLINE = static_cast<int32_t>(0x8002aa0c);

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
/* The sceNpBasic constants live in np_hle.h. This file used to define
 * kBasicNoEvent = 0x8002a66b, which is SCE_NP_BASIC_ERROR_EXCEEDS_MAX, and
 * returned it from every sceNpBasicGetEvent poll: a title asking "is there an
 * event" was told "too many recipients". NO_EVENT is 0x8002a66a (sceNp.h:91). */

struct NpState {
    bool init = false;
    bool np2_init = false;
    /* Still read by the 18 sceNp2 handlers below. It was removed from the
     * struct while they were still here, which left the tree unable to compile
     * ("no member named 'match2_init'"). Restored so the two are consistent;
     * whoever finishes moving sceNp2 out removes both together. */
    bool match2_init = false;
    uint32_t manager_cb = 0;
    uint32_t manager_cb_arg = 0;
    uint32_t basic_cb = 0;
    uint32_t basic_cb_arg = 0;
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

/* The decision rules live in np_hle.h so that tests/np.cpp drives exactly the
 * code the imports run. This is the only place that binds them to the guest's
 * NP manager: sceNp2Init brings the NP manager up too (refs/rpcs3/.../sceNp2.cpp:223),
 * so the flag is g_np.init -- never a second copy that could drift from it. */
np::State& np_state() {
    np::State& s = np::state();
    s.np_init = g_np.init;
    return s;
}

/* Store the rule's output, if it has one, at the caller's address, then return
 * the rule's code. One helper so no handler can forget the write or write at
 * the wrong argument index. */
void np_ret(ppu::Context& ctx, const np::Result& r, uint32_t out_ea) {
    if (r.has_value && out_ea) vm::write32(out_ea, static_cast<uint32_t>(r.value));
    ret(ctx, r.code);
}

}  // namespace npdetail

using namespace npdetail;

/* cellNetCtl lives in runtime/src/hle/cellNetCtl.cpp. It was here, and the
 * move matters: a second HLE_FUNC(cellNetCtl, ...) for the same name would
 * register a second entry under "cellNetCtl::name" in the same map
 * (runtime/src/ppu_runtime.cpp:74-83), and which one wins is the static
 * constructor order -- a silent coin flip. */

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
/* ------------------------------------------------------------------------
 * sceNp -- the 39 imports BLES00680 (Red Dead Redemption) needs, plus the
 * manager getters other titles already used here.
 *
 * Every rule is in np_hle.h, cited to refs/rpcs3, and driven by tests/np.cpp;
 * these handlers only bind guest arguments to those rules and store the
 * output. Nothing below answers CELL_OK for work it did not do: the identity
 * getters report OFFLINE offline, the lookup/community calls report
 * INVALID_ONLINE_ID, and the local objects (signaling contexts, history,
 * score init) really are created.
 * ---------------------------------------------------------------------- */

/* The simulated identity a signed-in console hands out. SceNpId is 0x24 bytes
 * (sceNp.h:1266-1298): SceNpOnlineId handle { char data[16+1]; u8 dummy[3] },
 * then opt[8] and reserved[8]. Offline there is no identity: the getter returns
 * OFFLINE without writing one, so this leaves the buffer zeroed and
 * np::is_own_npid() then never matches a peer. */
void np_self_npid(uint8_t out[np::NPID_SIZE]) {
    std::memset(out, 0, np::NPID_SIZE);
    if (np_psn_status() != SCE_NP_MANAGER_STATUS_ONLINE) return;
    std::memcpy(out, "mcps3base", 9);
}

/* Copy a guest SceNpId. False when the pointer is null or unmapped, which the
 * rules report as a missing argument rather than dereferencing it. */
bool np_read_npid(uint32_t ea, uint8_t out[np::NPID_SIZE]) {
    if (!ea || !vm::range_mapped(ea, np::NPID_SIZE)) return false;
    std::memcpy(out, vm::ptr(ea), np::NPID_SIZE);
    return true;
}

void np_clear_state() {
    np::State& s = np::state();
    s.np_init = false;
    s.lookup_init = false;
    s.score_init = false;
    s.basic_handler_registered = false;
    s.basic_handler = 0;
    s.basic_arg = 0;
    s.title_ctxs.clear();
    s.trans.clear();
    s.sigs.clear();
    s.conns.clear();
    s.ticket_size = 0;
}
HLE_FUNC(sceNp, sceNpInit) {
    NP_ENTER("sceNpInit");
    const uint32_t poolsize = a32(ctx, 0), poolptr = a32(ctx, 1);
    LOG_INFO("sceNp", "sceNpInit poolsize=0x%x pool=0x%08x", poolsize, poolptr);
    /* sceNp.cpp:585-601, in RPCS3's order: already-init first, then the two
     * argument checks. 128 KiB is SCE_NP_MIN_POOL_SIZE (sceNp.h:825). */
    if (g_np.init) return ret(ctx, kAlreadyInit);
    if (poolsize == 0 || !poolptr) return ret(ctx, kInvalidArg);
    if (poolsize < 128 * 1024) return ret(ctx, np::E_INSUFFICIENT_BUFFER);
    g_np.init = true;
    np::state().np_init = true;
    ret(ctx, 0);
}
HLE_FUNC(sceNp, sceNpTerm) {
    /* sceNp.cpp:608-632: NOT_INITIALIZED when not up, and the teardown clears
     * the signaling contexts and the lookup/score state with it. */
    LOG_INFO("sceNp", "sceNpTerm");
    if (!g_np.init) return ret(ctx, kNotInit);
    np_clear_state();
    g_np.init = false;
    ret(ctx, 0);
}
/* ---- sceNpLookup ------------------------------------------------------ */
HLE_FUNC(sceNp, sceNpLookupInit) {
    /* sceNp.cpp:3232-3251. */
    const int64_t r = np::lookup_init(np_state());
    LOG_INFO("sceNp", "LookupInit -> 0x%08x", static_cast<uint32_t>(r));
    ret(ctx, r);
}
HLE_FUNC(sceNp, sceNpLookupTerm) {
    /* sceNp.cpp:3253-3272. */
    const int64_t r = np::lookup_term(np_state());
    LOG_INFO("sceNp", "LookupTerm -> 0x%08x", static_cast<uint32_t>(r));
    ret(ctx, r);
}
/* sceNpLookupCreateTitleCtx(communicationId, selfNpId): the id is the RETURN
 * VALUE, not an output pointer (sceNp.cpp:3274-3296). */
HLE_FUNC(sceNp, sceNpLookupCreateTitleCtx) {
    const uint32_t comm = a32(ctx, 0), self = a32(ctx, 1);
    const np::Result r = np::lookup_create_title_ctx(np_state(), comm != 0, self != 0);
    LOG_INFO("sceNp", "LookupCreateTitleCtx comm=0x%08x self=0x%08x -> 0x%08x",
             comm, self, static_cast<uint32_t>(r.code));
    ret(ctx, r.code);
}
HLE_FUNC(sceNp, sceNpLookupDestroyTitleCtx) {
    const int32_t id = s32(ctx, 0);
    const int64_t r = np::lookup_destroy_title_ctx(np_state(), id);
    LOG_INFO("sceNp", "LookupDestroyTitleCtx(%d) -> 0x%08x", id, static_cast<uint32_t>(r));
    ret(ctx, r);
}
HLE_FUNC(sceNp, sceNpLookupCreateTransactionCtx) {
    const int32_t title = s32(ctx, 0);
    const np::Result r = np::lookup_create_transaction_ctx(np_state(), title, np_psn_status());
    LOG_INFO("sceNp", "LookupCreateTransactionCtx(title %d) -> 0x%08x", title,
             static_cast<uint32_t>(r.code));
    ret(ctx, r.code);
}
HLE_FUNC(sceNp, sceNpLookupDestroyTransactionCtx) {
    const int32_t id = s32(ctx, 0);
    const int64_t r = np::lookup_destroy_transaction_ctx(np_state(), id);
    LOG_INFO("sceNp", "LookupDestroyTransactionCtx(%d) -> 0x%08x", id, static_cast<uint32_t>(r));
    ret(ctx, r);
}
/* sceNpLookupSetTimeout(ctxId, timeout): the timeout is usecond_t, a 64-bit
 * argument in ONE register (sceNp.h:27, sceNp.cpp:3365). */
HLE_FUNC(sceNp, sceNpLookupSetTimeout) {
    const int32_t id = s32(ctx, 0);
    const uint64_t timeout = a64(ctx, 1);
    const int64_t r = np::lookup_set_timeout(np_state(), id, timeout);
    LOG_INFO("sceNp", "LookupSetTimeout(%d, %llu) -> 0x%08x", id,
             static_cast<unsigned long long>(timeout), static_cast<uint32_t>(r));
    ret(ctx, r);
}
HLE_FUNC(sceNp, sceNpLookupPollAsync) {
    const int32_t id = s32(ctx, 0);
    const uint32_t out = a32(ctx, 1);
    const np::Result r = np::lookup_poll_async(np_state(), id);
    LOG_INFO("sceNp", "LookupPollAsync(%d) -> 0x%08x", id, static_cast<uint32_t>(r.code));
    if (r.has_value && out && vm::range_mapped(out, 4))
        vm::write32(out, static_cast<uint32_t>(r.value));
    ret(ctx, r.code);
}
HLE_FUNC(sceNp, sceNpLookupNpIdAsync) {
    const int32_t id = s32(ctx, 0);
    const uint32_t online_id = a32(ctx, 1);
    const int64_t r = np::lookup_npid_async(np_state(), online_id != 0, id, np_psn_status());
    LOG_INFO("sceNp", "LookupNpIdAsync(%d) onlineId=0x%08x -> 0x%08x", id, online_id,
             static_cast<uint32_t>(r));
    ret(ctx, r);
}
/* sceNpLookupTitleSmallStorageAsync(transId, data, maxSize, contentLength, prio,
 * option): prio is an s32 and option is the sixth argument (sceNp.cpp:3890). */
HLE_FUNC(sceNp, sceNpLookupTitleSmallStorageAsync) {
    const int32_t id = s32(ctx, 0);
    const uint32_t data = a32(ctx, 1);
    const uint32_t max_size = a32(ctx, 2);
    const uint32_t len = a32(ctx, 3);
    const uint32_t option = a32(ctx, 5);
    const int64_t r = np::lookup_title_small_storage_async(
        np_state(), max_size, data != 0, len != 0, option != 0, np_psn_status());
    LOG_INFO("sceNp", "LookupTitleSmallStorageAsync(%d, max 0x%x) -> 0x%08x", id, max_size,
             static_cast<uint32_t>(r));
    ret(ctx, r);
}
/* ---- the manager ------------------------------------------------------ */
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
    np::State& s = np_state();
    const int64_t r = np::manager_register_callback(s, cb != 0);
    LOG_INFO("sceNp", "ManagerRegisterCallback opd=0x%08x arg=0x%08x -> 0x%08x", cb, arg,
             static_cast<uint32_t>(r));
    if (r != 0) return ret(ctx, r);
    g_np.manager_cb = cb;
    g_np.manager_cb_arg = arg;
    s.manager_cb = cb;
    s.manager_cb_arg = arg;
    np_deliver_manager_status();
    ret(ctx, 0);
}
HLE_FUNC(sceNp, sceNpManagerUnregisterCallback) {
    const int64_t r = np::manager_unregister_callback(np_state());
    LOG_INFO("sceNp", "ManagerUnregisterCallback -> 0x%08x", static_cast<uint32_t>(r));
    if (r == 0) {
        g_np.manager_cb = 0;
        g_np.manager_cb_arg = 0;
    }
    ret(ctx, r);
}
HLE_FUNC(sceNp, sceNpManagerGetStatus) {
    /* sceNp.cpp:3934-3953. The value written is the manager state, so a signed
     * out console says -1 here (see the module comment at the top of this
     * block for why offline is the default). */
    const uint32_t out = a32(ctx, 0);
    const np::Result r = np::manager_get_status(np_state(), out != 0, np_psn_status());
    LOG_INFO("sceNp", "ManagerGetStatus -> %d", np_psn_status());
    if (r.has_value && out) vm::write32(out, static_cast<uint32_t>(r.value));
    ret(ctx, r.code);
}
/* sceNpManagerGetNpId(SceNpId* npId).
 * This is the call that decides whether the world server thread is created at
 * all (out/ppu/mc_020.cpp f_00216260 returns 0 on a negative r3). Offline it
 * answers SCE_NP_ERROR_OFFLINE as a VALUE (sceNp.cpp:4033), which is why the
 * caller must test the sign and not "did it fail to be 0". */
HLE_FUNC(sceNp, sceNpManagerGetNpId) {
    const uint32_t p = a32(ctx, 0);
    const int64_t r = np::manager_get_np_id(np_state(), p != 0, np_psn_status());
    if (r == 0) {
        uint8_t id[np::NPID_SIZE];
        np_self_npid(id);
        if (vm::range_mapped(p, np::NPID_SIZE)) std::memcpy(vm::ptr(p), id, np::NPID_SIZE);
    }
    LOG_INFO("sceNp", "ManagerGetNpId -> 0x%08x (%s)", static_cast<uint32_t>(r),
             r == 0 ? "signed in" : "OFFLINE: not signed in");
    ret(ctx, r);
}
HLE_FUNC(sceNp, sceNpManagerGetOnlineId) {
    const uint32_t p = a32(ctx, 0);
    if (!g_np.init) return ret(ctx, kNotInit);
    if (!p) return ret(ctx, kInvalidArg);
    /* sceNp.cpp:4002-4012: OFFLINE as a value, then LOGGING_IN/ONLINE only. */
    const int32_t st = np_psn_status();
    if (st == SCE_NP_MANAGER_STATUS_OFFLINE) return ret(ctx, np::E_OFFLINE);
    if (st != np::STATUS_LOGGING_IN && st != SCE_NP_MANAGER_STATUS_ONLINE)
        return ret(ctx, np::E_INVALID_STATE);
    /* SceNpOnlineId: char data[16+1]; char dummy[3] (sceNp.h:1266-1272). */
    std::memset(vm::ptr(p), 0, 20);
    std::memcpy(vm::ptr(p), "mcps3base", 9);
    ret(ctx, 0);
}
HLE_FUNC(sceNp, sceNpManagerGetAccountAge) {
    const uint32_t p = a32(ctx, 0);
    if (!g_np.init) return ret(ctx, kNotInit);
    if (!p) return ret(ctx, kInvalidArg);
    const int32_t st = np_psn_status();
    if (st == SCE_NP_MANAGER_STATUS_OFFLINE) return ret(ctx, np::E_OFFLINE);
    if (st != np::STATUS_LOGGING_IN && st != SCE_NP_MANAGER_STATUS_ONLINE)
        return ret(ctx, np::E_INVALID_STATE);
    vm::write32(p, 18); /* sceNp.cpp:4211 */
    ret(ctx, 0);
}
/* sceNpManagerGetAccountRegion(countryCode, language): BOTH outputs, and a
 * null one is INVALID_ARGUMENT (sceNp.cpp:4149-4183). It used to answer
 * OFFLINE without checking anything and without writing language, so the
 * caller read an uninitialised stack slot. */
HLE_FUNC(sceNp, sceNpManagerGetAccountRegion) {
    const uint32_t cc = a32(ctx, 0), lang = a32(ctx, 1);
    if (!g_np.init) return ret(ctx, kNotInit);
    if (!cc || !lang) return ret(ctx, kInvalidArg);
    const int32_t st = np_psn_status();
    if (st == SCE_NP_MANAGER_STATUS_OFFLINE) return ret(ctx, np::E_OFFLINE);
    if (st != np::STATUS_LOGGING_IN && st != SCE_NP_MANAGER_STATUS_ONLINE)
        return ret(ctx, np::E_INVALID_STATE);
    /* SceNpCountryCode: char data[2]; char term; char padding[1]. */
    if (vm::range_mapped(cc, 4)) std::memset(vm::ptr(cc), 0, 4);
    vm::write32(lang, 0);
    ret(ctx, 0);
}
HLE_FUNC(sceNp, sceNpManagerGetContentRatingFlag) {
    const uint32_t restricted = a32(ctx, 0), age = a32(ctx, 1);
    const np::Result r = np::manager_get_content_rating(np_state(), restricted != 0, age != 0,
                                                        np_psn_status());
    if (r.has_value) {
        if (restricted) vm::write32(restricted, 0);
        if (age) vm::write32(age, 18); /* sceNp.cpp:4244 */
    }
    LOG_INFO("sceNp", "ManagerGetContentRatingFlag -> 0x%08x (restricted 0, age 18)",
             static_cast<uint32_t>(r.code));
    ret(ctx, r.code);
}
HLE_FUNC(sceNp, sceNpManagerGetChatRestrictionFlag) {
    /* piste 43: this must be 0. It was hardcoded to 1, which the title reads as
     * "this local player has chat restrictions", and it then refuses EVERY
     * attempt to start a world -- including loading an existing save -- with
     * "Failed to create game ... Online is disabled on their Sony Entertainment
     * Network account due to chat restrictions. Uncheck the 'Online Game' box
     * in 'More Options' to start an offline game." That dialog has no offline
     * path out of it, so the load never begins. */
    const uint32_t out = a32(ctx, 0);
    const np::Result r = np::manager_get_chat_restriction(np_state(), out != 0, np_psn_status());
    if (r.has_value && out) vm::write32(out, 0);
    LOG_INFO("sceNp", "ManagerGetChatRestrictionFlag -> 0x%08x (0 = unrestricted)",
             static_cast<uint32_t>(r.code));
    ret(ctx, r.code);
}
/* sceNpManagerGetNetworkTime(CellRtcTick* pTick): CellRtcTick is one
 * be_t<u64> tick. RPCS3 returns every early code here as not_an_error,
 * NOT_INITIALIZED included (sceNp.cpp:3963). */
HLE_FUNC(sceNp, sceNpManagerGetNetworkTime) {
    const uint32_t out = a32(ctx, 0);
    const np::Result r = np::manager_get_network_time(np_state(), out != 0, np_psn_status());
    if (r.has_value && out && vm::range_mapped(out, 8)) vm::write64(out, 0);
    LOG_INFO("sceNp", "ManagerGetNetworkTime -> 0x%08x", static_cast<uint32_t>(r.code));
    ret(ctx, r.code);
}
/* sceNpManagerRequestTicket(npId, serviceId, cookie, cookieSize, entitlementId,
 * consumedCount): six arguments, the last two on the stack (sceNp.cpp:4318). */
HLE_FUNC(sceNp, sceNpManagerRequestTicket) {
    const uint32_t npid = a32(ctx, 0), service = a32(ctx, 1), cookie_size = a32(ctx, 3);
    const int64_t r = np::manager_request_ticket(np_state(), npid != 0, service != 0, cookie_size,
                                                 np_psn_status());
    LOG_INFO("sceNp", "ManagerRequestTicket service=%s cookie=%u -> 0x%08x",
             gstr(service, 32).c_str(), cookie_size, static_cast<uint32_t>(r));
    ret(ctx, r);
}
HLE_FUNC(sceNp, sceNpManagerGetTicket) {
    const uint32_t buffer = a32(ctx, 0), size = a32(ctx, 1);
    const np::Result r = np::manager_get_ticket(np_state(), size != 0);
    if (r.has_value && size) vm::write32(size, static_cast<uint32_t>(r.value));
    LOG_INFO("sceNp", "ManagerGetTicket buffer=0x%08x size=0x%08x -> 0x%08x (empty ticket)",
             buffer, size, static_cast<uint32_t>(r.code));
    ret(ctx, r.code);
}
HLE_FUNC(sceNp, sceNpManagerGetTicketParam) {
    const int32_t id = s32(ctx, 0);
    const uint32_t out = a32(ctx, 1);
    const int64_t r = np::manager_get_ticket_param(np_state(), id, out != 0);
    LOG_INFO("sceNp", "ManagerGetTicketParam(%d) -> 0x%08x", id, static_cast<uint32_t>(r));
    ret(ctx, r);
}
HLE_FUNC(sceNp, sceNpScoreInit) {
    /* sceNp.cpp:5456-5475. It takes NO arguments, so the old handler's
     * "write 1 to *r3" was writing through whatever the caller had in r3. */
    const int64_t r = np::score_init(np_state());
    LOG_INFO("sceNp", "ScoreInit -> 0x%08x", static_cast<uint32_t>(r));
    ret(ctx, r);
}
/* ---- sceNpBasic ------------------------------------------------------- */
/* sceNpBasicRegisterHandler / ContextSensitiveHandler: (context, handler, arg).
 * The handler is argument 1; keying a census on argument 0 filed the
 * registration under the SceNpCommunicationId address. */
HLE_FUNC(sceNp, sceNpBasicRegisterHandler) {
    cbwatch::note_register("np-basic", a32(ctx, 1), a32(ctx, 2));
    np::State& s = np_state();
    const uint32_t cp = a32(ctx, 0), cb = a32(ctx, 1), arg = a32(ctx, 2);
    const int64_t r = np::basic_register_handler(s, cp != 0, cb != 0);
    LOG_INFO("sceNp", "BasicRegisterHandler ctx=0x%08x cb=0x%08x arg=0x%08x -> 0x%08x", cp, cb, arg,
             static_cast<uint32_t>(r));
    if (r != 0) return ret(ctx, r);
    g_np.basic_cb = cb;
    g_np.basic_cb_arg = arg;
    s.basic_handler = cb;
    s.basic_arg = arg;
    np_deliver_basic_presence_end();
    ret(ctx, 0);
}
HLE_FUNC(sceNp, sceNpBasicRegisterContextSensitiveHandler) {
    cbwatch::note_register("np-basic", a32(ctx, 1), a32(ctx, 2));
    np::State& s = np_state();
    const uint32_t cp = a32(ctx, 0), cb = a32(ctx, 1), arg = a32(ctx, 2);
    const int64_t r = np::basic_register_handler(s, cp != 0, cb != 0);
    LOG_INFO("sceNp", "BasicRegisterContextSensitiveHandler ctx=0x%08x cb=0x%08x -> 0x%08x", cp, cb,
             static_cast<uint32_t>(r));
    if (r != 0) return ret(ctx, r);
    g_np.basic_cb = cb;
    g_np.basic_cb_arg = arg;
    s.basic_handler = cb;
    s.basic_arg = arg;
    np_deliver_basic_presence_end();
    ret(ctx, 0);
}
HLE_FUNC(sceNp, sceNpBasicUnregisterHandler) {
    const int64_t r = np::basic_unregister_handler(np_state());
    LOG_INFO("sceNp", "BasicUnregisterHandler -> 0x%08x", static_cast<uint32_t>(r));
    if (r == 0) {
        g_np.basic_cb = 0;
        g_np.basic_cb_arg = 0;
    }
    ret(ctx, r);
}
/* sceNpBasicGetEvent(event, from, data, size): an EMPTY queue is
 * not_an_error(SCE_NP_BASIC_ERROR_NO_EVENT = 0x8002a66a), which is how a title
 * tells "nothing pending" from a real error (np_handler.cpp:1144-1148).
 * This module used to return 0x8002a66b -- EXCEEDS_MAX -- from every poll. */
HLE_FUNC(sceNp, sceNpBasicGetEvent) {
    const uint32_t event = a32(ctx, 0), from = a32(ctx, 1);
    const uint32_t data = a32(ctx, 2), size = a32(ctx, 3);
    const np::Result r = np::basic_get_event(np_state(), event != 0, from != 0, data != 0,
                                             size != 0, true);
    static std::atomic<int> seen{0};
    const int n = seen.fetch_add(1);
    if (n < 4 || (n % 5000) == 0)
        LOG_INFO("sceNp", "BasicGetEvent #%d -> 0x%08x (no event queued)",
                 n, static_cast<uint32_t>(r.code));
    ret(ctx, r.code);
}
/* sceNpBasicSetPresence(data, size): local, no online gate (sceNp.cpp:999). */
HLE_FUNC(sceNp, sceNpBasicSetPresence) {
    const uint32_t data = a32(ctx, 0), size = a32(ctx, 1);
    const int64_t r = np::basic_set_presence(np_state(), size);
    LOG_INFO("sceNp", "BasicSetPresence data=0x%08x size=%u -> 0x%08x", data, size,
             static_cast<uint32_t>(r));
    ret(ctx, r);
}
/* sceNpBasicSetPresenceDetails2(pres, options), sceNp.cpp:1076-1124: options
 * must be 1..3, struct_size must match, and the payload is capped at 128. */
HLE_FUNC(sceNp, sceNpBasicSetPresenceDetails2) {
    const uint32_t pres = a32(ctx, 0), options = a32(ctx, 1);
    np::State& s = np_state();
    if (!g_np.init) return ret(ctx, np::E_BASIC_NOT_INITIALIZED);
    if (!s.basic_handler_registered) return ret(ctx, np::E_BASIC_NOT_REGISTERED);
    if (!pres || options < 1 || options > 3) return ret(ctx, np::E_BASIC_INVALID_ARGUMENT);
    const uint32_t struct_size = vm::read32(pres);
    const uint32_t size = vm::read32(pres + 0x208);
    LOG_INFO("sceNp", "BasicSetPresenceDetails2 options=0x%x struct_size=0x%x size=%u",
             options, struct_size, size);
    if (size > np::MAX_PRESENCE_SIZE) return ret(ctx, np::E_BASIC_EXCEEDS_MAX);
    ret(ctx, 0);
}
/* _sceNpBasicSendMessage, sceNp.cpp:1150-1215: offline it is
 * SCE_NP_BASIC_ERROR_NOT_CONNECTED as a non-error value (sceNp.cpp:1180). */
HLE_FUNC(sceNp, sceNpBasicSendMessage) {
    const uint32_t to = a32(ctx, 0), data = a32(ctx, 1), size = a32(ctx, 2);
    uint8_t peer[np::NPID_SIZE];
    const bool have_to = np_read_npid(to, peer) && peer[0] != 0;
    const int64_t r = np::basic_send_message(np_state(), have_to, size, np_psn_status());
    LOG_INFO("sceNp", "BasicSendMessage to=0x%08x data=0x%08x size=%u -> 0x%08x", to, data, size,
             static_cast<uint32_t>(r));
    ret(ctx, r);
}
/* sceNpBasicAddFriend(contact, body, containerId), sceNp.cpp:1750-1767: it
 * builds a message with mainType ADD_FRIEND and size 0, and RPCS3's own
 * send-message composition then rejects a zero size (sceNp.cpp:1306). */
HLE_FUNC(sceNp, sceNpBasicAddFriend) {
    const uint32_t contact = a32(ctx, 0), body = a32(ctx, 1), container = a32(ctx, 2);
    uint8_t peer[np::NPID_SIZE];
    const bool have = np_read_npid(contact, peer) && peer[0] != 0;
    const int64_t r = np::basic_add_friend(np_state(), have);
    LOG_INFO("sceNp", "BasicAddFriend contact=0x%08x body=%s container=%d -> 0x%08x",
             contact, gstr(body, 64).c_str(), static_cast<int32_t>(container),
             static_cast<uint32_t>(r));
    ret(ctx, r);
}
/* sceNpBasicAddPlayersHistoryAsync(npids, count, description, reqId): the
 * history is a LOCAL object, so this succeeds without a network and hands back
 * a request id (sceNp.cpp:1956-2003, np_handler.cpp:1588-1618). */
HLE_FUNC(sceNp, sceNpBasicAddPlayersHistoryAsync) {
    const uint32_t npids = a32(ctx, 0), count = a32(ctx, 1);
    const uint32_t description = a32(ctx, 2), req_id = a32(ctx, 3);
    const bool have = npids != 0;
    bool empty_entry = false;
    if (have && count > 0 && count <= np::PLAYER_HISTORY_MAX) {
        for (uint32_t i = 0; i < count; i++) {
            const uint32_t ea = npids + i * np::NPID_SIZE;
            if (!vm::range_mapped(ea, np::NPID_SIZE)) { empty_entry = true; break; }
            if (vm::ptr(ea)[0] == 0) { empty_entry = true; break; }
        }
    }
    const bool long_desc = description != 0 &&
                           gstr(description, np::DESCRIPTION_CHAR_MAX + 2).size() >
                               np::DESCRIPTION_CHAR_MAX;
    np::Result r = np::basic_add_players_history(np_state(), count, have, empty_entry, long_desc);
    if (r.has_value && req_id) vm::write32(req_id, static_cast<uint32_t>(r.value));
    LOG_INFO("sceNp", "BasicAddPlayersHistoryAsync count=%u desc=0x%08x reqId=0x%08x -> 0x%08x",
             count, description, req_id, static_cast<uint32_t>(r.code));
    ret(ctx, r.code);
}
/* sceNpBasicGetFriendPresenceByNpId(npid, pres, options): the argument checks
 * come before NP init here, and the lookup needs a PSN session
 * (sceNp.cpp:1880-1903, np_handler.cpp:1835-1847). */
HLE_FUNC(sceNp, sceNpBasicGetFriendPresenceByNpId) {
    const uint32_t npid = a32(ctx, 0), pres = a32(ctx, 1), options = a32(ctx, 2);
    uint8_t peer[np::NPID_SIZE];
    const bool have = np_read_npid(npid, peer);
    const int64_t r = np::basic_get_friend_presence(np_state(), pres != 0, have);
    LOG_INFO("sceNp", "BasicGetFriendPresenceByNpId npid=0x%08x pres=0x%08x opt=0x%x -> 0x%08x",
             npid, pres, options, static_cast<uint32_t>(r));
    ret(ctx, r);
}

/* ---- sceNpSignaling --------------------------------------------------- */
HLE_FUNC(sceNp, sceNpSignalingCreateCtx) {
    const uint32_t npid = a32(ctx, 0), handler = a32(ctx, 1);
    const uint32_t arg = a32(ctx, 2), out = a32(ctx, 3);
    np::State& s = np_state();
    np::Result r = np::signaling_create_ctx(s, npid != 0, out != 0);
    if (r.code == 0) {
        for (auto& c : s.sigs) {
            if (c.id != static_cast<uint32_t>(r.value)) continue;
            c.handler = handler;
            c.arg = arg;
        }
        if (out) vm::write32(out, static_cast<uint32_t>(r.value));
    }
    LOG_INFO("sceNp", "SignalingCreateCtx npid=0x%08x handler=0x%08x arg=0x%08x -> 0x%08x (ctx %d)",
             npid, handler, arg, static_cast<uint32_t>(r.code), r.value);
    ret(ctx, r.code);
}
HLE_FUNC(sceNp, sceNpSignalingDestroyCtx) {
    const uint32_t id = a32(ctx, 0);
    const int64_t r = np::signaling_destroy_ctx(np_state(), id);
    LOG_INFO("sceNp", "SignalingDestroyCtx(%u) -> 0x%08x", id, static_cast<uint32_t>(r));
    ret(ctx, r);
}
HLE_FUNC(sceNp, sceNpSignalingAddExtendedHandler) {
    const uint32_t ctxid = a32(ctx, 0), handler = a32(ctx, 1), arg = a32(ctx, 2);
    const int64_t r = np::signaling_add_extended_handler(np_state(), ctxid, handler, arg);
    LOG_INFO("sceNp", "SignalingAddExtendedHandler ctx=%u handler=0x%08x arg=0x%08x -> 0x%08x",
             ctxid, handler, arg, static_cast<uint32_t>(r));
    ret(ctx, r);
}
HLE_FUNC(sceNp, sceNpSignalingActivateConnection) {
    const uint32_t ctxid = a32(ctx, 0), npid = a32(ctx, 1), out = a32(ctx, 2);
    uint8_t peer[np::NPID_SIZE], self[np::NPID_SIZE];
    np_self_npid(self);
    const bool have = np_read_npid(npid, peer);
    const np::Result r = np::signaling_activate(np_state(), ctxid, peer, self, have, out != 0);
    if (r.has_value && out) vm::write32(out, static_cast<uint32_t>(r.value));
    LOG_INFO("sceNp", "SignalingActivateConnection ctx=%u npid=0x%08x -> 0x%08x (conn %d)",
             ctxid, npid, static_cast<uint32_t>(r.code), r.value);
    ret(ctx, r.code);
}
HLE_FUNC(sceNp, sceNpSignalingTerminateConnection) {
    const uint32_t ctxid = a32(ctx, 0), conn = a32(ctx, 1);
    const int64_t r = np::signaling_terminate(np_state(), conn);
    LOG_INFO("sceNp", "SignalingTerminateConnection ctx=%u conn=%u -> 0x%08x", ctxid, conn,
             static_cast<uint32_t>(r));
    ret(ctx, r);
}
/* sceNpSignalingGetConnectionStatus(ctx, conn, status, peer_addr, peer_port):
 * five arguments, and an unknown connection still writes INACTIVE before it
 * reports CONN_NOT_FOUND (sceNp.cpp:7030-7036). */
HLE_FUNC(sceNp, sceNpSignalingGetConnectionStatus) {
    const uint32_t ctxid = a32(ctx, 0), conn = a32(ctx, 1), status = a32(ctx, 2);
    const uint32_t addr = a32(ctx, 3), port = a32(ctx, 4);
    const np::Result r = np::signaling_get_conn_status(np_state(), conn, status != 0);
    if (r.has_value && status) vm::write32(status, static_cast<uint32_t>(r.value));
    if (r.code == 0) {
        if (addr) vm::write32(addr, 0);
        if (port) vm::write16(port, 0);
    }
    LOG_INFO("sceNp", "SignalingGetConnectionStatus ctx=%u conn=%u -> 0x%08x (status %d)",
             ctxid, conn, static_cast<uint32_t>(r.code), r.value);
    ret(ctx, r.code);
}
HLE_FUNC(sceNp, sceNpSignalingGetConnectionInfo) {
    const uint32_t ctxid = a32(ctx, 0), conn = a32(ctx, 1), code = a32(ctx, 2);
    const uint32_t info = a32(ctx, 3);
    const int64_t r = np::signaling_get_conn_info(np_state(), conn, static_cast<int32_t>(code),
                                                  info != 0);
    LOG_INFO("sceNp", "SignalingGetConnectionInfo ctx=%u conn=%u code=%d info=0x%08x -> 0x%08x",
             ctxid, conn, static_cast<int32_t>(code), info, static_cast<uint32_t>(r));
    ret(ctx, r);
}
HLE_FUNC(sceNp, sceNpSignalingGetConnectionFromNpId) {
    const uint32_t ctxid = a32(ctx, 0), npid = a32(ctx, 1), out = a32(ctx, 2);
    uint8_t peer[np::NPID_SIZE], self[np::NPID_SIZE];
    np_self_npid(self);
    const bool have = np_read_npid(npid, peer);
    const np::Result r = np::signaling_conn_from_npid(np_state(), peer, self, have, out != 0);
    if (r.has_value && out) vm::write32(out, static_cast<uint32_t>(r.value));
    LOG_INFO("sceNp", "SignalingGetConnectionFromNpId ctx=%u -> 0x%08x", ctxid,
             static_cast<uint32_t>(r.code));
    ret(ctx, r.code);
}
HLE_FUNC(sceNp, sceNpSignalingGetConnectionFromPeerAddress) {
    const uint32_t ctxid = a32(ctx, 0), addr = a32(ctx, 1), port = a32(ctx, 2), out = a32(ctx, 3);
    const int64_t r = np::signaling_conn_from_addr(np_state(), out != 0);
    LOG_INFO("sceNp", "SignalingGetConnectionFromPeerAddress ctx=%u addr=0x%08x port=%u -> 0x%08x",
             ctxid, addr, port, static_cast<uint32_t>(r));
    ret(ctx, r);
}
/* ---- sceNpUtil -------------------------------------------------------- */
/* sceNpUtilCmpNpId(id1, id2): equal ids answer CELL_OK, different ids answer
 * SCE_NP_UTIL_ERROR_NOT_MATCH as a non-error value (sceNp.cpp:7301-7314). */
HLE_FUNC(sceNp, sceNpUtilCmpNpId) {
    uint8_t a[np::NPID_SIZE], b[np::NPID_SIZE];
    const bool have_a = np_read_npid(a32(ctx, 0), a);
    const bool have_b = np_read_npid(a32(ctx, 1), b);
    const bool equal = have_a && have_b && np::same_npid(a, b);
    const int64_t r = np::util_cmp_np_id(have_a, have_b, equal);
    LOG_INFO("sceNp", "UtilCmpNpId -> 0x%08x", static_cast<uint32_t>(r));
    ret(ctx, r);
}
/* ---- sceNpDrm --------------------------------------------------------- */
/* sceNpDrmIsAvailable(k_licensee, drm_path), sceNp.cpp:634-753.
 *
 * The reference opens the file and dispatches on its first word: "SCE\0" is an
 * NPDRM self, "NPD\0" an edata/sdata, anything else is treated as unencrypted
 * and ACCEPTED. Both encrypted forms need a licence verified against a key
 * this runtime does not hold, so they answer NO_ENTITLEMENT instead of
 * pretending the file is usable. A missing file is the lv2 open error. */
HLE_FUNC(sceNp, sceNpDrmIsAvailable) {
    const uint32_t k_lic = a32(ctx, 0), path = a32(ctx, 1);
    const std::string guest = gstr(path, 0x100);
    np::DrmVerdict verdict = np::DrmVerdict::Missing;
    uint32_t first_word = 0;
    if (!guest.empty()) {
        const std::string host = vfs::host_path(guest);
        FILE* f = host.empty() ? nullptr : std::fopen(host.c_str(), "rb");
        if (f) {
            uint8_t word[4] = {};
            const bool got = std::fread(word, 1, 4, f) == 4;
            std::fclose(f);
            if (got) {
                first_word = (static_cast<uint32_t>(word[0]) << 24) |
                             (static_cast<uint32_t>(word[1]) << 16) |
                             (static_cast<uint32_t>(word[2]) << 8) | word[3];
                verdict = np::drm_verdict_for(first_word);
            }
        }
    }
    const int64_t r = np::drm_is_available(!guest.empty(), verdict);
    LOG_INFO("sceNp", "DrmIsAvailable k_licensee=0x%08x path=%s magic=0x%08x -> 0x%08x", k_lic,
             guest.c_str(), first_word, static_cast<uint32_t>(r));
    ret(ctx, r);
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

/* sceNpCommerce2 lives in runtime/src/hle/sceNpCommerce2.cpp.
 *
 * It was here, as seven handlers driven by NpState's commerce2_* fields, from
 * the loading-43 session that proved the module had to exist (see
 * docs/loading-43/REPORT.md). It moved out for two reasons:
 *
 *   1. the module has 26 imports, not 7, and they share per-context state
 *      (version, npId, callback, arg) that belongs in one object, not in a
 *      handful of globals next to an unrelated module's;
 *   2. those seven registrations collided with the new ones -- two HLE_FUNC
 *      macros registering the same "sceNpCommerce2::*" key, with the winner
 *      decided by static-initialisation order across translation units.
 *
 * The one thing the new module cannot see from its own file is the NP manager's
 * flag: sceNpCommerce2Init answers SCE_NP_ERROR_NOT_INITIALIZED when the
 * manager is down (refs/rpcs3/rpcs3/Emu/Cell/Modules/sceNpCommerce2.cpp:138-139
 * tests nph.is_NP_init). This is that flag, read-only.
 */
namespace commerce2 {
bool np_manager_initialised() { return g_np.init; }
}  // namespace commerce2
}
