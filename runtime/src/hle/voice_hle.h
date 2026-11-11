/* ps3base -- the cellVoice (libvoice) state machine, in a header so the unit
 * test drives exactly the code the import runs.
 *
 * Reference: refs/rpcs3/rpcs3/Emu/Cell/Modules/cellVoice.h    (codes, enums, structs)
 *            refs/rpcs3/rpcs3/Emu/Cell/Modules/cellVoice.cpp  (behaviour)
 *
 * Red Dead Redemption (BLES00680) imports exactly 13 functions of this module;
 * the list is out-rdr/ppu/table.cpp:81815-81827 (checked against
 * ref/nid_db.json, which agrees on every NID):
 *
 *   cellVoiceStart                    0x0A563878
 *   cellVoiceDisconnectIPortFromOPort 0x18D3DF30
 *   cellVoiceCreatePort               0x2DE54871
 *   cellVoiceReadFromOPort            0x36472C57
 *   cellVoiceWriteToIPort             0x3DAD26E7
 *   cellVoiceGetPortInfo              0x54AC3519
 *   cellVoiceSetPortAttr              0x9D0F4AF1
 *   cellVoiceDeletePort               0x9F70C475
 *   cellVoiceConnectIPortToOPort      0xAE6A21D5
 *   cellVoiceGetBitRate               0xBEF53A2B
 *   cellVoiceInit                     0xC7CF1182
 *   cellVoiceStop                     0xD3A84BE1
 *   cellVoiceEnd                      0xE0E1AE12
 *
 * cellVoiceCreateNotifyEventQueue (0x2A01013E) is NOT in that list -- the title
 * never registers a notify queue -- but it is implemented because RPCS3 has it
 * and because cellVoiceStart/Stop/End deliver to whatever queue that function
 * registered. The rest of RPCS3's surface (InitEx, GetPortAttr, GetSignalState,
 * PausePortAll, ResumePortAll, SetMuteFlagAll, DebugTopology) is registered too,
 * so no import can fall through to a generic CELL_OK.
 *
 * Every function below answers what RPCS3 answers, success included (AGENTS.md:
 * an implemented function answers what the console answers). The one shape of
 * "the console has no device" is the raw code RPCS3 returns from
 * cellVoiceSetPortAttr / cellVoiceGetPortAttr / cellVoiceGetSignalState through
 * not_an_error(): CELL_VOICE_ERROR_DEVICE_NOT_PRESENT = 0x80310812
 * (refs/.../cellVoice.cpp:330, :377, :663; refs/.../ErrorCodes.h:58-83). It is
 * returned as-is, not turned into CELL_OK, so the title sees the same absence.
 *
 * Structures are NOT re-typed from memory: the offsets come from
 * refs/.../cellVoice.h:120-174 and are asserted with static_assert below. They
 * are guest (big-endian) layouts, so every field is read with vm::read32/read16
 * at its offset rather than by casting a host struct over guest memory.
 */
#ifndef PS3BASE_HLE_VOICE_H
#define PS3BASE_HLE_VOICE_H

#include "runtime/memory.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace hle {
namespace voice {

/* libvoice error codes, refs/.../cellVoice.h:9-27. The values are the ones the
 * header names; 0x80310801 - 0x803108ff is the library's range (:3-4). */
constexpr int32_t ERR_LIBVOICE_NOT_INIT     = static_cast<int32_t>(0x80310801);
constexpr int32_t ERR_LIBVOICE_INITIALIZED  = static_cast<int32_t>(0x80310802);
constexpr int32_t ERR_PORT_INVALID          = static_cast<int32_t>(0x80310804);
constexpr int32_t ERR_ARGUMENT_INVALID      = static_cast<int32_t>(0x80310805);
constexpr int32_t ERR_TOPOLOGY              = static_cast<int32_t>(0x80310807);
constexpr int32_t ERR_RESOURCE_INSUFFICIENT = static_cast<int32_t>(0x80310808);
constexpr int32_t ERR_SERVICE_DETACHED      = static_cast<int32_t>(0x8031080b);
constexpr int32_t ERR_EVENT_QUEUE           = static_cast<int32_t>(0x8031080f);
constexpr int32_t ERR_DEVICE_NOT_PRESENT    = static_cast<int32_t>(0x80310812);

/* refs/.../cellVoice.h:28-32. */
constexpr int32_t APPTYPE_GAME_1MB = 1 << 29;

/* refs/.../cellVoice.h:34-44. CELLVOICE_BITRATE_NULL is ~0u. */
constexpr uint32_t BITRATE_NULL  = 0xffffffffu;
constexpr uint32_t BITRATE_3850  = 3850;
constexpr uint32_t BITRATE_4650  = 4650;
constexpr uint32_t BITRATE_5700  = 5700;
constexpr uint32_t BITRATE_7300  = 7300;
constexpr uint32_t BITRATE_14400 = 14400;
constexpr uint32_t BITRATE_16000 = 16000;
constexpr uint32_t BITRATE_22533 = 22533;

/* refs/.../cellVoice.h:46-55. */
constexpr uint64_t EVENT_DATA_ERROR         = 1u << 0;
constexpr uint64_t EVENT_PORT_ATTACHED      = 1u << 1;
constexpr uint64_t EVENT_PORT_DETACHED      = 1u << 2;
constexpr uint64_t EVENT_SERVICE_ATTACHED   = 1u << 3;
constexpr uint64_t EVENT_SERVICE_DETACHED   = 1u << 4;
constexpr uint64_t EVENT_PORT_WEAK_ATTACHED = 1u << 5;
constexpr uint64_t EVENT_PORT_WEAK_DETACHED = 1u << 6;

/* refs/.../cellVoice.h:57-66. PCM_NULL is ~0u. */
constexpr uint32_t PCM_NULL                  = 0xffffffffu;
constexpr uint32_t PCM_FLOAT                 = 0;
constexpr uint32_t PCM_FLOAT_LITTLE_ENDIAN   = 1;
constexpr uint32_t PCM_SHORT                 = 2;
constexpr uint32_t PCM_SHORT_LITTLE_ENDIAN   = 3;
constexpr uint32_t PCM_INTEGER               = 4;
constexpr uint32_t PCM_INTEGER_LITTLE_ENDIAN = 5;

/* refs/.../cellVoice.h:78-96. */
constexpr uint32_t PORTSTATE_NULL = 0xffffffffu;
constexpr uint32_t PORTTYPE_NULL  = 0xffffffffu;
constexpr uint32_t PORTTYPE_IN_MIC         = 0;
constexpr uint32_t PORTTYPE_IN_PCMAUDIO    = 1;
constexpr uint32_t PORTTYPE_IN_VOICE       = 2;
constexpr uint32_t PORTTYPE_OUT_PCMAUDIO   = 3;
constexpr uint32_t PORTTYPE_OUT_VOICE      = 4;
constexpr uint32_t PORTTYPE_OUT_SECONDARY  = 5;

/* refs/.../cellVoice.h:110-118. */
constexpr uint32_t MAX_PORT = 128;
constexpr uint32_t INVALID_PORT_ID = 0xff;

/* --- guest structures, refs/.../cellVoice.h:120-174 ------------------------
 *
 * CellVoiceInitParam, aligned(32): eventMask 0, version 4, appType 8,
 * reserved 12..31.                                                    :130-136
 * CellVoicePortParam, aligned(64): portType 0, threshold 4, bMute 6,
 * volume 8, union at 12; voice.bitrate at 12; pcmaudio.bufSize at 12 and
 * pcmaudio.format at 16.                                               :146-168
 * CellVoicePCMFormat: numChannels 0, sampleAlignment 1, dataType 4,
 * sampleRate 8 -- so inside the union, at 16/17/20/24.                 :138-144
 * CellVoiceBasePortInfo, aligned(64): portType 0, state 4, numEdge 8,
 * pEdge 12, numByte 16, frameSize 20 (24 bytes).                       :120-128
 *
 * The structs below exist to assert those numbers at compile time; the handlers
 * still read guest memory field by field in big endian.
 */
struct InitParam {
    uint32_t event_mask;   /* 0 */
    uint32_t version;      /* 4 */
    int32_t app_type;      /* 8 */
    uint8_t reserved[32 - 3 * 4]; /* 12..31 */
};
static_assert(sizeof(InitParam) == 32, "CellVoiceInitParam is 32 bytes (cellVoice.h:130)");
static_assert(offsetof(InitParam, event_mask) == 0, "eventMask at 0");
static_assert(offsetof(InitParam, version) == 4, "version at 4");
static_assert(offsetof(InitParam, app_type) == 8, "appType at 8");

struct PortParam {
    uint32_t port_type;    /* 0 */
    uint16_t threshold;    /* 4 */
    uint16_t b_mute;       /* 6 */
    float volume;          /* 8 */
    uint8_t un[16];        /* 12: the three-way union, offsets below */
};
static_assert(sizeof(PortParam) == 28, "CellVoicePortParam is 28 bytes (cellVoice.h:146-168)");
static_assert(offsetof(PortParam, port_type) == 0, "portType at 0");
static_assert(offsetof(PortParam, threshold) == 4, "threshold at 4");
static_assert(offsetof(PortParam, b_mute) == 6, "bMute at 6");
static_assert(offsetof(PortParam, volume) == 8, "volume at 8");
static_assert(offsetof(PortParam, un) == 12, "the union is at 12");

/* Union members, absolute offsets in CellVoicePortParam. */
constexpr uint32_t PARAM_VOICE_BITRATE        = 12; /* union.voice.bitrate     :158 */
constexpr uint32_t PARAM_PCMAUDIO_BUF_SIZE     = 12; /* union.pcmaudio.bufSize  :160 */
constexpr uint32_t PARAM_PCMAUDIO_NUM_CHANNELS = 16; /* format.numChannels      :140 */
constexpr uint32_t PARAM_PCMAUDIO_ALIGNMENT    = 17; /* format.sampleAlignment  :141 */
constexpr uint32_t PARAM_PCMAUDIO_DATA_TYPE    = 20; /* format.dataType (align4):142 */
constexpr uint32_t PARAM_PCMAUDIO_SAMPLE_RATE  = 24; /* format.sampleRate       :143 */

struct BasePortInfo {
    uint32_t port_type;    /* 0 */
    uint32_t state;        /* 4 */
    uint16_t num_edge;     /* 8 */
    uint16_t pad;          /* 10 */
    uint32_t p_edge;       /* 12 */
    uint32_t num_byte;     /* 16 */
    uint32_t frame_size;   /* 20 */
};
static_assert(sizeof(BasePortInfo) == 24, "CellVoiceBasePortInfo is 24 bytes (cellVoice.h:120-128)");
static_assert(offsetof(BasePortInfo, port_type) == 0, "portType at 0");
static_assert(offsetof(BasePortInfo, state) == 4, "state at 4");
static_assert(offsetof(BasePortInfo, num_edge) == 8, "numEdge at 8");
static_assert(offsetof(BasePortInfo, p_edge) == 12, "pEdge at 12");
static_assert(offsetof(BasePortInfo, num_byte) == 16, "numByte at 16");
static_assert(offsetof(BasePortInfo, frame_size) == 20, "frameSize at 20");

/* One voice port, the fields RPCS3's voice_manager::port_t holds
 * (refs/.../cellVoice.h:178-184). */
struct Port {
    uint32_t port_type = PORTTYPE_NULL;
    uint16_t threshold = 0;
    uint16_t b_mute = 0;
    float volume = 0.0f;
    uint32_t bitrate = BITRATE_NULL;      /* union.voice.bitrate */
    uint32_t pcm_buf_size = 0;            /* union.pcmaudio.bufSize */
    uint8_t pcm_num_channels = 0;
    uint8_t pcm_alignment = 0;
    uint32_t pcm_data_type = PCM_NULL;
    uint32_t pcm_sample_rate = 0;
};

/* The module's state: refs/.../cellVoice.h:176-218 (voice_manager). Ports are
 * keyed by the 16-bit id cellVoiceCreatePort hands out; the upper 16 bits of a
 * caller's id are ignored (:196-207). */
struct Manager {
    bool is_init = false;
    uint8_t id_ctr = 0;                       /* :187 */
    uint32_t port_source = 0;                 /* :190 */
    std::map<uint16_t, Port> ports;           /* :192 */
    std::map<uint64_t, std::vector<uint64_t>> queue_keys; /* :193 */
    bool voice_service_started = false;       /* :194 */

    Port* access_port(uint32_t id) {
        /* "Upper 16 bits are ignored" -- cellVoice.h:198. */
        auto it = ports.find(static_cast<uint16_t>(id));
        return it == ports.end() ? nullptr : &it->second;
    }
};

/* --- the two calls cellVoice makes into LV2 -------------------------------
 *
 * RPCS3 creates its notify queue with sys_event_queue_create
 * (refs/.../cellVoice.cpp:99) and delivers events with queue->send (:256, :697,
 * :758). In this runtime those live in runtime/src/lv2/event.cpp, behind these
 * two functions, which cellVoice.cpp implements on top of lv2::create_queue_by_key
 * and lv2::send_event_by_key. A unit test supplies its own, which is why the
 * state machine below needs no scheduler, no guest thread and no game.
 */

/* 0 when a queue with this key already exists (RPCS3's CELL_EEXIST branch,
 * :101-105); otherwise the new queue's guest id. */
uint32_t queue_create(uint64_t key, uint32_t size, const char* name);

/* Deliver {source, data1, data2, data3}. False when no queue has that key
 * (RPCS3's lv2_event_queue::find test at :252, :693, :754). */
bool queue_send(uint64_t key, uint64_t source, uint64_t data1, uint64_t data2, uint64_t data3);

/* --- helpers -------------------------------------------------------------- */

/* Guest memory is only touched when it is mapped: RPCS3 dereferences these
 * pointers unchecked (its comments say so), and a null or unmapped pointer there
 * faults the emulator. Here such an argument is refused with the code the
 * function returns for a bad argument, and the deviation is logged by the
 * handler. No value is ever invented for the caller. */
inline bool readable(uint32_t addr, uint32_t size) {
    return addr && vm::range_mapped(addr, size);
}

inline Port read_port_param(uint32_t arg) {
    Port p;
    p.port_type = vm::read32(arg + 0);
    p.threshold = vm::read16(arg + 4);
    p.b_mute = vm::read16(arg + 6);
    p.volume = std::bit_cast<float>(vm::read32(arg + 8));
    p.bitrate = vm::read32(arg + PARAM_VOICE_BITRATE);
    p.pcm_buf_size = vm::read32(arg + PARAM_PCMAUDIO_BUF_SIZE);
    p.pcm_num_channels = vm::ptr(arg + PARAM_PCMAUDIO_NUM_CHANNELS)[0];
    p.pcm_alignment = vm::ptr(arg + PARAM_PCMAUDIO_ALIGNMENT)[0];
    p.pcm_data_type = vm::read32(arg + PARAM_PCMAUDIO_DATA_TYPE);
    p.pcm_sample_rate = vm::read32(arg + PARAM_PCMAUDIO_SAMPLE_RATE);
    return p;
}

/* --- cellVoiceInit, refs/.../cellVoice.cpp:405-422 ----------------------- */
inline int32_t init(Manager& m, uint32_t p_arg) {
    if (m.is_init) return ERR_LIBVOICE_INITIALIZED;   /* :413-414 */
    if (!p_arg) return ERR_ARGUMENT_INVALID;          /* :416-417 */
    m.is_init = true;                                 /* :419 */
    return 0;                                         /* :421 */
}

/* --- cellVoiceCreateNotifyEventQueue, :79-114 ---------------------------- */
inline int32_t create_notify_event_queue(Manager& m, uint32_t id_out, uint32_t key_out) {
    if (!m.is_init) return ERR_LIBVOICE_NOT_INIT;     /* :87-88 */
    if (!readable(id_out, 4) || !readable(key_out, 8)) return ERR_ARGUMENT_INVALID;

    /* "bruteforcing an available key": ten consecutive keys, queue size 0x40
     * (:95-99). A key already in use is CELL_EEXIST and the loop continues
     * (:101-105); the queue id goes to *id and the key to *key (:108). */
    for (uint64_t i = 0; i < 10; i++) {
        const uint64_t key_value = 0x80004d494f323285ull + i;   /* :98 */
        const uint32_t id = queue_create(key_value, 0x40, "cellVoice");  /* :99 */
        if (!id) continue;
        vm::write32(id_out, id);
        vm::write64(key_out, key_value);
        return 0;
    }
    return ERR_EVENT_QUEUE;                            /* :113 */
}

/* --- cellVoiceCreatePort, :116-194 --------------------------------------- */
inline int32_t create_port(Manager& m, uint32_t port_id_out, uint32_t arg) {
    if (!m.is_init) return ERR_LIBVOICE_NOT_INIT;      /* :124-125 */
    if (!arg || !readable(arg, sizeof(PortParam))) return ERR_ARGUMENT_INVALID; /* :127-128 */
    /* RPCS3 dereferences *portId only after a slot was claimed (:188). Refusing
     * an unwritable output pointer up front keeps the failure from leaving a
     * half-created port behind. */
    if (!readable(port_id_out, 4)) return ERR_ARGUMENT_INVALID;

    const Port p = read_port_param(arg);

    /* Per-port-type validation, :130-168, in this order: the PCM data type
     * range (:135), the voice bitrate exact set (:144-158), the two port types
     * that carry no parameter (:161-165), and the port type itself (:167). */
    switch (p.port_type) {
    case PORTTYPE_IN_PCMAUDIO:
    case PORTTYPE_OUT_PCMAUDIO:
        if (p.pcm_data_type > PCM_INTEGER_LITTLE_ENDIAN) return ERR_ARGUMENT_INVALID;
        break;
    case PORTTYPE_IN_VOICE:
    case PORTTYPE_OUT_VOICE:
        switch (p.bitrate) {
        case BITRATE_3850:
        case BITRATE_4650:
        case BITRATE_5700:
        case BITRATE_7300:
        case BITRATE_14400:
        case BITRATE_16000:
        case BITRATE_22533:
            break;
        default:
            return ERR_ARGUMENT_INVALID;
        }
        break;
    case PORTTYPE_IN_MIC:
    case PORTTYPE_OUT_SECONDARY:
        break;
    default:
        return ERR_ARGUMENT_INVALID;
    }

    if (m.ports.size() >= MAX_PORT) return ERR_RESOURCE_INSUFFICIENT;  /* :170-171 */

    /* Id: bits [0,7] are the creation counter modulo 0xa0, bits [8,15] a
     * non-zero counter; the rest is zero and ignored (:173-183). The id is the
     * map key, and it is what *portId receives (:188). */
    m.id_ctr = static_cast<uint8_t>((m.id_ctr + 1) % 0xa0);            /* :176 */
    constexpr uint32_t min_value = 1;                                   /* :179 */
    for (uint32_t ctr2 = min_value; ctr2 < MAX_PORT + min_value; ctr2++) {
        const uint16_t id = static_cast<uint16_t>((ctr2 << 8) | m.id_ctr);
        if (m.ports.count(id)) continue;                               /* :183 */
        m.ports[id] = p;                                               /* :187 */
        vm::write32(port_id_out, id);                                  /* :188 */
        return 0;                                                      /* :189 */
    }
    /* RPCS3 throws here (:193); with MAX_PORT slots and the capacity check above
     * it cannot be reached, and an explicit refusal is the honest answer. */
    return ERR_RESOURCE_INSUFFICIENT;
}

/* --- cellVoiceDeletePort, :196-211 --------------------------------------- */
inline int32_t delete_port(Manager& m, uint32_t port_id) {
    if (!m.is_init) return ERR_LIBVOICE_NOT_INIT;      /* :204-205 */
    if (m.ports.erase(static_cast<uint16_t>(port_id)) == 0) return ERR_TOPOLOGY;  /* :207-208 */
    return 0;                                          /* :210 */
}

/* --- cellVoiceConnectIPortToOPort, :55-77 --------------------------------
 * An input port is anything below OUT_PCMAUDIO, an output port anything above
 * IN_VOICE (:68-74); a port that does not exist is the same topology error. */
inline int32_t connect(Manager& m, uint32_t ips, uint32_t ops) {
    if (!m.is_init) return ERR_LIBVOICE_NOT_INIT;      /* :63-64 */
    const Port* iport = m.access_port(ips);
    if (!iport || iport->port_type >= PORTTYPE_OUT_PCMAUDIO) return ERR_TOPOLOGY;  /* :66-69 */
    const Port* oport = m.access_port(ops);
    if (!oport || oport->port_type <= PORTTYPE_IN_VOICE) return ERR_TOPOLOGY;      /* :71-74 */
    return 0;                                          /* :76 */
}

/* --- cellVoiceDisconnectIPortFromOPort, :213-235 ------------------------- */
inline int32_t disconnect(Manager& m, uint32_t ips, uint32_t ops) {
    if (!m.is_init) return ERR_LIBVOICE_NOT_INIT;      /* :221-222 */
    const Port* iport = m.access_port(ips);
    if (!iport || iport->port_type >= PORTTYPE_OUT_PCMAUDIO) return ERR_TOPOLOGY;  /* :224-227 */
    const Port* oport = m.access_port(ops);
    if (!oport || oport->port_type <= PORTTYPE_IN_VOICE) return ERR_TOPOLOGY;      /* :229-232 */
    return 0;                                          /* :234 */
}

/* --- cellVoiceStart, :705-717 on top of VoiceStart, :686-703 -------------
 * Starting an already-started service is CELL_OK and sends nothing (:688-689). */
inline void deliver(Manager& m, uint64_t event) {
    for (auto& key_pair : m.queue_keys) {                     /* :691 */
        for (const auto& source : key_pair.second) {          /* :695 */
            queue_send(key_pair.first, source, event, 0, 0);  /* :697, :758 */
        }
    }
}

inline int32_t start(Manager& m) {
    if (!m.is_init) return ERR_LIBVOICE_NOT_INIT;      /* :713-714 */
    if (m.voice_service_started) return 0;             /* :688-689 */
    m.voice_service_started = true;
    deliver(m, EVENT_SERVICE_ATTACHED);                /* :697 */
    return 0;                                          /* :702 */
}

/* --- cellVoiceStop, :738-764 --------------------------------------------- */
inline int32_t stop(Manager& m) {
    if (!m.is_init) return ERR_LIBVOICE_NOT_INIT;      /* :746-747 */
    if (!m.voice_service_started) return 0;            /* :749-750 */
    m.voice_service_started = false;                   /* :749 */
    deliver(m, EVENT_SERVICE_DETACHED);                /* :758 */
    return 0;                                          /* :763 */
}

/* --- cellVoiceEnd, :237-266 ----------------------------------------------
 * Ends the service (a DETACHED event when it was started), then resets every
 * piece of state including is_init (:248-263). The reset clears the ports and
 * the queue keys, so a later Init starts from nothing. */
inline int32_t end(Manager& m) {
    if (!m.is_init) return ERR_LIBVOICE_NOT_INIT;      /* :245-246 */
    if (m.voice_service_started) {                     /* :248 */
        m.voice_service_started = false;
        deliver(m, EVENT_SERVICE_DETACHED);            /* :256 */
    }
    m.id_ctr = 0;                                      /* reset(), :41-47 */
    m.port_source = 0;
    m.ports.clear();
    m.queue_keys.clear();
    m.is_init = false;                                 /* :263 */
    return 0;                                          /* :265 */
}

/* --- cellVoiceGetBitRate, :268-291 --------------------------------------- */
inline int32_t get_bit_rate(Manager& m, uint32_t port_id, uint32_t bitrate_out) {
    if (!m.is_init) return ERR_LIBVOICE_NOT_INIT;      /* :276-277 */
    /* RPCS3 writes a constant first, "meaning unknown" (:281-282), then
     * overwrites it only for a voice port; the topology error leaves the
     * constant behind, which is observable, so it is written here too. No
     * nullptr check in RPCS3 (:279) -- an unmapped pointer is refused instead
     * of faulting. */
    if (!readable(bitrate_out, 4)) return ERR_ARGUMENT_INVALID;
    vm::write32(bitrate_out, 0x4f323285u);             /* :282 */
    const Port* port = m.access_port(port_id);
    if (!port || (port->port_type != PORTTYPE_IN_VOICE && port->port_type != PORTTYPE_OUT_VOICE))
        return ERR_TOPOLOGY;                           /* :286-287 */
    vm::write32(bitrate_out, port->bitrate);           /* :289 */
    return 0;                                          /* :290 */
}

/* --- cellVoiceGetPortInfo, :333-358 -------------------------------------- */
inline int32_t get_port_info(Manager& m, uint32_t port_id, uint32_t info) {
    if (!m.is_init) return ERR_LIBVOICE_NOT_INIT;      /* :341-342 */
    const Port* port = m.access_port(port_id);
    if (!port) return ERR_TOPOLOGY;                    /* :346-347 */
    if (!m.voice_service_started) return ERR_SERVICE_DETACHED;  /* :349-350 */
    /* Only portType is written; state, numEdge, pEdge, numByte and frameSize
     * are left as the caller had them, which is what RPCS3 does ("TODO",
     * :352-356). Zeroing them would be a field the console never wrote. */
    if (!readable(info, 4)) return ERR_ARGUMENT_INVALID;
    vm::write32(info + offsetof(BasePortInfo, port_type), port->port_type);  /* :353 */
    return 0;                                          /* :357 */
}

/* --- cellVoiceSetPortAttr, :646-664 --------------------------------------
 * No attribute is stored: the console has no microphone attached here, and
 * RPCS3 answers exactly that, as a raw (not-an-error) code (:663). */
inline int32_t set_port_attr(Manager& m, uint32_t port_id, uint32_t /*attr*/, uint32_t /*attr_value*/) {
    if (!m.is_init) return ERR_LIBVOICE_NOT_INIT;      /* :654-655 */
    if (!m.access_port(port_id)) return ERR_TOPOLOGY;  /* :657-660 */
    return ERR_DEVICE_NOT_PRESENT;                     /* :663 */
}

/* cellVoiceGetPortAttr, refs/.../cellVoice.cpp:313-331, and
 * cellVoiceGetSignalState, :360-378, are the SAME body as SetPortAttr: validate
 * is_init, validate the port, then report the detached microphone (:330, :377).
 * Kept as one function with three names rather than three copies, so the day the
 * reference changes, one place changes. */
inline int32_t get_port_attr(Manager& m, uint32_t port_id, uint32_t attr, uint32_t attr_value) {
    return set_port_attr(m, port_id, attr, attr_value);
}

/* --- cellVoiceReadFromOPort, :855-876 ------------------------------------
 * An output port yields no data offline: *size is set to 0 and the buffer is
 * untouched (:872-875). An input port is a topology error (:869-870). */
inline int32_t read_from_o_port(Manager& m, uint32_t ops, uint32_t /*data*/, uint32_t size_out) {
    if (!m.is_init) return ERR_LIBVOICE_NOT_INIT;      /* :864-865 */
    const Port* port = m.access_port(ops);
    if (!port || port->port_type <= PORTTYPE_IN_VOICE) return ERR_TOPOLOGY;  /* :867-870 */
    if (size_out && vm::range_mapped(size_out, 4)) vm::write32(size_out, 0);  /* :872-873 */
    return 0;                                          /* :875 */
}

/* --- cellVoiceWriteToIPort, :798-815 -------------------------------------
 * Accepts a frame on an input port and reports CELL_OK. RPCS3 does not touch
 * *size here (:813-814), so neither does this. */
inline int32_t write_to_i_port(Manager& m, uint32_t ips, uint32_t /*data*/, uint32_t /*size*/) {
    if (!m.is_init) return ERR_LIBVOICE_NOT_INIT;      /* :806-807 */
    const Port* port = m.access_port(ips);
    if (!port || port->port_type >= PORTTYPE_OUT_PCMAUDIO) return ERR_TOPOLOGY;  /* :809-812 */
    return 0;                                          /* :814 */
}

}  // namespace voice
}  // namespace hle

#endif
