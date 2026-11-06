#include "common.h"
#include <cstdlib>
#include "runtime/sysutil_events.h"

#include <cstring>

namespace hle {

namespace {

constexpr int32_t SCE_NP_MANAGER_STATUS_OFFLINE = -1;
constexpr int32_t CELL_NET_CTL_STATE_Disconnected = 0;
constexpr int64_t SCE_NP_ERROR_NOT_INITIALIZED = static_cast<int32_t>(0x8002aa01);
constexpr int64_t CELL_NET_CTL_ERROR_NOT_CONNECTED = static_cast<int32_t>(0x80130106);

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

HLE_FUNC(cellNetCtl, cellNetCtlInit) { ret(ctx, 0); }
HLE_FUNC(cellNetCtl, cellNetCtlTerm) { ret(ctx, 0); }
HLE_FUNC(cellNetCtl, cellNetCtlAddHandler) {
    if (uint32_t id = a32(ctx, 2)) vm::write32(id, 1);
    ret(ctx, 0);
}
HLE_FUNC(cellNetCtl, cellNetCtlDelHandler) { ret(ctx, 0); }
HLE_FUNC(cellNetCtl, cellNetCtlGetState) {
    LOG_INFO("cellNetCtl", "GetState called");
    if (uint32_t p = a32(ctx, 0)) vm::write32(p, CELL_NET_CTL_STATE_Disconnected);
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
        vm::write32(result + 4, static_cast<uint32_t>(CELL_NET_CTL_ERROR_DIALOG_CANCELED));
    }
    sysutil_send(CELL_SYSUTIL_NET_CTL_NETSTART_UNLOADED);
    LOG_INFO("cellNetCtl", "NetStartDialogUnloadAsync -> CANCELED + UNLOADED");
    ret(ctx, 0);
}
HLE_FUNC(cellNetCtl, cellNetCtlGetNatInfo) { ret(ctx, CELL_NET_CTL_ERROR_NOT_CONNECTED); }

HLE_FUNC(sceNp2, sceNp2Init) { ret(ctx, 0); }
HLE_FUNC(sceNp2, sceNp2Term) { ret(ctx, 0); }
/* PS3BASE_NP_LOG: report every sceNp call, to find which one the title's
 * "you are offline" dialogue keyed on. */
#define NP_ENTER(name)                                                        \
    do {                                                                      \
        static const bool on = std::getenv("PS3BASE_NP_LOG") != nullptr;      \
        if (on) LOG_INFO("sceNp", name " r3=0x%x r4=0x%x", a32(ctx, 0), a32(ctx, 1)); \
    } while (0)

HLE_FUNC(sceNp, sceNpInit) { NP_ENTER("sceNpInit"); ret(ctx, 0); }
HLE_FUNC(sceNp, sceNpTerm) { ret(ctx, 0); }
HLE_FUNC(sceNp, sceNpLookupInit) { ret(ctx, 0); }
HLE_FUNC(sceNp, sceNpManagerRegisterCallback) { ret(ctx, 0); }
HLE_FUNC(sceNp, sceNpManagerUnregisterCallback) { ret(ctx, 0); }
HLE_FUNC(sceNp, sceNpBasicRegisterContextSensitiveHandler) { ret(ctx, 0); }
HLE_FUNC(sceNp, sceNpBasicRegisterHandler) { ret(ctx, 0); }
HLE_FUNC(sceNp, sceNpManagerGetStatus) {
    if (uint32_t p = a32(ctx, 0)) vm::write32(p, static_cast<uint32_t>(SCE_NP_MANAGER_STATUS_OFFLINE));
    ret(ctx, 0);
}
HLE_FUNC(sceNp, sceNpManagerGetNpId) { ret(ctx, static_cast<int32_t>(0x8002aa0c)); }
HLE_FUNC(sceNp, sceNpManagerGetOnlineId) { ret(ctx, static_cast<int32_t>(0x8002aa0c)); }
HLE_FUNC(sceNp, sceNpManagerGetAccountRegion) { ret(ctx, static_cast<int32_t>(0x8002aa0c)); }
HLE_FUNC(sceNp, sceNpManagerGetTicket) { ret(ctx, static_cast<int32_t>(0x8002aa0c)); }
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
