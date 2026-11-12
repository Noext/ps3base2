/* ps3base -- the cellAudio (libaudio) state machine, in a header so the unit
 * test drives exactly the code the import runs.
 *
 * Reference: refs/rpcs3/rpcs3/Emu/Cell/Modules/cellAudio.h   (codes, structures)
 *            refs/rpcs3/rpcs3/Emu/Cell/Modules/cellAudio.cpp (behaviour)
 *
 * Red Dead Redemption imports seven functions (out-rdr/ppu/table.cpp:77122-77128):
 * cellAudioInit, cellAudioPortClose, cellAudioPortStop, cellAudioGetPortConfig,
 * cellAudioPortStart, cellAudioQuit, cellAudioPortOpen. gta-sa imports
 * cellAudioAddData as well (out-gta/ppu/table.cpp), which used to resolve
 * nowhere and get a silent CELL_OK from the dispatcher; it is implemented here.
 * Every remaining function RPCS3 registers is implemented too, so no import can
 * fall through to a generic answer.
 *
 * The point of this file is that a title reads the SAME things the console
 * would: the exact error code for each rejected argument, the exact
 * CellAudioPortConfig bytes (readIndexAddr at 0, status at 4, nChannel at 8,
 * nBlock at 16, portSize at 24, portAddr at 28 -- see the static_asserts), the
 * exact port sizes, and a read index that actually advances while the mixer
 * runs.
 *
 * Guest memory is only touched when it is mapped: RPCS3 dereferences these
 * pointers unchecked, and a null or unmapped pointer there faults the emulator.
 * Here such an argument is refused with the code the function returns for a bad
 * argument, and the handler logs the deviation. No value is invented.
 */
#ifndef PS3BASE_HLE_AUDIO_H
#define PS3BASE_HLE_AUDIO_H

#include "runtime/memory.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace hle {
namespace audio {

/* --- error codes, refs/.../cellAudio.h:14-31 ----------------------------- */
constexpr int32_t ERR_ALREADY_INIT       = static_cast<int32_t>(0x80310701);
constexpr int32_t ERR_AUDIOSYSTEM        = static_cast<int32_t>(0x80310702);
constexpr int32_t ERR_NOT_INIT           = static_cast<int32_t>(0x80310703);
constexpr int32_t ERR_PARAM              = static_cast<int32_t>(0x80310704);
constexpr int32_t ERR_PORT_FULL          = static_cast<int32_t>(0x80310705);
constexpr int32_t ERR_PORT_ALREADY_RUN   = static_cast<int32_t>(0x80310706);
constexpr int32_t ERR_PORT_NOT_OPEN      = static_cast<int32_t>(0x80310707);
constexpr int32_t ERR_PORT_NOT_RUN       = static_cast<int32_t>(0x80310708);
constexpr int32_t ERR_TRANS_EVENT        = static_cast<int32_t>(0x80310709);
constexpr int32_t ERR_PORT_OPEN          = static_cast<int32_t>(0x8031070a);
constexpr int32_t ERR_SHAREDMEMORY       = static_cast<int32_t>(0x8031070b);
constexpr int32_t ERR_MUTEX              = static_cast<int32_t>(0x8031070c);
constexpr int32_t ERR_EVENT_QUEUE        = static_cast<int32_t>(0x8031070d);
constexpr int32_t ERR_AUDIOSYSTEM_NOT_FOUND = static_cast<int32_t>(0x8031070e);
constexpr int32_t ERR_TAG_NOT_FOUND      = static_cast<int32_t>(0x8031070f);

/* --- constants, refs/.../cellAudio.h:33-105 ------------------------------ */
constexpr uint32_t BLOCK_SAMPLES          = 256;   /* CELL_AUDIO_BLOCK_SAMPLES */
constexpr uint32_t BLOCK_8                = 8;
constexpr uint32_t BLOCK_16               = 16;
constexpr uint32_t BLOCK_32               = 32;
constexpr uint32_t PORT_2CH               = 2;
constexpr uint32_t PORT_8CH               = 8;
constexpr uint32_t MAX_PORT               = 4;     /* CELL_AUDIO_MAX_PORT   */
constexpr uint32_t MAX_PORT_2             = 8;     /* CELL_AUDIO_MAX_PORT_2 */
constexpr uint32_t PORT_COUNT             = 8;     /* AUDIO_PORT_COUNT      */

constexpr uint64_t PORTATTR_BGM            = 0x0000000000000010ull;
constexpr uint64_t PORTATTR_INITLEVEL      = 0x0000000000001000ull;
constexpr uint64_t PORTATTR_OUT_SECONDARY  = 0x0000000000000001ull;
constexpr uint64_t PORTATTR_OUT_NO_ROUTE   = 0x0000000000100000ull;
constexpr uint64_t PORTATTR_OUT_PERSONAL_0 = 0x0000000001000000ull;
constexpr uint64_t PORTATTR_OUT_PERSONAL_1 = 0x0000000002000000ull;
constexpr uint64_t PORTATTR_OUT_PERSONAL_2 = 0x0000000004000000ull;
constexpr uint64_t PORTATTR_OUT_PERSONAL_3 = 0x0000000008000000ull;

constexpr uint32_t STATUS_CLOSE = 0x1010;  /* CELL_AUDIO_STATUS_CLOSE */
constexpr uint32_t STATUS_READY = 1;
constexpr uint32_t STATUS_RUN   = 2;

constexpr uint64_t EVENT_MIX                = 0;         /* CELL_AUDIO_EVENT_MIX */
constexpr uint64_t EVENTFLAG_BEFOREMIX      = 0x80000000ull;
constexpr uint64_t EVENTFLAG_DECIMATE_2     = 0x08000000ull;
constexpr uint64_t EVENTFLAG_DECIMATE_4     = 0x10000000ull;
constexpr uint64_t EVENTFLAG_HEADPHONE      = 0x20000000ull;
constexpr uint64_t EVENTFLAG_NOMIX          = 0x40000000ull;

constexpr uint32_t CREATEEVENTFLAG_SPU = 0x00000001;

/* The brute-forced notify queue key base and the number of candidates:
 * cellAudio.cpp:2050-2075. */
constexpr uint64_t NOTIFY_KEY_BASE = 0x80004d494f323221ull;
constexpr uint32_t MAX_NOTIFY_QUEUES = 64;

/* AUDIO_BLOCK_SAMPLES * sizeof(f32) = one frame of one channel: 256*4. */
constexpr uint32_t BLOCK_BYTES_PER_CHANNEL = BLOCK_SAMPLES * 4;

/* --- guest structures -----------------------------------------------------
 *
 * CellAudioPortParam (refs/.../cellAudio.h:88-94):
 *     be_t<u64> nChannel;  0
 *     be_t<u64> nBlock;    8
 *     be_t<u64> attr;     16
 *     be_t<float> level;  24
 * -> 32 bytes.
 *
 * CellAudioPortConfig (refs/.../cellAudio.h:96-104):
 *     vm::bptr<u64> readIndexAddr;  0   (a vm pointer is a 32-bit guest address!)
 *     be_t<u32>     status;         4
 *     be_t<u64>     nChannel;       8
 *     be_t<u64>     nBlock;        16
 *     be_t<u32>     portSize;      24
 *     be_t<u32>     portAddr;      28
 * -> 32 bytes.
 *
 * vm::bptr's address type is u32 (refs/rpcs3/rpcs3/Emu/Memory/vm_ptr.h:20,29),
 * which is what puts status at 4 and not at 8. The structs below exist to
 * assert those numbers at compile time; the handlers read and write guest
 * memory field by field in big endian.
 */
struct PortParam {
    uint64_t n_channel;  /* 0 */
    uint64_t n_block;    /* 8 */
    uint64_t attr;       /* 16 */
    float level;         /* 24 */
};
static_assert(sizeof(PortParam) == 32, "CellAudioPortParam is 32 bytes");
static_assert(offsetof(PortParam, n_channel) == 0, "nChannel at 0");
static_assert(offsetof(PortParam, n_block) == 8, "nBlock at 8");
static_assert(offsetof(PortParam, attr) == 16, "attr at 16");
static_assert(offsetof(PortParam, level) == 24, "level at 24");

struct PortConfig {
    uint32_t read_index_addr; /* 0 */
    uint32_t status;          /* 4 */
    uint64_t n_channel;       /* 8 */
    uint64_t n_block;         /* 16 */
    uint32_t port_size;       /* 24 */
    uint32_t port_addr;       /* 28 */
};
static_assert(sizeof(PortConfig) == 32, "CellAudioPortConfig is 32 bytes");
static_assert(offsetof(PortConfig, read_index_addr) == 0, "readIndexAddr at 0");
static_assert(offsetof(PortConfig, status) == 4, "status at 4");
static_assert(offsetof(PortConfig, n_channel) == 8, "nChannel at 8");
static_assert(offsetof(PortConfig, n_block) == 16, "nBlock at 16");
static_assert(offsetof(PortConfig, port_size) == 24, "portSize at 24");
static_assert(offsetof(PortConfig, port_addr) == 28, "portAddr at 28");

/* --- port state, refs/.../cellAudio.h:154-222 (audio_port) ---------------- */
enum class PortState : uint32_t { closed, opened, started };

struct Port {
    PortState state = PortState::closed;
    uint32_t number = 0;
    uint32_t server_index = 0;
    uint32_t addr = 0;        /* guest address of the block buffer */
    uint32_t index = 0;       /* guest address of the read index (be u64) */
    uint32_t num_channels = 0;
    uint32_t num_blocks = 0;
    uint64_t attr = 0;
    uint32_t size = 0;        /* exact portSize: ch*blocks*256*4 */
    uint32_t mapped = 0;      /* bytes actually mapped (>= size, 64K aligned) */
    uint64_t cur_pos = 0;
    uint64_t global_counter = 0;
    uint64_t active_counter = 0;
    uint64_t timestamp = 0;
    float level = 0.0f;
    /* Index-write bookkeeping for the log: how many periods have advanced this
     * port and the last value published. Not part of the ABI. */
    uint64_t index_writes = 0;
    uint64_t index_last = 0;
};

struct KeyInfo {
    uint64_t key = 0;
    uint64_t source = 0;
    uint32_t flags = 0;
    uint64_t start_period = 0;
    uint64_t ack_timestamp = 0;
};

struct Manager {
    bool is_init = false;
    uint32_t index_area = 0;      /* 64 KiB shared area holding the indices */
    uint32_t index_area_size = 0;
    uint32_t free_count = 0;
    uint32_t free_ports[PORT_COUNT] = {};
    uint32_t free_indices[PORT_COUNT] = {};
    Port ports[PORT_COUNT];
    std::vector<KeyInfo> keys;
    uint32_t key_count = 0;
    uint64_t counter = 0;         /* global period counter, m_counter */
    uint64_t event_period = 0;    /* period counter the events are keyed on */
};

/* --- the calls into the host runtime -------------------------------------
 *
 * cellAudio.cpp implements them on top of vm:: and lv2::; tests/cellAudio.cpp
 * supplies its own, which is why the state machine below needs no scheduler,
 * no audio device and no game.
 */

/* A 64 KiB, zeroed, 64 KiB-aligned guest area for the sixteen-byte index slots.
 * 0 on failure. */
uint32_t alloc_index_area(uint32_t bytes);
void free_index_area(uint32_t addr);

/* A zeroed port buffer of at least @bytes, 64 KiB aligned. 0 on failure. */
uint32_t alloc_port_buffer(uint32_t bytes);
void free_port_buffer(uint32_t addr);

/* Publish the read index as a big-endian u64 at @index_addr. */
void write_index(uint32_t index_addr, uint64_t value);

/* The guest clock, microseconds. lv2::system_time_us(), which is what
 * cellAudioGetPortTimestamp reports. */
uint64_t guest_time_us();

/* sys_event_queue_create with an explicit depth. 0 when the key is taken
 * (RPCS3's CELL_EEXIST), otherwise the new queue's guest id. */
uint32_t queue_create(uint64_t key, uint32_t depth, const char* name);

/* queue->send(source, d1, d2, d3). False when no queue has that key. */
bool queue_send(uint64_t key, uint64_t source, uint64_t d1, uint64_t d2, uint64_t d3);

/* lv2_event_queue::find(key) != null. Separate from queue_send because
 * cellAudioSetNotifyEventQueue only looks the queue up -- it must not deliver
 * anything into the title's queue at registration time (cellAudio.cpp:2162-2166).
 */
bool queue_exists(uint64_t key);

/* The caller's process id, for the event source word (RPCS3 cellAudio.cpp:2242:
 * ((process_getpid() << 32) + lv2_event_port::id_base + key_count * id_step)). */
uint32_t process_pid();

/* The title's SDK version: the port count (4 or 8, cellAudio.cpp:1590) and the
 * notify queue depth (:2055-2057) both depend on it. */
uint32_t sdk_version();

/* --- helpers -------------------------------------------------------------- */
inline bool readable(uint32_t addr, uint32_t size) {
    return addr && vm::range_mapped(addr, size);
}

inline PortParam read_port_param(uint32_t ea) {
    PortParam p{};
    p.n_channel = vm::read64(ea + offsetof(PortParam, n_channel));
    p.n_block = vm::read64(ea + offsetof(PortParam, n_block));
    p.attr = vm::read64(ea + offsetof(PortParam, attr));
    const uint32_t lv = vm::read32(ea + offsetof(PortParam, level));
    std::memcpy(&p.level, &lv, 4);
    return p;
}

inline void write_port_config(uint32_t ea, const PortConfig& c) {
    vm::write32(ea + offsetof(PortConfig, read_index_addr), c.read_index_addr);
    vm::write32(ea + offsetof(PortConfig, status), c.status);
    vm::write64(ea + offsetof(PortConfig, n_channel), c.n_channel);
    vm::write64(ea + offsetof(PortConfig, n_block), c.n_block);
    vm::write32(ea + offsetof(PortConfig, port_size), c.port_size);
    vm::write32(ea + offsetof(PortConfig, port_addr), c.port_addr);
}

inline uint32_t port_count_for_sdk(uint32_t sdk) {
    return sdk <= 0x35ffff ? MAX_PORT : MAX_PORT_2;
}

/* The port buffer size RPCS3 computes, cellAudio.cpp:1749:
 * max(1, num_channels) * num_blocks * AUDIO_BUFFER_SAMPLES * sizeof(f32). */
inline uint32_t port_size_for(uint64_t channels, uint64_t blocks) {
    return static_cast<uint32_t>(std::max<uint64_t>(1, channels) * blocks * BLOCK_BYTES_PER_CHANNEL);
}

/* RPCS3 zeroes max(0x10000, align(size, 0x10000)) of the mapping
 * (cellAudio.cpp:1276) and maps 0x40000-aligned shared memory (:1315). */
inline uint32_t mapped_size_for(uint32_t size) {
    const uint32_t rounded = (size + 0xffffu) & ~0xffffu;
    return std::max<uint32_t>(0x10000u, rounded);
}

/* --- cellAudioInit / Quit ------------------------------------------------
 * refs/.../cellAudio.cpp:1520-1616 / :1617-1651. The shared index area is the
 * only allocation that is global; a second Init is ALREADY_INIT (:1538-1541),
 * and Quit closes every open port and releases it (:1637-1650). */
inline int32_t init(Manager& m) {
    if (m.is_init) return ERR_ALREADY_INIT;
    m.index_area_size = 0x10000;
    const uint32_t area = alloc_index_area(m.index_area_size);
    if (!area) return ERR_SHAREDMEMORY;
    m.index_area = area;

    const uint32_t count = port_count_for_sdk(sdk_version());
    m.free_count = count;
    for (uint32_t i = 0; i < PORT_COUNT; i++) {
        m.ports[i] = Port{};
        m.ports[i].state = PortState::closed;
        m.ports[i].number = i;
        /* Closed ports keep their slot in the index mapping, cellAudio.cpp:1604. */
        m.ports[i].index = m.index_area + m.ports[i].server_index * 16;
        if (i < count) {
            m.free_ports[i] = count - i - 1;
            m.free_indices[i] = count - i;
        }
    }
    m.keys.clear();
    m.key_count = 0;
    m.counter = 0;
    m.event_period = 0;
    m.is_init = true;
    return 0;
}

inline void close_port(Manager& m, Port& p);

inline int32_t quit(Manager& m) {
    if (!m.is_init) return ERR_NOT_INIT;
    for (auto& p : m.ports) {
        if (p.state != PortState::closed) close_port(m, p);
    }
    /* NOTE: event queues are not destroyed here; RPCS3 says so at :1650. */
    m.keys.clear();
    if (m.index_area) free_index_area(m.index_area);
    m.index_area = 0;
    m.index_area_size = 0;
    m.is_init = false;
    return 0;
}

/* The free-list order is RPCS3's: open_port takes free_indices[--free_count],
 * so the first port opened with an 8-port console has server_index 1 (:1606-1610,
 * :1257-1272). Only the resulting address is guest-visible, but it is what the
 * console hands out, so it is what this hands out. */
inline Port* open_port(Manager& m) {
    if (!m.free_count) return nullptr;
    const uint32_t index = --m.free_count;
    Port& p = m.ports[m.free_ports[index]];
    p.server_index = m.free_indices[index];
    p.state = PortState::opened;
    return &p;
}

inline void close_port(Manager& m, Port& p) {
    (void)m;
    p.state = PortState::closed;
    if (p.mapped && p.addr) free_port_buffer(p.addr);
    p.mapped = 0;
    p.addr = 0;
    p.index_writes = 0;
    p.index_last = 0;
    m.free_ports[m.free_count] = p.number;
    m.free_indices[m.free_count] = p.server_index;
    m.free_count++;
}

/* --- cellAudioPortOpen(portParam, portNum) -------------------------------
 * refs/.../cellAudio.cpp:1652-1776. Order: NOT_INIT, then PARAM for a null
 * pointer (:1673-1676), then the channel count (:1680-1684 -- 2, 8 or 0), then
 * the block count (:1686-1692 -- 2, 4, 8, 16 or 32), then PORT_FULL (:1742).
 * Attributes are not rejected: each unsupported one is a TODO in RPCS3
 * (:1695-1731), so they are logged and ignored here. */
inline int32_t port_open(Manager& m, uint32_t param_ea, uint32_t num_ea, uint64_t* attr_out = nullptr,
                         float* level_out = nullptr, uint32_t* size_out = nullptr) {
    if (!m.is_init) return ERR_NOT_INIT;
    if (!param_ea || !num_ea) return ERR_PARAM;
    if (!readable(param_ea, sizeof(PortParam)) || !readable(num_ea, 4)) return ERR_PARAM;

    const PortParam param = read_port_param(param_ea);
    if (param.n_channel != PORT_2CH && param.n_channel != PORT_8CH && param.n_channel != 0) return ERR_PARAM;
    if (param.n_block != 2 && param.n_block != 4 && param.n_block != BLOCK_8 && param.n_block != BLOCK_16 &&
        param.n_block != BLOCK_32) {
        return ERR_PARAM;
    }

    Port* p = open_port(m);
    if (!p) return ERR_PORT_FULL;

    p->num_channels = static_cast<uint32_t>(param.n_channel);
    p->num_blocks = static_cast<uint32_t>(param.n_block);
    p->attr = param.attr;
    p->size = port_size_for(param.n_channel, param.n_block);
    p->mapped = mapped_size_for(p->size);
    p->cur_pos = 0;
    p->global_counter = m.counter;
    p->active_counter = 0;
    p->timestamp = guest_time_us();
    /* cellAudio.cpp:1756-1763: level comes from the parameter only when
     * CELL_AUDIO_PORTATTR_INITLEVEL is set, else 1.0. */
    p->level = (param.attr & PORTATTR_INITLEVEL) ? param.level : 1.0f;

    p->addr = alloc_port_buffer(p->mapped);
    if (!p->addr) {
        close_port(m, *p);
        return ERR_SHAREDMEMORY;
    }
    p->index = m.index_area + p->server_index * 16;
    write_index(p->index, 0);

    vm::write32(num_ea, p->number);
    if (attr_out) *attr_out = param.attr;
    if (level_out) *level_out = p->level;
    if (size_out) *size_out = p->size;
    return 0;
}

/* --- cellAudioGetPortConfig(portNum, portConfig) -------------------------
 * refs/.../cellAudio.cpp:1778-1821. Order: NOT_INIT, PARAM for null or
 * portNum >= AUDIO_PORT_COUNT (:1789-1792), then the three status values
 * 0x1010 / 1 / 2 (:1797-1811), then the port's own numbers. portSize is the
 * EXACT size, not the rounded mapping, and portAddr is the real guest buffer. */
inline int32_t get_port_config(Manager& m, uint32_t port_num, uint32_t cfg_ea) {
    if (!m.is_init) return ERR_NOT_INIT;
    if (!cfg_ea || port_num >= PORT_COUNT) return ERR_PARAM;
    if (!readable(cfg_ea, sizeof(PortConfig))) return ERR_PARAM;

    const Port& p = m.ports[port_num];
    PortConfig c{};
    c.read_index_addr = p.index;
    switch (p.state) {
    case PortState::closed:  c.status = STATUS_CLOSE; break;
    case PortState::opened:  c.status = STATUS_READY; break;
    case PortState::started: c.status = STATUS_RUN;   break;
    default: return ERR_AUDIOSYSTEM;
    }
    c.n_channel = p.num_channels;
    c.n_block = p.num_blocks;
    c.port_size = p.size;
    c.port_addr = p.addr;
    write_port_config(cfg_ea, c);
    return 0;
}

/* --- cellAudioPortStart(portNum) ----------------------------------------
 * refs/.../cellAudio.cpp:1823-1861. Order: NOT_INIT, PARAM for portNum >= 8,
 * then PORT_NOT_OPEN / PORT_ALREADY_RUN / OK; starting an opened port zeroes
 * its whole buffer (:1850-1851). */
inline int32_t port_start(Manager& m, uint32_t port_num) {
    if (!m.is_init) return ERR_NOT_INIT;
    if (port_num >= PORT_COUNT) return ERR_PARAM;
    Port& p = m.ports[port_num];
    switch (p.state) {
    case PortState::closed:  return ERR_PORT_NOT_OPEN;
    case PortState::started: return ERR_PORT_ALREADY_RUN;
    case PortState::opened:  break;
    }
    if (!p.addr || !p.size || !vm::range_mapped(p.addr, p.size)) return ERR_PORT_NOT_OPEN;
    std::memset(vm::ptr(p.addr), 0, p.size);
    p.state = PortState::started;
    p.cur_pos = 0;
    p.global_counter = m.counter;
    p.timestamp = guest_time_us();
    write_index(p.index, 0);
    return 0;
}

/* --- cellAudioPortStop(portNum) -------------------------------------------------
 * refs/.../cellAudio.cpp:1900-1927: NOT_INIT, PARAM, then
 * PORT_NOT_RUN for closed OR opened and OK only for started. */
inline int32_t port_stop(Manager& m, uint32_t port_num) {
    if (!m.is_init) return ERR_NOT_INIT;
    if (port_num >= PORT_COUNT) return ERR_PARAM;
    Port& p = m.ports[port_num];
    if (p.state == PortState::started) {
        p.state = PortState::opened;
        return 0;
    }
    return ERR_PORT_NOT_RUN;
}

/* --- cellAudioPortClose(portNum) -------------------------------------------------
 * refs/.../cellAudio.cpp:1863-1898: NOT_INIT, PARAM, PORT_NOT_OPEN when the
 * port is already closed, else the close. */
inline int32_t port_close(Manager& m, uint32_t port_num) {
    if (!m.is_init) return ERR_NOT_INIT;
    if (port_num >= PORT_COUNT) return ERR_PARAM;
    Port& p = m.ports[port_num];
    if (p.state == PortState::closed) return ERR_PORT_NOT_OPEN;
    close_port(m, p);
    return 0;
}

/* --- cellAudioGetPortTimestamp(portNum, tag, stamp) ----------------------
 * refs/.../cellAudio.cpp:1929-1968. NOT_INIT, PARAM, PORT_NOT_OPEN,
 * TAG_NOT_FOUND when global_counter < tag (:1955-1958), else the guest time of
 * the tag: stored timestamp - (global_counter - tag) * block period (:1960-1966).
 * The block period is 256 samples at 48 kHz = 5333 us (cellAudio.cpp:1006,
 * AUDIO_BUFFER_SAMPLES * 1e6 / sampling rate). */
inline int32_t get_port_timestamp(Manager& m, uint32_t port_num, uint64_t tag, uint32_t stamp_ea) {
    if (!m.is_init) return ERR_NOT_INIT;
    if (port_num >= PORT_COUNT) return ERR_PARAM;
    const Port& p = m.ports[port_num];
    if (p.state == PortState::closed) return ERR_PORT_NOT_OPEN;
    if (p.global_counter < tag) return ERR_TAG_NOT_FOUND;
    if (stamp_ea && readable(stamp_ea, 8)) {
        const uint64_t delta = (p.global_counter - tag) * 5333ull;
        vm::write64(stamp_ea, p.timestamp > delta ? p.timestamp - delta : 0);
    }
    return 0;
}

/* --- cellAudioGetPortBlockTag(portNum, blockNo, tag) --------------------
 * refs/.../cellAudio.cpp:1970-2004: NOT_INIT, PARAM, PORT_NOT_OPEN,
 * PARAM when blockNo >= num_blocks, else global_counter + blockNo - cur_pos. */
inline int32_t get_port_block_tag(Manager& m, uint32_t port_num, uint64_t block_no, uint32_t tag_ea) {
    if (!m.is_init) return ERR_NOT_INIT;
    if (port_num >= PORT_COUNT) return ERR_PARAM;
    const Port& p = m.ports[port_num];
    if (p.state == PortState::closed) return ERR_PORT_NOT_OPEN;
    if (block_no >= p.num_blocks) return ERR_PARAM;
    if (tag_ea && readable(tag_ea, 8)) vm::write64(tag_ea, p.global_counter + block_no - p.cur_pos);
    return 0;
}

/* --- cellAudioCreateNotifyEventQueue / Ex -------------------------------
 * refs/.../cellAudio.cpp:2050-2105. Sixteen-byte key base, 64 candidates, the
 * depth is 2 on an SDK <= 0x35ffff once cellAudio is initialised, else 8
 * (:2055-2057), and the first key that creates is the answer. */
inline int32_t create_notify_event_queue(Manager& m, uint32_t id_ea, uint32_t key_ea, uint32_t flags) {
    if (flags & ~CREATEEVENTFLAG_SPU) return ERR_PARAM;   /* :2094-2097 */
    if (!id_ea || !key_ea) return ERR_PARAM;
    if (!readable(id_ea, 4) || !readable(key_ea, 8)) return ERR_PARAM;
    const uint32_t depth = (m.is_init && sdk_version() <= 0x35ffff) ? 2 : 8;
    for (uint32_t i = 0; i < MAX_NOTIFY_QUEUES; i++) {
        const uint64_t key = NOTIFY_KEY_BASE + i;
        const uint32_t id = queue_create(key, depth, "cellAudio notify");
        if (!id) continue;                                  /* CELL_EEXIST */
        vm::write32(id_ea, id);
        vm::write64(key_ea, key);
        return 0;
    }
    return ERR_EVENT_QUEUE;
}

/* --- cellAudioSetNotifyEventQueue / Ex ----------------------------------
 * refs/.../cellAudio.cpp:2076-2249. NOT_INIT, TRANS_EVENT when the key names no
 * queue, TRANS_EVENT for a duplicate, else the key is registered with its own
 * event source. The source word (:2242) is
 *     (pid << 32) + 0x0e000000 + (key_count++ % 8192) * 0x100
 * which is process_getpid and lv2_event_port::id_base/id_step/id_count
 * (refs/rpcs3/rpcs3/Emu/Cell/lv2/sys_event.h:120, refs/rpcs3/rpcs3/Emu/Cell/lv2/sys_sync.h:70-72).
 * The _mxr000/_cellsurMixerMain hack (:2103-2140) is VSH-specific and has no
 * console-visible effect on a title's own queue, so it is not reproduced. */
constexpr uint64_t EVENT_PORT_ID_BASE = 0x0e000000ull;
constexpr uint64_t EVENT_PORT_ID_STEP = 0x100ull;
constexpr uint64_t EVENT_PORT_ID_COUNT = 8192ull;

inline int32_t set_notify_event_queue(Manager& m, uint64_t key, uint32_t flags) {
    if (!m.is_init) return ERR_NOT_INIT;
    if (flags & (~0u >> 5)) return ERR_PARAM;   /* :2211-2214 */
    if (!queue_exists(key)) return ERR_TRANS_EVENT;   /* lv2_event_queue::find, cellAudio.cpp:2162-2166 */
    for (const KeyInfo& k : m.keys) {
        if (k.key == key) return ERR_TRANS_EVENT;   /* duplicates are refused */
    }
    KeyInfo k;
    k.key = key;
    k.flags = flags;
    k.start_period = m.event_period;
    k.ack_timestamp = 0;
    k.source = (static_cast<uint64_t>(process_pid()) << 32) + EVENT_PORT_ID_BASE +
               (m.key_count++ % EVENT_PORT_ID_COUNT) * EVENT_PORT_ID_STEP;
    m.keys.push_back(k);
    return 0;
}

/* --- cellAudioRemoveNotifyEventQueue / Ex -------------------------------
 * refs/.../cellAudio.cpp:2258-2293: NOT_INIT, TRANS_EVENT unless a registered
 * key matches exactly (including its flags), else the entry is dropped. */
inline int32_t remove_notify_event_queue(Manager& m, uint64_t key, uint32_t flags) {
    if (!m.is_init) return ERR_NOT_INIT;
    if (flags & (~0u >> 5)) return ERR_PARAM;
    for (auto it = m.keys.begin(); it != m.keys.end(); ++it) {
        if (it->key == key) {
            if (it->flags != flags) break;
            m.keys.erase(it);
            return 0;
        }
    }
    return ERR_TRANS_EVENT;
}

/* --- cellAudioSetPortLevel(portNum, level) ------------------------------
 * refs/.../cellAudio.cpp:2006-2044. NOT_INIT, PARAM, PORT_NOT_OPEN, and a
 * negative level is a TODO that still answers OK (:2038-2041). The level ramp
 * RPCS3 spreads over 624 samples has no observable effect on the guest. */
inline int32_t set_port_level(Manager& m, uint32_t port_num, float level) {
    if (!m.is_init) return ERR_NOT_INIT;
    if (port_num >= PORT_COUNT) return ERR_PARAM;
    Port& p = m.ports[port_num];
    if (p.state == PortState::closed) return ERR_PORT_NOT_OPEN;
    if (level >= 0.0f) p.level = level;
    return 0;
}

/* --- the block the game is filling, and the Add*Data family -------------
 *
 * refs/.../cellAudio.h:196-200 (get_vm_ptr): the destination is
 *     addr + (u16(index) & (num_blocks - 1)) * block_size_bytes
 * i.e. the block the read index currently names. cellAudioAddData (:2287-2318)
 * validates NOT_INIT, PARAM for NULL/misaligned/samples != 256, then mixes
 * src[i] * volume over samples*channels; Add2chData (:2320-2380) spreads a
 * stereo source over a 2ch or 8ch port; Add6chData (:2382-2430) needs an 8ch
 * port. The volume is clamped to [-16,16] and a non-finite volume becomes 0
 * (:2310-2313).
 */
inline float clamp_volume(float v) {
    if (!(v == v) || v > 1e30f || v < -1e30f) return 0.0f;
    return std::clamp(v, -16.0f, 16.0f);
}

inline float* current_block(Manager& m, Port& p) {
    if (!p.num_blocks) return nullptr;
    const uint32_t block = static_cast<uint32_t>(p.cur_pos) & (p.num_blocks - 1);
    return reinterpret_cast<float*>(vm::ptr(p.addr + block * static_cast<uint32_t>(p.num_channels) * BLOCK_BYTES_PER_CHANNEL));
}

inline int32_t add_data(Manager& m, uint32_t port_num, uint32_t src_ea, uint32_t samples, float volume) {
    if (!m.is_init) return ERR_NOT_INIT;
    if (port_num >= PORT_COUNT || !src_ea || (src_ea & 3) || samples != BLOCK_SAMPLES) return ERR_PARAM;
    if (!readable(src_ea, samples * 4)) return ERR_PARAM;
    Port& p = m.ports[port_num];
    if (p.state == PortState::closed) return ERR_PORT_NOT_OPEN;
    if (!p.num_channels) return 0;
    if (!vm::range_mapped(p.addr, p.size)) return ERR_PORT_NOT_OPEN;
    float* dst = current_block(m, p);
    if (!dst) return ERR_PARAM;
    const float v = clamp_volume(volume);
    /* Guest samples are big-endian floats; the destination is guest memory too,
     * so the addition happens on host values and is stored big-endian. */
    const uint32_t n = samples * p.num_channels;
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t raw = vm::read32(src_ea + i * 4);
        float s;
        std::memcpy(&s, &raw, 4);
        const uint32_t draw = vm::read32(p.addr + static_cast<uint32_t>(p.cur_pos & (p.num_blocks - 1)) *
                                                       p.num_channels * BLOCK_BYTES_PER_CHANNEL + i * 4);
        float d;
        std::memcpy(&d, &draw, 4);
        d += s * v;
        uint32_t out;
        std::memcpy(&out, &d, 4);
        vm::write32(p.addr + static_cast<uint32_t>(p.cur_pos & (p.num_blocks - 1)) * p.num_channels *
                                     BLOCK_BYTES_PER_CHANNEL + i * 4, out);
    }
    return 0;
}

inline int32_t add_2ch_data(Manager& m, uint32_t port_num, uint32_t src_ea, uint32_t samples, float volume) {
    if (!m.is_init) return ERR_NOT_INIT;
    if (port_num >= PORT_COUNT || !src_ea || (src_ea & 3) || samples != BLOCK_SAMPLES) return ERR_PARAM;
    if (!readable(src_ea, samples * 2 * 4)) return ERR_PARAM;
    Port& p = m.ports[port_num];
    if (p.state == PortState::closed) return ERR_PORT_NOT_OPEN;
    if (p.num_channels != 2 && p.num_channels != 8) return 0;   /* RPCS3 logs and misses (:2369) */
    if (!vm::range_mapped(p.addr, p.size)) return ERR_PORT_NOT_OPEN;
    const float v = clamp_volume(volume);
    const uint32_t block_ofs = static_cast<uint32_t>(p.cur_pos & (p.num_blocks - 1)) * p.num_channels * BLOCK_BYTES_PER_CHANNEL;
    for (uint32_t i = 0; i < samples; i++) {
        for (uint32_t c = 0; c < 2; c++) {
            const uint32_t sraw = vm::read32(src_ea + (i * 2 + c) * 4);
            float s;
            std::memcpy(&s, &sraw, 4);
            const uint32_t ofs = p.addr + block_ofs + (i * p.num_channels + c) * 4;
            const uint32_t draw = vm::read32(ofs);
            float d;
            std::memcpy(&d, &draw, 4);
            d += s * v;
            uint32_t out;
            std::memcpy(&out, &d, 4);
            vm::write32(ofs, out);
        }
    }
    return 0;
}

inline int32_t add_6ch_data(Manager& m, uint32_t port_num, uint32_t src_ea, float volume) {
    if (!m.is_init) return ERR_NOT_INIT;
    if (port_num >= PORT_COUNT || !src_ea || (src_ea & 3)) return ERR_PARAM;
    if (!readable(src_ea, BLOCK_SAMPLES * 6 * 4)) return ERR_PARAM;
    Port& p = m.ports[port_num];
    if (p.state == PortState::closed) return ERR_PORT_NOT_OPEN;
    if (p.num_channels != 8) return 0;   /* :2421 */
    if (!vm::range_mapped(p.addr, p.size)) return ERR_PORT_NOT_OPEN;
    const float v = clamp_volume(volume);
    /* Front L, Center, Front R, Surround L, Surround R, LFE (cellAudio.cpp:2409). */
    static const uint32_t map[6] = {0, 2, 1, 4, 5, 3};   /* dst channel per src */
    const uint32_t block_ofs = static_cast<uint32_t>(p.cur_pos & (p.num_blocks - 1)) * 8 * BLOCK_BYTES_PER_CHANNEL;
    for (uint32_t i = 0; i < BLOCK_SAMPLES; i++) {
        for (uint32_t c = 0; c < 6; c++) {
            const uint32_t sraw = vm::read32(src_ea + (i * 6 + c) * 4);
            float s;
            std::memcpy(&s, &sraw, 4);
            const uint32_t ofs = p.addr + block_ofs + (i * 8 + map[c]) * 4;
            const uint32_t draw = vm::read32(ofs);
            float d;
            std::memcpy(&d, &draw, 4);
            d += s * v;
            uint32_t out;
            std::memcpy(&out, &d, 4);
            vm::write32(ofs, out);
        }
    }
    return 0;
}

/* --- one period of the mixer --------------------------------------------
 *
 * refs/.../cellAudio.cpp:706-780 (advance). For every STARTED port the period
 * counter is copied in, cur_pos advances by one block modulo num_blocks, and
 * that position is published as a big-endian u64 at the port's read index --
 * which is exactly what the guest polls. Then one CELL_AUDIO_EVENT_MIX event
 * per registered notify queue, subject to the decimation flags (:750-777):
 * data1 is CELL_AUDIO_EVENT_MIX (0), data3 is the queue's source word when
 * EVENTFLAG_BEFOREMIX is set and 0 otherwise.
 *
 * send_fn(source, d1, d2, d3) is called with the queue's key captured so the
 * caller can deliver by key; it returns whether the queue existed. */
template <typename SendFn>
inline void step(Manager& m, SendFn&& send_fn) {
    for (Port& p : m.ports) {
        if (p.state != PortState::started) continue;
        p.global_counter = m.counter;
        p.active_counter++;
        p.timestamp = guest_time_us();
        if (p.num_blocks) {
            p.cur_pos = (p.cur_pos + 1) % p.num_blocks;
            write_index(p.index, p.cur_pos);
            p.index_writes++;
            p.index_last = p.cur_pos;
        }
    }
    m.counter++;
    m.event_period++;

    for (const KeyInfo& k : m.keys) {
        if (k.flags & EVENTFLAG_NOMIX) continue;
        uint32_t periods = 1;
        if (k.flags & EVENTFLAG_DECIMATE_2) periods *= 2;
        if (k.flags & EVENTFLAG_DECIMATE_4) periods *= 4;
        if ((m.event_period ^ k.start_period) & (periods - 1)) continue;
        const uint64_t data3 = (k.flags & EVENTFLAG_BEFOREMIX) ? k.source : 0;
        send_fn(k.key, k.source, EVENT_MIX, 0ull, data3);
    }
}

}  // namespace audio
}  // namespace hle

#endif
