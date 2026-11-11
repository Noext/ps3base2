#include "common.h"

#include <atomic>
#include <cstdlib>

#include "netctl_hle.h"
#include "runtime/cbwatch.h"
#include "runtime/sysutil_events.h"

/* cellNetCtl -- the network-state module, written against the reference.
 *
 * The nine functions this title (BLES00680) imports from the module are all
 * here. Signatures and return codes:
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellNetCtl.cpp
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellNetCtl.h
 * and the decision logic lives in netctl_hle.h so it can be unit tested with no
 * game, no GPU and no network.
 *
 * The state this runtime reports is the console's with no link: Disconnected,
 * which is also RPCS3's default (np_handler::get_net_status,
 * refs/rpcs3/rpcs3/Emu/NP/np_handler.cpp:867-870, with is_connected false
 * unless its network is enabled). Consequently GetInfo answers NOT_CONNECTED
 * for everything except ETHER_ADDR -- which RPCS3 answers BEFORE its connection
 * test (cellNetCtl.cpp:247-256) and so must succeed offline -- and GetNatInfo
 * answers successfully, because that function never tests the connection at all
 * (cellNetCtl.cpp:377-403).
 *
 * PS3BASE_NET_CONNECTED=1 selects the connected answers, the same switch this
 * tree already used, kept so the online branch stays measurable.
 */

namespace hle {

namespace {

struct NetCtlState {
    bool init = false;
    uint32_t handler = 0;
    uint32_t handler_arg = 0;
};

NetCtlState g_netctl;

/* RPCS3 decides this from its network configuration; the environment is this
 * runtime's equivalent, and offline is the default. */
bool netctl_connected() {
    static const bool on = std::getenv("PS3BASE_NET_CONNECTED") != nullptr;
    return on;
}

netctl::Env netctl_env() {
    netctl::Env env;
    return env;
}

/* Read the caller's structure only when it is mapped: a title that passes a
 * pointer into an unmapped region must get the module's error code, not a host
 * fault inside the handler. */
bool read_be32(uint32_t addr, uint32_t& out) {
    if (!addr || !vm::range_mapped(addr, 4)) return false;
    out = vm::read32(addr);
    return true;
}

}  // namespace

/* The connect-to-PSN dialog raises three system commands through the sysutil
 * callback queue: LOADED, FINISHED, then UNLOADED after the result is read
 * (cellNetCtl.cpp:299-301, :373). Delivery goes through hle::sysutil_post,
 * drained by cellSysutilCheckCallback -- the thread a real firmware uses. */
void netctl_send_sysutil(uint64_t event) {
    for (int slot = 0; slot < 4; slot++) {
        const uint32_t fn = callback_function(slot);
        if (!fn) continue;
        const uint32_t arg = callback_arg(slot);
        sysutil_post([fn, arg, event](ppu::Context& c) {
            /* CellSysutilCallback = void(u64 status, u64 param, void* userdata):
             * the EVENT is the first argument and the caller's pointer THIRD. */
            call_guest(c, fn, event, 0, arg);
        });
    }
}

/* The transition a registered handler receives when the interface comes up.
 * CellNetCtlHandler = void(s32 prev_state, s32 new_state, s32 event,
 *                           s32 error_code, void* arg)
 * -- five arguments, with the caller's pointer LAST (cellNetCtl.h:291). */
void netctl_deliver_establish() {
    if (!g_netctl.handler) return;
    const uint32_t h = g_netctl.handler, arg = g_netctl.handler_arg;
    sysutil_post([h, arg](ppu::Context& c) {
        call_guest(c, h, netctl::STATE_DISCONNECTED /* prev */,
                   netctl::STATE_IPOBTAINED /* new */,
                   1 /* CELL_NET_CTL_EVENT_ESTABLISH, cellNetCtl.h:68 */, 0, arg);
    });
    LOG_INFO("cellNetCtl", "handler 0x%08x <- ESTABLISH 0->3, arg 0x%08x", h, arg);
}

/* cellNetCtlInit(void) -> s32. cellNetCtl.cpp:108-122.
 * A second init is CELL_NET_CTL_ERROR_NOT_TERMINATED, not CELL_OK. */
HLE_FUNC(cellNetCtl, cellNetCtlInit) {
    const int32_t rc = netctl::init(g_netctl.init);
    if (rc == 0) g_netctl.init = true;
    LOG_INFO("cellNetCtl", "Init() -> 0x%08x", static_cast<uint32_t>(rc));
    ret(ctx, rc);
}

/* cellNetCtlTerm(void) -> void. cellNetCtl.cpp:124-131: no return value, and
 * the init flag is simply cleared. The handlers go with it: a handler that
 * outlived its module would be called into freed code. */
HLE_FUNC(cellNetCtl, cellNetCtlTerm) {
    g_netctl.init = false;
    g_netctl.handler = 0;
    g_netctl.handler_arg = 0;
    LOG_INFO("cellNetCtl", "Term()");
}

/* cellNetCtlGetState(s32* state) -> s32. cellNetCtl.cpp:133-152. */
HLE_FUNC(cellNetCtl, cellNetCtlGetState) {
    const uint32_t state = a32(ctx, 0);
    const int32_t rc = netctl::get_state(g_netctl.init, state == 0, netctl_connected());
    if (rc == 0) vm::write32(state, static_cast<uint32_t>(netctl::net_state(netctl_connected())));
    /* A handful of calls is the whole story; 17k identical lines tell nothing. */
    static std::atomic<int> seen{0};
    const int n = seen.fetch_add(1);
    if (n < 6 || (n % 20000) == 0)
        LOG_INFO("cellNetCtl", "GetState #%d -> %s (rc 0x%08x)", n,
                 netctl_connected() ? "IPObtained" : "Disconnected", static_cast<uint32_t>(rc));
    ret(ctx, rc);
}

/* cellNetCtlAddHandler(cellNetCtlHandler handler, void* arg, s32* hid) -> s32.
 * cellNetCtl.cpp:154-171.
 *
 * RPCS3's version is a todo stub: it checks init and *hid and returns CELL_OK
 * WITHOUT writing an id. A title that then calls DelHandler(*hid) would pass an
 * uninitialised value, which RPCS3's own check (hid > 3, :184) can reject on a
 * value we never chose. The console allocates an id, so an id is written here:
 * slot 0, the first of the ids DelHandler accepts. */
HLE_FUNC(cellNetCtl, cellNetCtlAddHandler) {
    cbwatch::note_register("netctl-handler", a32(ctx, 0), a32(ctx, 1));
    const uint32_t hid = a32(ctx, 2);
    const int32_t rc = netctl::add_handler(g_netctl.init, hid == 0);
    if (rc == 0) {
        g_netctl.handler = a32(ctx, 0);
        g_netctl.handler_arg = a32(ctx, 1);
        vm::write32(hid, static_cast<uint32_t>(netctl::HANDLER_ID));
        /* A transition is delivered only when the interface actually has one to
         * report; a Disconnected interface never raises ESTABLISH. */
        if (netctl_connected()) netctl_deliver_establish();
    }
    LOG_INFO("cellNetCtl", "AddHandler handler=0x%08x arg=0x%08x -> hid %d (rc 0x%08x)",
             a32(ctx, 0), a32(ctx, 1), netctl::HANDLER_ID, static_cast<uint32_t>(rc));
    ret(ctx, rc);
}

/* cellNetCtlDelHandler(s32 hid) -> s32. cellNetCtl.cpp:173-190.
 * The module refuses only ids GREATER than 3. */
HLE_FUNC(cellNetCtl, cellNetCtlDelHandler) {
    const int32_t hid = s32(ctx, 0);
    const int32_t rc = netctl::del_handler(g_netctl.init, hid);
    if (rc == 0 && static_cast<uint32_t>(hid) == static_cast<uint32_t>(netctl::HANDLER_ID)) {
        g_netctl.handler = 0;
        g_netctl.handler_arg = 0;
    }
    LOG_INFO("cellNetCtl", "DelHandler(%d) -> 0x%08x", hid, static_cast<uint32_t>(rc));
    ret(ctx, rc);
}

/* cellNetCtlGetInfo(s32 code, CellNetCtlInfo* info) -> s32.
 * cellNetCtl.cpp:192-289. The CellNetCtlInfo union's largest member is
 * CELL_NET_CTL_HOSTNAME_LEN = 256 bytes (cellNetCtl.h:256, :264), and the
 * reference writes exactly one member per code (:258-286) -- never a blanket
 * clear of the caller's union. */
HLE_FUNC(cellNetCtl, cellNetCtlGetInfo) {
    const int32_t code = s32(ctx, 0);
    const uint32_t info = a32(ctx, 1);

    /* A non-null pointer that is not mapped cannot be written without faulting
     * on the host; RPCS3 would fault here too. Refused with the module's own
     * address error instead, so the caller sees a return code. */
    const bool unusable = !info || !vm::range_mapped(info, netctl::INFO_MAX);

    netctl::InfoOut out;
    const int32_t rc = netctl::get_info(g_netctl.init, unusable, netctl_connected(), code,
                                        netctl_env(), out);
    if (rc == 0 && out.size) {
        std::memcpy(vm::ptr(info), out.bytes, out.size);
    }
    LOG_INFO("cellNetCtl", "GetInfo(code=0x%x) -> 0x%08x, %u byte(s)",
             static_cast<uint32_t>(code), static_cast<uint32_t>(rc), out.size);
    ret(ctx, rc);
}

/* cellNetCtlGetNatInfo(CellNetCtlNatInfo* natInfo) -> s32.
 * cellNetCtl.cpp:377-403. The caller's size field selects 16 or 20 bytes
 * (:393) and is written back unchanged; nat_type/stun_status/upnp_status are
 * then filled (:398-400). There is NO connection test: this answers offline. */
HLE_FUNC(cellNetCtl, cellNetCtlGetNatInfo) {
    const uint32_t nat = a32(ctx, 0);
    uint32_t size = 0;
    const bool readable = read_be32(nat, size);
    /* A null or unmapped pointer is the module's INVALID_ADDR; a readable but
     * wrong size is INVALID_SIZE. Both are checked by get_nat_info, in RPCS3's
     * order (:388-396). */
    netctl::NatInfoOut out;
    const int32_t rc = netctl::get_nat_info(g_netctl.init, !readable, size, netctl_env(), out);
    if (rc == 0) {
        /* CellNetCtlNatInfo: be_t<u32> size, be_t<s32> upnp_status,
         * be_t<s32> stun_status, be_t<s32> nat_type, be_t<u32> mapped_addr
         * (cellNetCtl.h:282-289). Only the declared size is written. */
        vm::write32(nat + 0, out.size);
        vm::write32(nat + 4, static_cast<uint32_t>(out.upnp_status));
        vm::write32(nat + 8, static_cast<uint32_t>(out.stun_status));
        vm::write32(nat + 12, static_cast<uint32_t>(out.nat_type));
        if (out.size >= netctl::NATINFO_SIZE_MAX) vm::write32(nat + 16, out.mapped_addr);
    }
    LOG_INFO("cellNetCtl", "GetNatInfo(size=%u) -> 0x%08x", size, static_cast<uint32_t>(rc));
    ret(ctx, rc);
}

/* cellNetCtlNetStartDialogLoadAsync(const CellNetCtlNetStartDialogParam* param)
 * -> s32. cellNetCtl.cpp:304-334.
 *
 * RPCS3 validates init, the pointer, param->type (< 2, :320) and param->size
 * (== 12, :325), then runs netstart_hack (:291-302): half a second later the
 * sysutil callbacks receive LOADED and FINISHED. The delay is what lets a title
 * register its callback first, and dropping it is the documented reason that
 * hack exists (:330). It is kept here as a queued pair of events: this runtime
 * has no timer thread for it, and the queue is drained by
 * cellSysutilCheckCallback on the guest thread, so a title that registers its
 * callback after this call still receives both. */
HLE_FUNC(cellNetCtl, cellNetCtlNetStartDialogLoadAsync) {
    const uint32_t param = a32(ctx, 0);
    uint32_t size = 0, type = 0;
    const bool readable = param && vm::range_mapped(param, netctl::NETSTART_PARAM_SIZE) &&
                          read_be32(param + 0, size) && read_be32(param + 4, type);
    const int32_t rc = netctl::netstart_load_check(
        g_netctl.init, !readable, size, static_cast<int32_t>(type));
    if (rc != 0) {
        LOG_WARN("cellNetCtl",
                 "NetStartDialogLoadAsync(param=0x%08x size=%u type=%d) -> 0x%08x",
                 param, size, static_cast<int32_t>(type), static_cast<uint32_t>(rc));
        return ret(ctx, rc);
    }
    netctl_send_sysutil(netctl::SYSUTIL_NET_CTL_NETSTART_LOADED);
    netctl_send_sysutil(netctl::SYSUTIL_NET_CTL_NETSTART_FINISHED);
    LOG_INFO("cellNetCtl", "NetStartDialogLoadAsync size=%u type=%u -> LOADED + FINISHED",
             size, type);
    ret(ctx, rc);
}

/* cellNetCtlNetStartDialogAbortAsync(void) -> s32. cellNetCtl.cpp:336-348:
 * the init check, then CELL_OK. */
HLE_FUNC(cellNetCtl, cellNetCtlNetStartDialogAbortAsync) {
    const int32_t rc = netctl::netstart_abort(g_netctl.init);
    LOG_INFO("cellNetCtl", "NetStartDialogAbortAsync() -> 0x%08x", static_cast<uint32_t>(rc));
    ret(ctx, rc);
}

/* cellNetCtlNetStartDialogUnloadAsync(CellNetCtlNetStartDialogResult* result)
 * -> s32. cellNetCtl.cpp:350-375.
 *
 * The size field is READ and validated against 8 (:366), never rewritten. The
 * result is 0 when the interface obtained an IP and CELL_NET_CTL_ERROR_DIALOG_CANCELED
 * otherwise (:371) -- the "continue playing offline" path -- followed by the
 * UNLOADED system command (:373). */
HLE_FUNC(cellNetCtl, cellNetCtlNetStartDialogUnloadAsync) {
    const uint32_t result = a32(ctx, 0);
    uint32_t size = 0;
    const bool readable = read_be32(result, size);
    netctl::DialogResultOut out;
    const int32_t rc = netctl::netstart_unload(g_netctl.init, !readable, size,
                                               netctl_connected(), out);
    if (rc == 0) {
        /* CellNetCtlNetStartDialogResult: be_t<u32> size, be_t<s32> result
         * (cellNetCtl.h:276-280). */
        vm::write32(result + 4, static_cast<uint32_t>(out.result));
        netctl_send_sysutil(netctl::SYSUTIL_NET_CTL_NETSTART_UNLOADED);
    }
    LOG_INFO("cellNetCtl", "NetStartDialogUnloadAsync(size=%u) -> 0x%08x, result 0x%08x",
             size, static_cast<uint32_t>(rc), static_cast<uint32_t>(out.result));
    ret(ctx, rc);
}

}  // namespace hle
