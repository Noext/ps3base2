/* ps3base -- cellNetCtl, tested against the reference.
 *
 * Two halves, both runnable with no game, no GPU and no network:
 *
 *   1. the decision table (hle/netctl_hle.h), which is where every rule of
 *      refs/rpcs3/rpcs3/Emu/Cell/Modules/cellNetCtl.cpp lives;
 *   2. the nine handlers themselves, included and called with a real guest
 *      context and real guest memory, so the return code that actually lands in
 *      r3 and the bytes that actually land in the caller's structure are what
 *      is checked.
 *
 * The properties here are the ones this tree got wrong before:
 *   a. GetInfo must answer CELL_NET_CTL_ERROR_NOT_CONNECTED offline for every
 *      code EXCEPT ETHER_ADDR, which RPCS3 answers before its connection test
 *      (cellNetCtl.cpp:247-256). The old handler returned NOT_CONNECTED for
 *      everything, with the wrong code number (0x80130106 is INVALID_CODE;
 *      NOT_CONNECTED is 0x80130108).
 *   b. GetNatInfo must SUCCEED offline. RPCS3 never tests the connection there
 *      (cellNetCtl.cpp:377-403); the old handler refused with NOT_CONNECTED.
 *   c. Init twice is NOT_TERMINATED, not CELL_OK.
 *   d. DelHandler accepts a NEGATIVE id (the only check is hid > 3).
 *   e. the size fields are validated against the structures' real sizes
 *      (12, 8, 16/20) and the caller's value is never rewritten.
 *   f. an unknown GetInfo code writes nothing and still returns CELL_OK.
 */
#include "runtime/memory.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "../runtime/src/hle/cellNetCtl.cpp"

namespace {
/* hle::Reg normally registers into the import table (ppu_runtime.cpp:81-83).
 * This test calls the handlers directly, so the constructor records the key in
 * a local map instead of pulling in the whole PPU runtime. A map, not a set,
 * so the registration NAME is checked too: the dispatcher builds
 * "module::name" and looks it up, and a typo there makes the import resolve
 * nowhere while the handler compiles. Function-local for the same reason as
 * tests/cellgameexec.cpp: the static hle::Reg objects inside the included
 * module run before any namespace-scope object declared after it. */
std::map<std::string, hle::Fn>& test_registry() {
    static std::map<std::string, hle::Fn> m;
    return m;
}
}

namespace hle {
Reg::Reg(const char* module, const char* name, Fn fn) {
    test_registry()[std::string(module) + "::" + name] = fn;
}
}

/* The services the module reaches for outside guest memory: the sysutil
 * callback queue (runtime/include/runtime/sysutil_events.h), the callback
 * census (runtime/include/runtime/cbwatch.h) and the guest-callback trampoline
 * (common.h:43-44). Test-local versions keep this suite down to the memory map,
 * and they make the system commands the dialog raises OBSERVABLE rather than
 * assumed -- which is the whole point of testing the load/unload pair. */
namespace {
/* One slot is registered (callback_function below returns non-zero for slot 0
 * only), so each system command raised means exactly one sysutil_post. Counting
 * those is how "the dialog raised LOADED then FINISHED, and UNLOADED on unload"
 * is measured rather than described. */
int g_posted = 0;
}

namespace hle {
uint32_t callback_function(int slot) { return slot == 0 ? 0x00100000u : 0u; }
uint32_t callback_arg(int) { return 0x42424242u; }
void sysutil_post(std::function<void(ppu::Context&)> fn) {
    /* The queued lambda would need a guest to call into; what this test checks
     * is that the command was RAISED, once per event. */
    (void)fn;
    g_posted++;
}
void call_guest(ppu::Context&, uint32_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {}
}

namespace cbwatch {
void note_register(const char*, uint32_t, uint32_t) {}
}

static int fails = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; std::fprintf(stderr, "[FAIL] %s\n", msg); } \
} while (0)

namespace {

uint32_t g_next = 0x20000000;

uint32_t alloc_area(uint32_t size = 0x1000) {
    const uint32_t a = g_next;
    if (!vm::map(a, size)) return 0;
    g_next += size;
    return a;
}

/* Call a handler the way the import path does: r3, r4, ... are the arguments
 * (runtime/src/hle/common.h:13-16). */
int64_t call(ppu::Context& ctx, void (*fn)(ppu::Context&, uint8_t*),
             uint64_t a0 = 0, uint64_t a1 = 0, uint64_t a2 = 0) {
    ctx = ppu::Context{};
    ctx.gpr[3] = a0;
    ctx.gpr[4] = a1;
    ctx.gpr[5] = a2;
    fn(ctx, vm::g_base);
    return static_cast<int64_t>(ctx.gpr[3]);
}

int32_t low32(int64_t v) { return static_cast<int32_t>(v & 0xFFFFFFFFll); }

namespace nc = hle::netctl;

}  // namespace

int main() {
    if (!vm::init()) {
        std::fprintf(stderr, "vm::init failed\n");
        return 1;
    }

    /* ---- 0. the constants are the reference header's -------------------
     * cellNetCtl.h:8-18, :58-61, :147-172, :186-188. */
    CHECK(nc::ERROR_NOT_INITIALIZED == static_cast<int32_t>(0x80130101), "NOT_INITIALIZED is 0x80130101");
    CHECK(nc::ERROR_NOT_TERMINATED  == static_cast<int32_t>(0x80130102), "NOT_TERMINATED is 0x80130102");
    CHECK(nc::ERROR_INVALID_ID      == static_cast<int32_t>(0x80130105), "INVALID_ID is 0x80130105");
    CHECK(nc::ERROR_INVALID_CODE    == static_cast<int32_t>(0x80130106), "INVALID_CODE is 0x80130106");
    CHECK(nc::ERROR_INVALID_ADDR    == static_cast<int32_t>(0x80130107), "INVALID_ADDR is 0x80130107");
    CHECK(nc::ERROR_NOT_CONNECTED   == static_cast<int32_t>(0x80130108), "NOT_CONNECTED is 0x80130108, not 0x80130106");
    CHECK(nc::ERROR_INVALID_TYPE    == static_cast<int32_t>(0x8013010a), "INVALID_TYPE is 0x8013010a");
    CHECK(nc::ERROR_INVALID_SIZE    == static_cast<int32_t>(0x8013010b), "INVALID_SIZE is 0x8013010b");
    CHECK(nc::ERROR_DIALOG_CANCELED == static_cast<int32_t>(0x80130190), "DIALOG_CANCELED is 0x80130190");
    CHECK(nc::STATE_DISCONNECTED == 0 && nc::STATE_IPOBTAINED == 3, "states are 0 and 3");
    CHECK(nc::INFO_ETHER_ADDR == 2 && nc::INFO_IP_ADDRESS == 16, "info codes 2 and 16");
    CHECK(nc::INFO_UPNP_CONFIG == 24, "INFO_UPNP_CONFIG is 24");
    CHECK(nc::SYSUTIL_NET_CTL_NETSTART_LOADED == 0x0801 &&
          nc::SYSUTIL_NET_CTL_NETSTART_FINISHED == 0x0802 &&
          nc::SYSUTIL_NET_CTL_NETSTART_UNLOADED == 0x0803,
          "the three sysutil net-start events");
    /* The UPnP enum is INVERTED: ON is 0, OFF is 1 (cellNetCtl.h:138-142). */
    CHECK(nc::UPNP_ON == 0 && nc::UPNP_OFF == 1, "CELL_NET_CTL_UPNP_ON is 0 and OFF is 1");
    /* Sizes straight from the structures (cellNetCtl.h:269-289). */
    CHECK(nc::NETSTART_PARAM_SIZE == 12, "CellNetCtlNetStartDialogParam is 12 bytes");
    CHECK(nc::NETSTART_RESULT_SIZE == 8, "CellNetCtlNetStartDialogResult is 8 bytes");
    CHECK(nc::NATINFO_SIZE_MIN == 16 && nc::NATINFO_SIZE_MAX == 20,
          "CellNetCtlNatInfo is 16 or 20 bytes");
    CHECK(nc::INFO_MAX == 256, "the union's largest member is 256 bytes");

    /* ---- 1. the decision table -------------------------------------- */
    /* Init: cellNetCtl.cpp:114-121. */
    CHECK(nc::init(false) == 0, "the first Init is CELL_OK");
    CHECK(nc::init(true) == nc::ERROR_NOT_TERMINATED,
          "a second Init is NOT_TERMINATED, never a second CELL_OK");

    /* GetState: cellNetCtl.cpp:139-147 -- init is checked BEFORE the pointer. */
    CHECK(nc::get_state(false, true) == nc::ERROR_NOT_INITIALIZED,
          "GetState(NULL) before Init is NOT_INITIALIZED, not INVALID_ADDR");
    CHECK(nc::get_state(true, true) == nc::ERROR_INVALID_ADDR,
          "GetState(NULL) after Init is INVALID_ADDR");
    CHECK(nc::get_state(true, false) == 0, "GetState with a valid pointer succeeds offline");
    CHECK(nc::net_state(false) == nc::STATE_DISCONNECTED, "an unconnected interface reports 0");
    CHECK(nc::net_state(true) == nc::STATE_IPOBTAINED, "a connected interface reports 3");

    /* DelHandler: cellNetCtl.cpp:184-187 refuses only hid > 3. */
    CHECK(nc::del_handler(true, 0) == 0, "DelHandler(0) succeeds");
    CHECK(nc::del_handler(true, 3) == 0, "DelHandler(3) succeeds -- 3 is within the range");
    CHECK(nc::del_handler(true, 4) == nc::ERROR_INVALID_ID, "DelHandler(4) is INVALID_ID");
    CHECK(nc::del_handler(true, -1) == 0,
          "DelHandler(-1) SUCCEEDS: the only check is hid > 3, so a negative id is accepted");
    CHECK(nc::del_handler(false, 0) == nc::ERROR_NOT_INITIALIZED, "DelHandler before Init is NOT_INITIALIZED");

    /* AddHandler: cellNetCtl.cpp:160-168. */
    CHECK(nc::add_handler(false, false) == nc::ERROR_NOT_INITIALIZED, "AddHandler before Init is NOT_INITIALIZED");
    CHECK(nc::add_handler(true, true) == nc::ERROR_INVALID_ADDR, "AddHandler(NULL hid) is INVALID_ADDR");
    CHECK(nc::add_handler(true, false) == 0, "AddHandler with a valid hid succeeds");

    /* ---- 2. GetInfo offline: one code succeeds, the rest refuse ------
     * cellNetCtl.cpp:247-256. */
    {
        nc::Env env;
        nc::InfoOut out;

        /* ETHER_ADDR is answered BEFORE the connection test, so it succeeds
         * with no network -- and it is the ONLY code that does. */
        const int32_t rc = nc::get_info(true, false, false, nc::INFO_ETHER_ADDR, env, out);
        CHECK(rc == 0, "GetInfo(ETHER_ADDR) SUCCEEDS offline -- RPCS3 answers it before the connection test");
        CHECK(out.size == 6, "ETHER_ADDR writes exactly 6 bytes");
        bool all_ff = true;
        for (int i = 0; i < 6; i++) all_ff = all_ff && out.bytes[i] == 0xFF;
        CHECK(all_ff, "the default ether address is FF:FF:FF:FF:FF:FF (np_handler.h:438)");

        /* Every other declared code is NOT_CONNECTED. */
        const int32_t codes[] = {
            nc::INFO_DEVICE, nc::INFO_MTU, nc::INFO_LINK, nc::INFO_LINK_TYPE,
            nc::INFO_RSSI, nc::INFO_CHANNEL, nc::INFO_IP_CONFIG, nc::INFO_DHCP_HOSTNAME,
            nc::INFO_IP_ADDRESS, nc::INFO_NETMASK, nc::INFO_DEFAULT_ROUTE,
            nc::INFO_PRIMARY_DNS, nc::INFO_SECONDARY_DNS, nc::INFO_HTTP_PROXY_CONFIG,
            nc::INFO_UPNP_CONFIG,
        };
        bool all_refused = true;
        for (int32_t c : codes) {
            nc::InfoOut o;
            const int32_t r = nc::get_info(true, false, false, c, env, o);
            if (r != nc::ERROR_NOT_CONNECTED) {
                std::fprintf(stderr, "  code %d returned 0x%08x\n", c, static_cast<uint32_t>(r));
                all_refused = false;
            }
            if (o.size != 0) all_refused = false;  /* and nothing is written */
        }
        CHECK(all_refused, "every other GetInfo code is NOT_CONNECTED offline and writes nothing");

        /* Order: init, then the pointer, then ETHER_ADDR. A null pointer is
         * INVALID_ADDR even for the code that would have succeeded. */
        CHECK(nc::get_info(false, true, false, nc::INFO_ETHER_ADDR, env, out) == nc::ERROR_NOT_INITIALIZED,
              "GetInfo before Init is NOT_INITIALIZED, whatever the pointer");
        CHECK(nc::get_info(true, true, false, nc::INFO_ETHER_ADDR, env, out) == nc::ERROR_INVALID_ADDR,
              "GetInfo(NULL info) is INVALID_ADDR");
    }

    /* ---- 3. GetInfo connected: the values and their byte order -------
     * cellNetCtl.cpp:258-286, against the union in cellNetCtl.h:241-267. */
    {
        nc::Env env;
        env.local_ip = 0xC0A80105u;   /* 192.168.1.5 */
        env.netmask = 0xFFFFFF00u;    /* 255.255.255.0 */
        env.default_route = 0xC0A80101u;
        env.hostname = "console";
        env.upnp_status = 2;          /* SCE_NP_SIGNALING_NETINFO_UPNP_STATUS_VALID */

        nc::InfoOut out;
        CHECK(nc::get_info(true, false, true, nc::INFO_MTU, env, out) == 0, "GetInfo(MTU) succeeds when connected");
        CHECK(out.size == 4 && out.bytes[0] == 0x00 && out.bytes[1] == 0x00 &&
                  out.bytes[2] == 0x05 && out.bytes[3] == 0xDC,
              "MTU 1500 is written BIG-ENDIAN: 00 00 05 DC");

        CHECK(nc::get_info(true, false, true, nc::INFO_DEVICE, env, out) == 0, "GetInfo(DEVICE) succeeds");
        CHECK(out.size == 4 && out.bytes[3] == nc::DEVICE_WIRED, "the device is WIRED (0)");

        CHECK(nc::get_info(true, false, true, nc::INFO_IP_CONFIG, env, out) == 0, "GetInfo(IP_CONFIG) succeeds");
        CHECK(out.bytes[3] == nc::IP_STATIC, "the IP configuration is STATIC (1)");

        CHECK(nc::get_info(true, false, true, nc::INFO_IP_ADDRESS, env, out) == 0, "GetInfo(IP_ADDRESS) succeeds");
        CHECK(std::strcmp(reinterpret_cast<const char*>(out.bytes), "192.168.1.5") == 0,
              "IP_ADDRESS is the dotted string 192.168.1.5");
        CHECK(out.size == 12, "a 12-character string writes 13 bytes with its NUL");

        CHECK(nc::get_info(true, false, true, nc::INFO_NETMASK, env, out) == 0, "GetInfo(NETMASK) succeeds");
        CHECK(std::strcmp(reinterpret_cast<const char*>(out.bytes), "255.255.255.0") == 0,
              "NETMASK is 255.255.255.0");

        CHECK(nc::get_info(true, false, true, nc::INFO_DHCP_HOSTNAME, env, out) == 0, "GetInfo(DHCP_HOSTNAME) succeeds");
        CHECK(std::strcmp(reinterpret_cast<const char*>(out.bytes), "console") == 0, "DHCP_HOSTNAME is the hostname");

        /* The UPnP CONFIG value is the INVERTED enum: VALID signalling status
         * means UPNP_ON, which is 0 -- not 1. */
        CHECK(nc::get_info(true, false, true, nc::INFO_UPNP_CONFIG, env, out) == 0, "GetInfo(UPNP_CONFIG) succeeds");
        CHECK(out.bytes[3] == nc::UPNP_ON, "a VALID UPnP signalling status writes UPNP_ON, which is 0");
        env.upnp_status = 1;
        CHECK(nc::get_info(true, false, true, nc::INFO_UPNP_CONFIG, env, out) == 0, "GetInfo(UPNP_CONFIG) succeeds again");
        CHECK(out.bytes[3] == nc::UPNP_OFF, "an INVALID UPnP status writes UPNP_OFF, which is 1");

        /* An unknown code writes nothing and is still CELL_OK (cellNetCtl.cpp:285-288). */
        env.upnp_status = 2;
        nc::InfoOut unknown;
        CHECK(nc::get_info(true, false, true, 999, env, unknown) == 0,
              "an unknown GetInfo code returns CELL_OK, as RPCS3's default branch does");
        CHECK(unknown.size == 0, "an unknown GetInfo code writes NOTHING into the caller's union");
        CHECK(nc::get_info(true, false, true, nc::INFO_RESERVED1, env, unknown) == 0 &&
                  unknown.size == 0,
              "INFO_RESERVED1 is also unanswered -- RPCS3 has it commented out");
    }

    /* ---- 4. GetNatInfo: it succeeds OFFLINE --------------------------
     * cellNetCtl.cpp:377-403. There is no connection test in the reference at
     * all, so refusing here would be a refusal the module never makes. */
    {
        nc::Env env;
        nc::NatInfoOut out;
        CHECK(nc::get_nat_info(true, false, 16, env, out) == 0,
              "GetNatInfo SUCCEEDS offline: RPCS3 never tests the connection in it");
        CHECK(out.nat_type == nc::NATINFO_NAT_TYPE_2, "the NAT type is 2");
        CHECK(out.stun_status == nc::NATINFO_STUN_OK, "the STUN status is OK (2)");
        CHECK(out.upnp_status == 1,
              "the UPnP status is written RAW (the sceNp enum, 1 = INVALID), not inverted");
        CHECK(out.size == 16, "the caller's size is echoed back unchanged");
        CHECK(out.mapped_addr == 0, "no mapped address is reported");

        CHECK(nc::get_nat_info(false, false, 16, env, out) == nc::ERROR_NOT_INITIALIZED,
              "GetNatInfo before Init is NOT_INITIALIZED");
        CHECK(nc::get_nat_info(true, true, 16, env, out) == nc::ERROR_INVALID_ADDR,
              "GetNatInfo(NULL) is INVALID_ADDR");
        CHECK(nc::get_nat_info(true, false, 12, env, out) == nc::ERROR_INVALID_SIZE,
              "a size that is neither 16 nor 20 is INVALID_SIZE");
        CHECK(nc::get_nat_info(true, false, 24, env, out) == nc::ERROR_INVALID_SIZE,
              "24 is INVALID_SIZE too");
        CHECK(nc::get_nat_info(true, false, 20, env, out) == 0 && out.size == 20,
              "the 20-byte form is accepted");
    }

    /* ---- 5. the net-start dialog ------------------------------------- */
    {
        /* LoadAsync: cellNetCtl.cpp:310-328. */
        CHECK(nc::netstart_load_check(false, false, 12, 0) == nc::ERROR_NOT_INITIALIZED,
              "LoadAsync before Init is NOT_INITIALIZED");
        CHECK(nc::netstart_load_check(true, true, 12, 0) == nc::ERROR_INVALID_ADDR,
              "LoadAsync(NULL) is INVALID_ADDR");
        CHECK(nc::netstart_load_check(true, false, 12, 2) == nc::ERROR_INVALID_TYPE,
              "type 2 (CELL_NET_CTL_NETSTART_TYPE_MAX) is INVALID_TYPE");
        CHECK(nc::netstart_load_check(true, false, 16, 0) == nc::ERROR_INVALID_SIZE,
              "size 16 is INVALID_SIZE: the structure is 12 bytes");
        CHECK(nc::netstart_load_check(true, false, 12, 0) == 0, "type 0 size 12 succeeds");
        CHECK(nc::netstart_load_check(true, false, 12, 1) == 0, "type 1 size 12 succeeds");

        /* UnloadAsync: cellNetCtl.cpp:356-371. */
        nc::DialogResultOut out;
        CHECK(nc::netstart_unload(false, false, 8, false, out) == nc::ERROR_NOT_INITIALIZED,
              "UnloadAsync before Init is NOT_INITIALIZED");
        CHECK(nc::netstart_unload(true, true, 8, false, out) == nc::ERROR_INVALID_ADDR,
              "UnloadAsync(NULL) is INVALID_ADDR");
        CHECK(nc::netstart_unload(true, false, 12, false, out) == nc::ERROR_INVALID_SIZE,
              "a size other than 8 is INVALID_SIZE");
        CHECK(nc::netstart_unload(true, false, 8, false, out) == 0, "the valid call succeeds");
        CHECK(out.result == nc::ERROR_DIALOG_CANCELED,
              "offline the dialog result is DIALOG_CANCELED -- the 'play offline' path");
        CHECK(nc::netstart_unload(true, false, 8, true, out) == 0, "the connected call succeeds");
        CHECK(out.result == 0, "connected, the dialog result is 0");

        /* AbortAsync: cellNetCtl.cpp:340-347. */
        CHECK(nc::netstart_abort(false) == nc::ERROR_NOT_INITIALIZED,
              "AbortAsync before Init is NOT_INITIALIZED");
        CHECK(nc::netstart_abort(true) == 0, "AbortAsync after Init is CELL_OK");
    }

    /* ---- 6. the handlers themselves -----------------------------------
     * Real guest memory, real r3. This is what the import path runs. */
    {
        /* Offline is the default; make it explicit so the test does not inherit
         * a developer's shell. */
        unsetenv("PS3BASE_NET_CONNECTED");

        ppu::Context ctx;
        const uint32_t word = alloc_area(16);
        const uint32_t info = alloc_area(512);
        const uint32_t nat  = alloc_area(32);
        const uint32_t param = alloc_area(16);
        const uint32_t result = alloc_area(16);
        CHECK(word && info && nat && param && result, "guest memory for the handler tests");
        if (!word || !info || !nat || !param || !result) return 1;

        /* Before Init, everything network-shaped refuses. */
        CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlGetState, word)) == nc::ERROR_NOT_INITIALIZED,
              "GetState before Init -> NOT_INITIALIZED");
        CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlGetInfo, nc::INFO_ETHER_ADDR, info)) ==
                  nc::ERROR_NOT_INITIALIZED,
              "GetInfo before Init -> NOT_INITIALIZED");

        /* Init twice. */
        CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlInit)) == 0, "Init -> CELL_OK");
        CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlInit)) == nc::ERROR_NOT_TERMINATED,
              "the second Init -> NOT_TERMINATED");

        /* GetState: writes Disconnected (0) into the caller's word and returns
         * CELL_OK. This is the state the title's "am I online" test reads. */
        vm::write32(word, 0xDEADBEEF);
        CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlGetState, word)) == 0,
              "GetState -> CELL_OK");
        CHECK(vm::read32(word) == 0, "GetState wrote Disconnected (0) into the caller's word");
        CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlGetState, 0)) == nc::ERROR_INVALID_ADDR,
              "GetState(NULL) -> INVALID_ADDR");

        /* GetInfo: ETHER_ADDR succeeds and writes 6 bytes; nothing beyond them
         * is touched. */
        std::memset(vm::ptr(info), 0x5A, 512);
        CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlGetInfo, nc::INFO_ETHER_ADDR, info)) == 0,
              "GetInfo(ETHER_ADDR) -> CELL_OK offline");
        CHECK(vm::ptr(info)[0] == 0xFF && vm::ptr(info)[5] == 0xFF, "6 bytes of ether address were written");
        CHECK(vm::ptr(info)[6] == 0x5A,
              "byte 6 is untouched: a 6-byte member is a 6-byte write, not a 256-byte memset");

        /* The write-size rule: a 6-byte buffer is enough for ETHER_ADDR, which
         * is what the console takes, and NOT enough for a 256-byte member. */
        CHECK(nc::info_write_size(nc::INFO_ETHER_ADDR) == 6, "ETHER_ADDR needs 6 writable bytes, not 256");
        CHECK(nc::info_write_size(nc::INFO_RSSI) == 1, "RSSI is one byte");
        CHECK(nc::info_write_size(nc::INFO_DHCP_HOSTNAME) == 256, "the hostname member is 256 bytes");
        CHECK(nc::info_write_size(nc::INFO_MTU) == 4, "an integer member is 4 bytes");
        {
            const uint32_t tiny = alloc_area(16);   /* 16 bytes, far short of 256 */
            CHECK(tiny != 0, "a small buffer for the write-size test");
            if (tiny) {
                std::memset(vm::ptr(tiny), 0x5A, 16);
                CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlGetInfo, nc::INFO_ETHER_ADDR, tiny)) == 0,
                      "GetInfo(ETHER_ADDR) into a 16-byte buffer succeeds");
                CHECK(vm::ptr(tiny)[5] == 0xFF, "the 6-byte MAC landed in the small buffer");
                CHECK(vm::ptr(tiny)[6] == 0x5A, "and byte 6 is untouched");
                CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlGetInfo, nc::INFO_DHCP_HOSTNAME, tiny)) ==
                          nc::ERROR_NOT_CONNECTED,
                      "the same buffer is refused for the 256-byte hostname member (offline: NOT_CONNECTED answers first)");
            }
        }

        /* GetInfo: every other code is NOT_CONNECTED and leaves the caller's
         * union alone. */
        std::memset(vm::ptr(info), 0x5A, 512);
        CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlGetInfo, nc::INFO_IP_ADDRESS, info)) ==
                  nc::ERROR_NOT_CONNECTED,
              "GetInfo(IP_ADDRESS) -> NOT_CONNECTED offline");
        CHECK(vm::ptr(info)[0] == 0x5A, "a refused GetInfo writes nothing into the caller's union");

        /* GetNatInfo answers SUCCESSFULLY offline and fills the structure. */
        vm::write32(nat, 16);
        std::memset(vm::ptr(nat + 4), 0x5A, 12);
        CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlGetNatInfo, nat)) == 0,
              "GetNatInfo -> CELL_OK offline (the module never tests the connection here)");
        CHECK(vm::read32(nat) == 16, "the size field is echoed back, not rewritten to something else");
        CHECK(static_cast<int32_t>(vm::read32(nat + 4)) == 1, "upnp_status is the raw signalling status (1)");
        CHECK(static_cast<int32_t>(vm::read32(nat + 8)) == nc::NATINFO_STUN_OK, "stun_status is 2");
        CHECK(static_cast<int32_t>(vm::read32(nat + 12)) == nc::NATINFO_NAT_TYPE_2, "nat_type is 2");
        vm::write32(nat, 12);
        CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlGetNatInfo, nat)) == nc::ERROR_INVALID_SIZE,
              "GetNatInfo with size 12 -> INVALID_SIZE");

        /* AddHandler writes a usable id, and DelHandler accepts it. RPCS3's
         * AddHandler writes nothing at all; an id is written here because the
         * title hands whatever it read straight back to DelHandler. */
        vm::write32(word, 0xDEADBEEF);
        CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlAddHandler, 0x00010000, 0, word)) == 0,
              "AddHandler -> CELL_OK");
        const uint32_t hid = vm::read32(word);
        CHECK(hid <= 3, "AddHandler wrote an id DelHandler's own range check accepts");
        CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlDelHandler, hid)) == 0,
              "DelHandler accepts the id AddHandler handed out");
        CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlDelHandler, 4)) == nc::ERROR_INVALID_ID,
              "DelHandler(4) -> INVALID_ID");
        CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlAddHandler, 0x00010000, 0, 0)) ==
                  nc::ERROR_INVALID_ADDR,
              "AddHandler(NULL hid) -> INVALID_ADDR");
    }

    /* ---- 7. the dialogs through the handlers --------------------------
     * LoadAsync validates and returns CELL_OK; UnloadAsync writes the result
     * and the size is never rewritten by us. */
    {
        ppu::Context ctx;
        const uint32_t param = alloc_area(16);
        const uint32_t result = alloc_area(16);
        CHECK(param && result, "guest memory for the dialog tests");
        if (!param || !result) return 1;

        /* A valid param: size 12, type 0 (cellNetCtl.cpp:325, :320). */
        vm::write32(param + 0, 12);
        vm::write32(param + 4, 0);
        vm::write32(param + 8, 0);
        g_posted = 0;
        CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlNetStartDialogLoadAsync, param)) == 0,
              "LoadAsync with size 12 type 0 -> CELL_OK");
        CHECK(g_posted == 2,
              "LoadAsync raised TWO system commands (LOADED then FINISHED), as netstart_hack does");

        /* A type of 2 is INVALID_TYPE; a size of 16 is INVALID_SIZE -- and a
         * refused call raises NO system command: a title watching for LOADED
         * must not be told a dialog opened when none did. */
        const int posted_before_refusals = g_posted;
        vm::write32(param + 4, 2);
        CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlNetStartDialogLoadAsync, param)) ==
                  nc::ERROR_INVALID_TYPE,
              "LoadAsync with type 2 -> INVALID_TYPE");
        vm::write32(param + 4, 0);
        vm::write32(param + 0, 16);
        CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlNetStartDialogLoadAsync, param)) ==
                  nc::ERROR_INVALID_SIZE,
              "LoadAsync with size 16 -> INVALID_SIZE");
        CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlNetStartDialogLoadAsync, 0)) ==
                  nc::ERROR_INVALID_ADDR,
              "LoadAsync(NULL) -> INVALID_ADDR");
        CHECK(g_posted == posted_before_refusals,
              "the three refused LoadAsync calls raised no system command at all");

        /* UnloadAsync: the caller's size is validated, never rewritten, and the
         * result is DIALOG_CANCELED offline (cellNetCtl.cpp:366-371). */
        vm::write32(result + 0, 8);
        vm::write32(result + 4, 0xDEADBEEF);
        g_posted = 0;
        CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlNetStartDialogUnloadAsync, result)) == 0,
              "UnloadAsync with size 8 -> CELL_OK");
        CHECK(g_posted == 1, "UnloadAsync raised ONE system command (UNLOADED)");
        CHECK(vm::read32(result + 0) == 8, "the caller's size field is left as it was");
        CHECK(static_cast<int32_t>(vm::read32(result + 4)) == nc::ERROR_DIALOG_CANCELED,
              "offline the dialog result is DIALOG_CANCELED");
        vm::write32(result + 0, 4);
        CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlNetStartDialogUnloadAsync, result)) ==
                  nc::ERROR_INVALID_SIZE,
              "UnloadAsync with size 4 -> INVALID_SIZE");

        /* AbortAsync: CELL_OK once initialised. */
        CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlNetStartDialogAbortAsync)) == 0,
              "AbortAsync -> CELL_OK");

        /* Term, then everything is NOT_INITIALIZED again. */
        call(ctx, hle::hle_cellNetCtl_cellNetCtlTerm);
        CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlGetState, 0)) == nc::ERROR_NOT_INITIALIZED,
              "after Term, GetState -> NOT_INITIALIZED");
        CHECK(low32(call(ctx, hle::hle_cellNetCtl_cellNetCtlNetStartDialogAbortAsync)) ==
                  nc::ERROR_NOT_INITIALIZED,
              "after Term, AbortAsync -> NOT_INITIALIZED");
    }

    /* ---- 8. the imports are all registered, by the key the dispatcher
     * builds ("module::name", ppu_runtime.cpp:85-88) and pointing at the
     * handler this test called. A typo in the name resolves nowhere. */
    {
        /* The nine functions this title imports, out-rdr/ppu/table.cpp:81917-81925. */
        const char* names[] = {
            "cellNetCtlInit", "cellNetCtlTerm", "cellNetCtlGetState",
            "cellNetCtlAddHandler", "cellNetCtlDelHandler", "cellNetCtlGetInfo",
            "cellNetCtlGetNatInfo", "cellNetCtlNetStartDialogLoadAsync",
            "cellNetCtlNetStartDialogUnloadAsync",
        };
        const auto& reg = test_registry();
        int found = 0;
        for (const char* n : names) {
            const auto it = reg.find(std::string("cellNetCtl::") + n);
            if (it != reg.end()) found++;
            else std::fprintf(stderr, "[FAIL] not registered: cellNetCtl::%s\n", n);
        }
        CHECK(found == 9, "all nine imports the title uses are registered under cellNetCtl::");

        const auto it = reg.find("cellNetCtl::cellNetCtlGetInfo");
        CHECK(it != reg.end() && it->second == &hle::hle_cellNetCtl_cellNetCtlGetInfo,
              "cellNetCtlGetInfo is registered against the handler this test called");

        /* Ten handlers: the nine imports plus AbortAsync, which this title does
         * not import but which completes the dialog pair. No more, no less. */
        int total = 0;
        for (const auto& kv : reg)
            if (kv.first.rfind("cellNetCtl::", 0) == 0) total++;
        CHECK(total == 10, "the module registers exactly 10 handlers");
    }

    std::printf("netctl: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}