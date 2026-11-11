#pragma once

/* cellNetCtl -- the module's decision logic, separated from the guest-memory
 * plumbing so it can be tested with no game, no GPU and no network (the same
 * split as hle/cellgame_hle.h).
 *
 * Every constant below is cited from the reference header, never retyped from
 * memory (AGENTS.md):
 *
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellNetCtl.h
 *
 * and every return code from the reference implementation:
 *
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellNetCtl.cpp
 *
 * The one environmental fact is "is this console connected". RPCS3 derives it
 * from its network configuration (np_handler::get_net_status,
 * refs/rpcs3/rpcs3/Emu/NP/np_handler.cpp:867-870) and returns Disconnected when
 * it cannot reach the network. That is the state this runtime reports by
 * default, so every network answer below is the console's offline answer.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace hle {
namespace netctl {

/* Error codes: cellNetCtl.h:6-53. */
constexpr int32_t ERROR_NOT_INITIALIZED  = static_cast<int32_t>(0x80130101);  /* :8  */
constexpr int32_t ERROR_NOT_TERMINATED   = static_cast<int32_t>(0x80130102);  /* :9  */
constexpr int32_t ERROR_HANDLER_MAX      = static_cast<int32_t>(0x80130103);  /* :10 */
constexpr int32_t ERROR_ID_NOT_FOUND     = static_cast<int32_t>(0x80130104);  /* :11 */
constexpr int32_t ERROR_INVALID_ID       = static_cast<int32_t>(0x80130105);  /* :12 */
constexpr int32_t ERROR_INVALID_CODE     = static_cast<int32_t>(0x80130106);  /* :13 */
constexpr int32_t ERROR_INVALID_ADDR     = static_cast<int32_t>(0x80130107);  /* :14 */
constexpr int32_t ERROR_NOT_CONNECTED    = static_cast<int32_t>(0x80130108);  /* :15 */
constexpr int32_t ERROR_NOT_AVAIL        = static_cast<int32_t>(0x80130109);  /* :16 */
constexpr int32_t ERROR_INVALID_TYPE     = static_cast<int32_t>(0x8013010a);  /* :17 */
constexpr int32_t ERROR_INVALID_SIZE     = static_cast<int32_t>(0x8013010b);  /* :18 */
constexpr int32_t ERROR_DIALOG_CANCELED  = static_cast<int32_t>(0x80130190);  /* :25 */
constexpr int32_t ERROR_DIALOG_ABORTED   = static_cast<int32_t>(0x80130191);  /* :26 */

/* Connection states: cellNetCtl.h:56-62. */
constexpr int32_t STATE_DISCONNECTED = 0;  /* :58 */
constexpr int32_t STATE_CONNECTING   = 1;  /* :59 */
constexpr int32_t STATE_IPOBTAINING  = 2;  /* :60 */
constexpr int32_t STATE_IPOBTAINED   = 3;  /* :61 */

/* Information codes: cellNetCtl.h:145-173. */
constexpr int32_t INFO_DEVICE            = 1;   /* :147 */
constexpr int32_t INFO_ETHER_ADDR        = 2;   /* :148 */
constexpr int32_t INFO_MTU               = 3;   /* :149 */
constexpr int32_t INFO_LINK              = 4;   /* :150 */
constexpr int32_t INFO_LINK_TYPE         = 5;   /* :151 */
constexpr int32_t INFO_BSSID             = 6;   /* :152 */
constexpr int32_t INFO_SSID              = 7;   /* :153 */
constexpr int32_t INFO_WLAN_SECURITY     = 8;   /* :154 */
constexpr int32_t INFO_8021X_TYPE        = 9;   /* :155 */
constexpr int32_t INFO_8021X_AUTH_NAME   = 10;  /* :156 */
constexpr int32_t INFO_RSSI              = 11;  /* :157 */
constexpr int32_t INFO_CHANNEL           = 12;  /* :158 */
constexpr int32_t INFO_IP_CONFIG         = 13;  /* :159 */
constexpr int32_t INFO_DHCP_HOSTNAME     = 14;  /* :160 */
constexpr int32_t INFO_PPPOE_AUTH_NAME   = 15;  /* :161 */
constexpr int32_t INFO_IP_ADDRESS        = 16;  /* :162 */
constexpr int32_t INFO_NETMASK           = 17;  /* :163 */
constexpr int32_t INFO_DEFAULT_ROUTE     = 18;  /* :164 */
constexpr int32_t INFO_PRIMARY_DNS       = 19;  /* :165 */
constexpr int32_t INFO_SECONDARY_DNS     = 20;  /* :166 */
constexpr int32_t INFO_HTTP_PROXY_CONFIG = 21;  /* :167 */
constexpr int32_t INFO_HTTP_PROXY_SERVER = 22;  /* :168 */
constexpr int32_t INFO_HTTP_PROXY_PORT   = 23;  /* :169 */
constexpr int32_t INFO_UPNP_CONFIG       = 24;  /* :170 */
constexpr int32_t INFO_RESERVED1         = 25;  /* :171 */
constexpr int32_t INFO_RESERVED2         = 26;  /* :172 */

/* Devices: cellNetCtl.h:77-81. */
constexpr uint32_t DEVICE_WIRED    = 0;  /* :79 */
constexpr uint32_t DEVICE_WIRELESS = 1;  /* :80 */

/* Link types: cellNetCtl.h:84-93. */
constexpr uint32_t LINK_TYPE_100BASE_FULL = 4;  /* :90 */

/* Link states: cellNetCtl.h:96-100. */
constexpr uint32_t LINK_CONNECTED = 1;  /* :99 */

/* IP configuration: cellNetCtl.h:123-128. */
constexpr uint32_t IP_STATIC = 1;  /* :126 */

/* UPnP settings: cellNetCtl.h:138-142. The enum is INVERTED: ON is 0 and OFF
 * is 1. RPCS3 selects between them at cellNetCtl.cpp:282. */
constexpr uint32_t UPNP_ON  = 0;  /* :140 */
constexpr uint32_t UPNP_OFF = 1;  /* :141 */

/* UPnP NAT statuses: cellNetCtl.h:191-197. */
constexpr int32_t NATINFO_UPNP_UNCHECKED = 0;  /* :194 */
constexpr int32_t NATINFO_UPNP_NO        = 1;  /* :195 */
constexpr int32_t NATINFO_UPNP_USED      = 2;  /* :196 */

/* STUN NAT statuses: cellNetCtl.h:199-205. */
constexpr int32_t NATINFO_STUN_OK = 2;  /* :204 */

/* NAT types: cellNetCtl.h:207-213. */
constexpr int32_t NATINFO_NAT_TYPE_2 = 2;  /* :211 */

/* Network start dialogs: cellNetCtl.h:175-181. */
constexpr int32_t NETSTART_TYPE_MAX = 2;  /* :180 */

/* Network start dialog statuses, raised as sysutil events: cellNetCtl.h:183-189. */
constexpr uint64_t SYSUTIL_NET_CTL_NETSTART_LOADED   = 0x0801;  /* :186 */
constexpr uint64_t SYSUTIL_NET_CTL_NETSTART_FINISHED = 0x0802;  /* :187 */
constexpr uint64_t SYSUTIL_NET_CTL_NETSTART_UNLOADED = 0x0803;  /* :188 */

/* Sizes: cellNetCtl.h:215-226. */
constexpr uint32_t ETHER_ADDR_LEN    = 6;    /* :217 */
constexpr uint32_t HOSTNAME_LEN      = 256;  /* :224, (255 + 1) */
constexpr uint32_t IPV4_ADDR_STR_LEN = 16;   /* :225 */

/* The largest member of the CellNetCtlInfo union (cellNetCtl.h:241-267) is
 * dhcp_hostname / http_proxy_server at CELL_NET_CTL_HOSTNAME_LEN = 256 bytes.
 * Every member starts at offset 0, so a write is always (offset 0, own size):
 * this is the size of the buffer a handler may touch. */
constexpr uint32_t INFO_MAX = HOSTNAME_LEN;

/* Structure sizes, from the structures themselves (cellNetCtl.h:269-289): a
 * caller passes its own sizeof() in the first field and the module validates
 * it against these exact numbers (cellNetCtl.cpp:325, :366, :393). */
constexpr uint32_t NETSTART_PARAM_SIZE  = 12;  /* :269-274 size,type,cid      */
constexpr uint32_t NETSTART_RESULT_SIZE = 8;   /* :276-280 size,result        */
constexpr uint32_t NATINFO_SIZE_MIN     = 16;  /* :282-289 without mapped_addr */
constexpr uint32_t NATINFO_SIZE_MAX     = 20;  /* :282-289 with mapped_addr    */

/* RPCS3's cellNetCtlDelHandler accepts a handler id up to this value and
 * refuses only ids GREATER than it (cellNetCtl.cpp:184-187). */
constexpr int32_t HANDLER_ID_MAX = 3;

/* The single handler slot this runtime keeps. RPCS3's AddHandler is a todo
 * stub that returns CELL_OK and writes nothing into *hid
 * (cellNetCtl.cpp:154-171); leaving the caller's out-parameter untouched hands
 * the title an uninitialised id which it then passes to DelHandler, whose
 * range check would refuse it. Writing a valid id is what the console does.
 * The conformance table in runtime/src/hle/cellNetCtl.cpp records this. */
constexpr int32_t HANDLER_ID = 0;

/* The environment the answers are derived from. The defaults are RPCS3's own
 * defaults for the fields, from refs/rpcs3/rpcs3/Emu/NP/np_handler.h:437-444:
 *   hostname      "localhost"                              (:437)
 *   ether_address {0xFF,...,0xFF}                          (:438)
 *   local_ip_addr 0                                        (:439)
 *   dns_ip        0x08080808                               (:441)
 *   netmask       0                                        (:443)
 *   default_route 0                                        (:444)
 * and the UPnP status when no redirection is active is
 * SCE_NP_SIGNALING_NETINFO_UPNP_STATUS_INVALID = 1 (np_handler.cpp:877-883,
 * sceNp.h:1016). */
struct Env {
    uint8_t ether[ETHER_ADDR_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    std::string hostname = "localhost";
    uint32_t local_ip = 0;
    uint32_t dns_ip = 0x08080808u;
    uint32_t netmask = 0;
    uint32_t default_route = 0;
    int32_t upnp_status = 1;  /* SCE_NP_SIGNALING_NETINFO_UPNP_STATUS_INVALID */
};

/* ip_to_string: refs/rpcs3/rpcs3/Emu/NP/np_helpers.cpp:16-22 formats the stored
 * 32-bit address with inet_ntop, i.e. the bytes in network order. */
inline std::string ip_to_string(uint32_t addr) {
    char out[IPV4_ADDR_STR_LEN];
    std::snprintf(out, sizeof(out), "%u.%u.%u.%u", (addr >> 24) & 0xFFu, (addr >> 16) & 0xFFu,
                  (addr >> 8) & 0xFFu, addr & 0xFFu);
    return std::string(out);
}

/* The value the module writes into the caller's CellNetCtlInfo: a small buffer
 * and the number of leading bytes that are meaningful. Never a blanket memset:
 * RPCS3 writes exactly one member per code (cellNetCtl.cpp:258-286), and the
 * rest of the caller's union is not ours to touch. */
struct InfoOut {
    uint32_t size = 0;
    uint8_t bytes[INFO_MAX] = {};
};

struct NatInfoOut {
    uint32_t size = 0;
    int32_t upnp_status = 0;
    int32_t stun_status = 0;
    int32_t nat_type = 0;
    uint32_t mapped_addr = 0;
};

struct DialogResultOut {
    int32_t result = 0;
};

/* ---- the decisions, one function per RPCS3 entry point ------------------- */

/* cellNetCtlInit, cellNetCtl.cpp:108-122: a second init is NOT_TERMINATED. */
inline int32_t init(bool already_initialised) {
    return already_initialised ? ERROR_NOT_TERMINATED : 0;  /* :114-121 */
}

/* cellNetCtlGetState, cellNetCtl.cpp:133-152. The init check comes BEFORE the
 * pointer check (:139-147), so a null pointer with no init is NOT_INITIALIZED,
 * not INVALID_ADDR. */
inline int32_t get_state(bool initialised, bool state_is_null, bool connected) {
    (void)connected;
    if (!initialised) return ERROR_NOT_INITIALIZED;
    if (state_is_null) return ERROR_INVALID_ADDR;
    return 0;
}

/* np_handler::get_net_status, np_handler.cpp:867-870. */
inline int32_t net_state(bool connected) {
    return connected ? STATE_IPOBTAINED : STATE_DISCONNECTED;
}

/* cellNetCtlAddHandler, cellNetCtl.cpp:154-171. Signature and null checks only:
 * RPCS3 writes nothing into *hid, so the id is this runtime's business. */
inline int32_t add_handler(bool initialised, bool hid_is_null) {
    if (!initialised) return ERROR_NOT_INITIALIZED;
    if (hid_is_null) return ERROR_INVALID_ADDR;
    return 0;
}

/* cellNetCtlDelHandler, cellNetCtl.cpp:173-190: the only bound is
 * "hid > 3". A negative id is therefore ACCEPTED -- that is what RPCS3 does,
 * so it is what the console does. Returning INVALID_ID for hid < 0 would be a
 * refusal the module never makes. */
inline int32_t del_handler(bool initialised, int32_t hid) {
    if (!initialised) return ERROR_NOT_INITIALIZED;
    if (hid > HANDLER_ID_MAX) return ERROR_INVALID_ID;
    return 0;
}

/* The 1500 of cellNetCtl.cpp:261, spelled so the big-endian write is explicit
 * (the caller's field is be_t<u32>). */
constexpr uint8_t MTU_HI = 0x05;
constexpr uint8_t MTU_LO = 0xDC;
/* cellNetCtl.cpp:269-270. */
constexpr uint8_t RSSI_VALUE = 100;
constexpr uint8_t CHANNEL_VALUE = 1;
/* SCE_NP_SIGNALING_NETINFO_UPNP_STATUS_VALID, sceNp.h:1017. */
constexpr int32_t NETINFO_UPNP_STATUS_VALID = 2;

/* Write a NUL-terminated string into the leading bytes of an info member.
 * strcpy_trunc (cellNetCtl.cpp:272-278) copies at most the member's size. */
inline void set_str(InfoOut& out, const std::string& s) {
    out.size = static_cast<uint32_t>(s.size() + 1);
    if (out.size > INFO_MAX) out.size = INFO_MAX;
    std::memcpy(out.bytes, s.c_str(), out.size);
}

/* cellNetCtlGetInfo, cellNetCtl.cpp:192-289.
 *
 * Order, exactly as the reference: init (:237), pointer (:242), then
 * ETHER_ADDR is answered BEFORE the connection test (:247-251), and only after
 * that does a disconnected console refuse with NOT_CONNECTED (:253-256). One
 * code therefore succeeds offline and the others do not.
 *
 * An unknown code falls into RPCS3's "default:" (:285) which logs and still
 * returns CELL_OK having written nothing. Reproduced: out.size stays 0, which
 * is the whole of "nothing was written". */
inline int32_t get_info(bool initialised, bool info_is_null, bool connected, int32_t code,
                        const Env& env, InfoOut& out) {
    out = InfoOut{};
    if (!initialised) return ERROR_NOT_INITIALIZED;
    if (info_is_null) return ERROR_INVALID_ADDR;

    if (code == INFO_ETHER_ADDR) {  /* :247-251 */
        std::memcpy(out.bytes, env.ether, ETHER_ADDR_LEN);
        out.size = ETHER_ADDR_LEN;
        return 0;
    }

    if (!connected) return ERROR_NOT_CONNECTED;  /* :253-256 */

    switch (code) {
    case INFO_DEVICE:    /* :260 */ out.size = 4; out.bytes[3] = static_cast<uint8_t>(DEVICE_WIRED); break;
    case INFO_MTU:       /* :261 */ out.size = 4; out.bytes[2] = MTU_HI; out.bytes[3] = MTU_LO; break;
    case INFO_LINK:      /* :262 */ out.size = 4; out.bytes[3] = static_cast<uint8_t>(LINK_CONNECTED); break;
    case INFO_LINK_TYPE: /* :263 */ out.size = 4; out.bytes[3] = static_cast<uint8_t>(LINK_TYPE_100BASE_FULL); break;
    case INFO_RSSI:      /* :269 */ out.size = 1; out.bytes[0] = RSSI_VALUE; break;
    case INFO_CHANNEL:   /* :270 */ out.size = 1; out.bytes[0] = CHANNEL_VALUE; break;
    case INFO_IP_CONFIG: /* :271 */ out.size = 4; out.bytes[3] = static_cast<uint8_t>(IP_STATIC); break;

    case INFO_DHCP_HOSTNAME:  /* :272 */ set_str(out, env.hostname); break;
    case INFO_IP_ADDRESS:     /* :274 */ set_str(out, ip_to_string(env.local_ip)); break;
    case INFO_NETMASK:        /* :275 */ set_str(out, ip_to_string(env.netmask)); break;
    case INFO_DEFAULT_ROUTE:  /* :276 */ set_str(out, ip_to_string(env.default_route)); break;
    case INFO_PRIMARY_DNS:    /* :277 */ set_str(out, ip_to_string(env.dns_ip)); break;
    case INFO_SECONDARY_DNS:  /* :278 */ set_str(out, ip_to_string(env.dns_ip)); break;
    case INFO_HTTP_PROXY_CONFIG: /* :279 */ out.size = 4; break;

    case INFO_UPNP_CONFIG:       /* :282 */
        out.size = 4;
        /* The sceNp status is VALID only when a redirection is active; RPCS3
         * compares it against SCE_NP_SIGNALING_NETINFO_UPNP_STATUS_VALID (2)
         * and then writes the INVERTED cellNetCtl value. */
        out.bytes[3] = static_cast<uint8_t>(env.upnp_status == NETINFO_UPNP_STATUS_VALID ? UPNP_ON
                                                                                        : UPNP_OFF);
        break;
    default:
        /* Unsupported code: nothing written, CELL_OK (:285-288). */
        return 0;
    }
    return 0;
}

/* cellNetCtlGetNatInfo, cellNetCtl.cpp:377-403. Unlike GetInfo this does NOT
 * test the connection at all: a disconnected console still reports NAT type 2,
 * STUN OK and the UPnP signalling status. Returning NOT_CONNECTED here (as the
 * previous handler did) is a refusal the module never makes. */
inline int32_t get_nat_info(bool initialised, bool nat_is_null, uint32_t caller_size,
                            const Env& env, NatInfoOut& out) {
    out = NatInfoOut{};
    if (!initialised) return ERROR_NOT_INITIALIZED;
    if (nat_is_null) return ERROR_INVALID_ADDR;
    if (caller_size != NATINFO_SIZE_MIN && caller_size != NATINFO_SIZE_MAX)
        return ERROR_INVALID_SIZE;  /* :393-396 */
    out.size = caller_size;
    out.nat_type = NATINFO_NAT_TYPE_2;    /* :398 */
    out.stun_status = NATINFO_STUN_OK;    /* :399 */
    out.upnp_status = env.upnp_status;    /* :400, the RAW signalling enum */
    out.mapped_addr = 0;
    return 0;
}

/* cellNetCtlNetStartDialogLoadAsync, cellNetCtl.cpp:304-334. */
inline int32_t netstart_load_check(bool initialised, bool param_is_null, uint32_t size,
                                   int32_t type) {
    if (!initialised) return ERROR_NOT_INITIALIZED;
    if (param_is_null) return ERROR_INVALID_ADDR;
    if (type >= NETSTART_TYPE_MAX) return ERROR_INVALID_TYPE;    /* :320-323 */
    if (size != NETSTART_PARAM_SIZE) return ERROR_INVALID_SIZE;  /* :325-328 */
    return 0;
}

/* cellNetCtlNetStartDialogAbortAsync, cellNetCtl.cpp:336-348: init check, then
 * CELL_OK. Nothing else exists to abort. */
inline int32_t netstart_abort(bool initialised) {
    if (!initialised) return ERROR_NOT_INITIALIZED;
    return 0;
}

/* cellNetCtlNetStartDialogUnloadAsync, cellNetCtl.cpp:350-375. The size field
 * is READ and validated, never rewritten: a caller that passed a wrong size is
 * told INVALID_SIZE, not silently corrected. */
inline int32_t netstart_unload(bool initialised, bool result_is_null, uint32_t size,
                               bool connected, DialogResultOut& out) {
    out = DialogResultOut{};
    if (!initialised) return ERROR_NOT_INITIALIZED;
    if (result_is_null) return ERROR_INVALID_ADDR;
    if (size != NETSTART_RESULT_SIZE) return ERROR_INVALID_SIZE;
    out.result = connected ? 0 : ERROR_DIALOG_CANCELED;  /* :371 */
    return 0;
}

}  // namespace netctl
}  // namespace hle
