/* ps3base -- cellAudio (libaudio) HLE, tested against the reference.
 *
 * Reference: refs/rpcs3/rpcs3/Emu/Cell/Modules/cellAudio.cpp (behaviour)
 *            refs/rpcs3/rpcs3/Emu/Cell/Modules/cellAudio.h   (codes, structures)
 *
 * The suite drives the state machine in runtime/src/hle/audio_hle.h -- the same
 * header the import wrappers call -- with real guest memory. What it pins down,
 * one block per claim:
 *
 *   - the ORDER of checks in every function (NOT_INIT before PARAM, PARAM before
 *     PORT_NOT_OPEN), because a title that reads the wrong code takes the wrong
 *     branch;
 *   - the CellAudioPortConfig BYTES: 32 bytes, readIndexAddr at 0, status at 4,
 *     nChannel at 8, nBlock at 16, portSize at 24, portAddr at 28 -- the layout
 *     a wrong vm pointer width silently corrupts (the CellVideoOutDeviceInfo
 *     lesson in AGENTS.md);
 *   - portSize is EXACTLY ch*blocks*256*4 and portAddr is a mapped guest buffer
 *     of at least that size;
 *   - the read index at readIndexAddr ADVANCES once per period while started,
 *     wraps at num_blocks, and stays put when stopped and when closed;
 *   - one CELL_AUDIO_EVENT_MIX event per period per registered queue, with the
 *     source word and the decimation flags;
 *   - the Add*Data family writes the block the read index names.
 *
 * No game, no GPU, no audio device: guest memory, a stub queue and the header.
 */
#include "runtime/memory.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../runtime/src/hle/audio_hle.h"

/* --- the calls into the host, stubbed ------------------------------------ */

namespace hle {
namespace audio {

namespace {
uint32_t g_next_area = 0x20000000u;
uint32_t g_next_buffer = 0x30000000u;
struct FakeQueue {
    uint32_t depth = 0;
    std::string name;
};
std::map<uint64_t, FakeQueue> g_queues;
}

uint32_t alloc_index_area(uint32_t bytes) {
    const uint32_t a = g_next_area;
    if (!vm::map(a, bytes)) return 0;
    std::memset(vm::ptr(a), 0, bytes);
    g_next_area += 0x200000u;
    return a;
}
void free_index_area(uint32_t) {}

uint32_t alloc_port_buffer(uint32_t bytes) {
    const uint32_t a = g_next_buffer;
    if (!vm::map(a, bytes)) return 0;
    std::memset(vm::ptr(a), 0, bytes);
    g_next_buffer += 0x200000u;
    return a;
}
void free_port_buffer(uint32_t) {}

void write_index(uint32_t index_addr, uint64_t value) { vm::write64(index_addr, value); }

uint64_t guest_time_us() { return 1000000ull; }

uint32_t queue_create(uint64_t key, uint32_t depth, const char* name) {
    if (g_queues.count(key)) return 0;   /* CELL_EEXIST */
    g_queues[key] = FakeQueue{depth, name ? name : ""};
    return static_cast<uint32_t>(0x8d000100u + g_queues.size() * 0x100u);
}

struct Delivered {
    uint64_t key, source, d1, d2, d3;
};
std::vector<Delivered>& delivered() {
    static std::vector<Delivered> v;
    return v;
}

bool queue_send(uint64_t key, uint64_t source, uint64_t d1, uint64_t d2, uint64_t d3) {
    if (!g_queues.count(key)) return false;
    delivered().push_back({key, source, d1, d2, d3});
    return true;
}

bool queue_exists(uint64_t key) { return g_queues.count(key) != 0; }

uint32_t process_pid() { return 1; }

uint32_t g_sdk = 0x430001;   /* the three titles here are all above 0x35ffff */
uint32_t sdk_version() { return g_sdk; }

}  // namespace audio
}  // namespace hle

using namespace hle::audio;

static int fails = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; std::fprintf(stderr, "[FAIL] %s\n", msg); } \
} while (0)

#define CHECK_EQ(a, b, msg) do { \
    checks++; \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { fails++; std::fprintf(stderr, "[FAIL] %s (0x%llx != 0x%llx)\n", msg, _a, _b); } \
} while (0)

/* Guest scratch for the arguments and output structures. */
static constexpr uint32_t kParam = 0x100000;
static constexpr uint32_t kNum = 0x100100;
static constexpr uint32_t kConfig = 0x100200;
static constexpr uint32_t kId = 0x100300;
static constexpr uint32_t kKey = 0x100308;
static constexpr uint32_t kSrc = 0x101000;

static void put_param(uint64_t ch, uint64_t blocks, uint64_t attr, float level) {
    uint32_t lv;
    std::memcpy(&lv, &level, 4);
    vm::write64(kParam + 0, ch);
    vm::write64(kParam + 8, blocks);
    vm::write64(kParam + 16, attr);
    vm::write32(kParam + 24, lv);
}

static float read_be_float(uint32_t ea) {
    const uint32_t raw = vm::read32(ea);
    float f;
    std::memcpy(&f, &raw, 4);
    return f;
}
static void write_be_float(uint32_t ea, float f) {
    uint32_t raw;
    std::memcpy(&raw, &f, 4);
    vm::write32(ea, raw);
}

static uint64_t read_index(const Port& p) { return vm::read64(p.index); }

/* The stub table is in hle::audio's unnamed namespace, which a qualified name
 * still reaches from this translation unit. */
static uint32_t queue_depth(uint64_t key) { return hle::audio::g_queues[key].depth; }

int main() {
    if (!vm::init()) {
        std::fprintf(stderr, "vm::init failed\n");
        return 1;
    }
    if (!vm::map(0x100000, 0x40000)) {
        std::fprintf(stderr, "scratch map failed\n");
        return 1;
    }
    std::memset(vm::ptr(0x100000), 0, 0x40000);

    /* --- 1. the structure layouts --------------------------------------- */
    {
        CHECK_EQ(sizeof(PortParam), 32, "CellAudioPortParam is 32 bytes");
        CHECK_EQ(sizeof(PortConfig), 32, "CellAudioPortConfig is 32 bytes");
        CHECK_EQ(offsetof(PortConfig, read_index_addr), 0, "readIndexAddr at 0");
        CHECK_EQ(offsetof(PortConfig, status), 4, "status at 4 (a 32-bit vm pointer, not 64)");
        CHECK_EQ(offsetof(PortConfig, n_channel), 8, "nChannel at 8");
        CHECK_EQ(offsetof(PortConfig, n_block), 16, "nBlock at 16");
        CHECK_EQ(offsetof(PortConfig, port_size), 24, "portSize at 24");
        CHECK_EQ(offsetof(PortConfig, port_addr), 28, "portAddr at 28");
    }

    Manager m;

    /* --- 2. every entry point before init is NOT_INIT ---------------------
     * cellAudio.cpp:1538 (Init twice is ALREADY_INIT), :1619 (Quit), :1672,
     * :1791, :1829, :1871, :1908, :1941 and so on for every function. */
    {
        put_param(8, 8, 0, 1.0f);
        CHECK_EQ(quit(m), ERR_NOT_INIT, "Quit before init is NOT_INIT");
        CHECK_EQ(port_open(m, kParam, kNum), ERR_NOT_INIT, "PortOpen before init is NOT_INIT");
        CHECK_EQ(get_port_config(m, 0, kConfig), ERR_NOT_INIT, "GetPortConfig before init is NOT_INIT");
        CHECK_EQ(port_start(m, 0), ERR_NOT_INIT, "PortStart before init is NOT_INIT");
        CHECK_EQ(port_stop(m, 0), ERR_NOT_INIT, "PortStop before init is NOT_INIT");
        CHECK_EQ(port_close(m, 0), ERR_NOT_INIT, "PortClose before init is NOT_INIT");
        CHECK_EQ(get_port_timestamp(m, 0, 0, kConfig), ERR_NOT_INIT, "GetPortTimestamp before init is NOT_INIT");
        CHECK_EQ(get_port_block_tag(m, 0, 0, kConfig), ERR_NOT_INIT, "GetPortBlockTag before init is NOT_INIT");
        CHECK_EQ(set_notify_event_queue(m, 0x1234, 0), ERR_NOT_INIT, "SetNotifyEventQueue before init is NOT_INIT");
        CHECK_EQ(remove_notify_event_queue(m, 0x1234, 0), ERR_NOT_INIT,
                 "RemoveNotifyEventQueue before init is NOT_INIT");
        CHECK_EQ(set_port_level(m, 0, 1.0f), ERR_NOT_INIT, "SetPortLevel before init is NOT_INIT");
        CHECK_EQ(add_data(m, 0, kSrc, 256, 1.0f), ERR_NOT_INIT, "AddData before init is NOT_INIT");
        CHECK_EQ(add_2ch_data(m, 0, kSrc, 256, 1.0f), ERR_NOT_INIT, "Add2chData before init is NOT_INIT");
        CHECK_EQ(add_6ch_data(m, 0, kSrc, 1.0f), ERR_NOT_INIT, "Add6chData before init is NOT_INIT");
    }

    /* --- 3. Init, and Init twice is ALREADY_INIT (cellAudio.cpp:1538-1541) */
    CHECK_EQ(init(m), 0, "cellAudioInit answers 0");
    CHECK_EQ(m.index_area_size, 0x10000, "the shared index area is 0x10000 (cellAudio.cpp:1531)");
    CHECK(m.index_area != 0, "the index area is a real guest address");
    CHECK(vm::range_mapped(m.index_area, 0x10000), "the index area is mapped");
    CHECK_EQ(init(m), ERR_ALREADY_INIT, "a second Init is ALREADY_INIT");
    /* sdk 0x430001 > 0x35ffff, so eight ports (cellAudio.cpp:1590). */
    CHECK_EQ(m.free_count, 8, "sdk above 0x35ffff gives eight ports");
    /* The first free index is 8, so the first port opened gets server_index 1
     * and its index slot is index_area + 0x10 (cellAudio.cpp:1606-1610, :1604). */
    CHECK_EQ(m.free_indices[0], 8, "free_indices[0] is port_count (cellAudio.cpp:1609)");
        CHECK_EQ(m.free_indices[7], 1, "the stack pops from the top, so the first server_index is 1");

    /* --- 4. GetPortConfig on a closed port: status 0x1010, and the index slot
     * is still a real address (cellAudio.cpp:1797-1801). */
    {
        std::memset(vm::ptr(kConfig), 0xAA, 32);
        CHECK_EQ(get_port_config(m, 0, kConfig), 0, "GetPortConfig on a closed port answers 0");
        CHECK_EQ(vm::read32(kConfig + 0), m.index_area + m.ports[0].server_index * 16,
                 "readIndexAddr is the port's index slot");
        CHECK_EQ(vm::read32(kConfig + 4), STATUS_CLOSE, "status for a closed port is 0x1010");
        CHECK_EQ(vm::read64(kConfig + 8), 0, "nChannel for a closed port is 0");
        CHECK_EQ(vm::read64(kConfig + 16), 0, "nBlock for a closed port is 0");
        CHECK_EQ(vm::read32(kConfig + 24), 0, "portSize for a closed port is 0");
        CHECK_EQ(vm::read32(kConfig + 28), 0, "portAddr for a closed port is 0");
    }

    /* --- 5. PortOpen argument rules (cellAudio.cpp:1673-1692) ------------ */
    {
        std::memset(vm::ptr(kParam), 0, 32);
        CHECK_EQ(port_open(m, 0, kNum), ERR_PARAM, "a null portParam is PARAM");
        put_param(8, 8, 0, 1.0f);
        CHECK_EQ(port_open(m, kParam, 0), ERR_PARAM, "a null portNum is PARAM");
        put_param(0, 0, 0, 1.0f);
        CHECK_EQ(port_open(m, kParam, kNum), ERR_PARAM, "nBlock 0 is PARAM");
        put_param(2, 0, 0, 1.0f);
        CHECK_EQ(port_open(m, kParam, kNum), ERR_PARAM, "nBlock 0 is PARAM with 2 channels");
        put_param(2, 1, 0, 1.0f);
        CHECK_EQ(port_open(m, kParam, kNum), ERR_PARAM, "nBlock 1 is PARAM");
        put_param(2, 3, 0, 1.0f);
        CHECK_EQ(port_open(m, kParam, kNum), ERR_PARAM, "nBlock 3 is PARAM");
        put_param(1, 8, 0, 1.0f);
        CHECK_EQ(port_open(m, kParam, kNum), ERR_PARAM, "nChannel 1 is PARAM (only 2, 8 or 0)");
        put_param(6, 8, 0, 1.0f);
        CHECK_EQ(port_open(m, kParam, kNum), ERR_PARAM, "nChannel 6 is PARAM (only 2, 8 or 0)");
        put_param(4, 8, 0, 1.0f);
        CHECK_EQ(port_open(m, kParam, kNum), ERR_PARAM, "nChannel 4 is PARAM");
        CHECK_EQ(m.free_count, 8, "every refused PortOpen left the free list alone");
    }

    /* --- 6. the RDR port 0, exactly the arguments the title passes:
     * nChannel 8, nBlock 8, attr 0, level 0 (the live log at 2.372830). */
    {
        put_param(8, 8, 0, 0.0f);
        CHECK_EQ(port_open(m, kParam, kNum), 0, "PortOpen(8ch, 8 blocks) answers 0");
        const uint32_t num = vm::read32(kNum);
        CHECK_EQ(num, 0, "the first port is port 0");
        Port& p = m.ports[0];
        CHECK_EQ(p.server_index, 1, "port 0 gets server_index 1 (the console's order)");
        CHECK_EQ(p.num_channels, 8, "8 channels recorded");
        CHECK_EQ(p.num_blocks, 8, "8 blocks recorded");
        CHECK_EQ(p.size, 8u * 8u * 256u * 4u, "portSize is ch*blocks*256*4 = 0x10000");
        CHECK_EQ(p.size, 0x10000, "portSize is 0x10000");
        CHECK(p.addr != 0, "the port buffer has a guest address");
        CHECK(vm::range_mapped(p.addr, p.size), "the whole port buffer is mapped");
        CHECK_EQ(p.level, 1.0f, "level defaults to 1.0 without PORTATTR_INITLEVEL (cellAudio.cpp:1760-1763)");

        std::memset(vm::ptr(kConfig), 0xAA, 32);
        CHECK_EQ(get_port_config(m, 0, kConfig), 0, "GetPortConfig on the opened port answers 0");
        const uint32_t idx = vm::read32(kConfig + 0);
        CHECK_EQ(idx, p.index, "readIndexAddr is the port's index slot");
        CHECK(vm::range_mapped(idx, 8), "readIndexAddr is mapped and 8 bytes long");
        CHECK_EQ(vm::read32(kConfig + 4), STATUS_READY, "status of an opened port is READY (1)");
        CHECK_EQ(vm::read64(kConfig + 8), 8, "nChannel 8 in the output structure");
        CHECK_EQ(vm::read64(kConfig + 16), 8, "nBlock 8 in the output structure");
        CHECK_EQ(vm::read32(kConfig + 24), 0x10000, "portSize 0x10000 in the output structure");
        CHECK_EQ(vm::read32(kConfig + 28), p.addr, "portAddr is the buffer's guest address");
        CHECK_EQ(vm::read64(idx), 0, "the read index starts at 0");

        /* The buffer is real: write into it at the address the config gave. */
        const uint32_t buf = vm::read32(kConfig + 28);
        const uint32_t size = vm::read32(kConfig + 24);
        write_be_float(buf, 1.5f);
        CHECK(read_be_float(buf) == 1.5f, "the port buffer at portAddr is readable and writable guest memory");
        std::memset(vm::ptr(buf), 0x5A, size);
        bool all = true;
        for (uint32_t i = 0; i < size; i++) all = all && vm::ptr(buf)[i] == 0x5A;
        CHECK(all, "every byte of portSize is inside the mapping");
        std::memset(vm::ptr(buf), 0, size);
    }

    /* --- 6b. PORTATTR_INITLEVEL takes the parameter's level; without it the
     * level is 1.0 (cellAudio.cpp:1756-1763) ------------------------------- */
    {
        put_param(2, 4, PORTATTR_INITLEVEL, 0.25f);
        CHECK_EQ(port_open(m, kParam, kNum), 0, "PortOpen with INITLEVEL answers 0");
        const uint32_t num = vm::read32(kNum);
        CHECK_EQ(m.ports[num].level, 0.25f, "INITLEVEL takes the parameter's level");
        CHECK_EQ(m.ports[num].attr, PORTATTR_INITLEVEL, "the attribute is recorded");
        CHECK_EQ(port_close(m, num), 0, "and the port closes");
    }

    /* --- 7. PortStart: zeroes the buffer, status becomes RUN, index moves --- */
    {
        Port& p = m.ports[0];
        std::memset(vm::ptr(p.addr), 0x7F, p.size);
        CHECK_EQ(port_start(m, 0), 0, "PortStart on an opened port answers 0");
        bool zeroed = true;
        for (uint32_t i = 0; i < p.size && zeroed; i += 4096) zeroed = vm::ptr(p.addr)[i] == 0;
        CHECK(zeroed, "PortStart zeroes the whole port buffer (cellAudio.cpp:1850)");
        CHECK_EQ(vm::read32(kConfig + 4), STATUS_READY, "the stale config still says READY until re-read");
        CHECK_EQ(get_port_config(m, 0, kConfig), 0, "GetPortConfig after start answers 0");
        CHECK_EQ(vm::read32(kConfig + 4), STATUS_RUN, "status of a started port is RUN (2)");
        CHECK_EQ(port_start(m, 0), ERR_PORT_ALREADY_RUN, "a second Start is PORT_ALREADY_RUN");

        /* THE MEASUREMENT: the read index advances one step per period. */
        uint64_t seen = vm::read64(p.index);
        CHECK_EQ(seen, 0, "the read index is 0 after start");
        std::vector<uint64_t> indices;
        for (int period = 0; period < 10; period++) {
            step(m, [](uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {});
            indices.push_back(vm::read64(p.index));
        }
        const uint64_t expect[10] = {1, 2, 3, 4, 5, 6, 7, 0, 1, 2};
        bool matches = true;
        for (int i = 0; i < 10; i++) matches = matches && indices[i] == expect[i];
        CHECK(matches, "the read index advances 1 per period and wraps at nBlock (1..7,0,1,2)");
        CHECK_EQ(p.index_writes, 10, "ten periods wrote the index ten times");
        CHECK_EQ(m.counter, 10, "the period counter is 10");
        CHECK_EQ(p.global_counter, 9, "the port's global counter is the period it last advanced in");
    }

    /* --- 8. PortStop: NOT_RUN for a stopped port, and the index freezes ----- */
    {
        Port& p = m.ports[0];
        CHECK_EQ(port_stop(m, 0), 0, "PortStop on a started port answers 0");
        const uint64_t frozen = vm::read64(p.index);
        for (int i = 0; i < 5; i++) step(m, [](uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {});
        CHECK_EQ(vm::read64(p.index), frozen, "the read index does not move while the port is stopped");
        CHECK_EQ(get_port_config(m, 0, kConfig), 0, "GetPortConfig after stop answers 0");
        CHECK_EQ(vm::read32(kConfig + 4), STATUS_READY, "a stopped port reports READY (cellAudio.cpp:1799)");
        CHECK_EQ(port_stop(m, 0), ERR_PORT_NOT_RUN, "a second Stop is PORT_NOT_RUN");
        CHECK_EQ(port_stop(m, 3), ERR_PORT_NOT_RUN, "Stop on a closed port is PORT_NOT_RUN");
    }

    /* --- 9. PortClose: PORT_NOT_OPEN when already closed, and the port's buffer
     * is released so a later open reuses the slot ------------------------- */
    {
        CHECK_EQ(port_close(m, 0), 0, "PortClose on an open port answers 0");
        CHECK_EQ(m.ports[0].state, PortState::closed, "the port is closed");
        CHECK_EQ(m.ports[0].addr, 0, "the closed port's buffer address is cleared");
        CHECK_EQ(get_port_config(m, 0, kConfig), 0, "GetPortConfig on a closed port answers 0");
        CHECK_EQ(vm::read32(kConfig + 4), STATUS_CLOSE, "status is CLOSE again");
        CHECK_EQ(port_close(m, 0), ERR_PORT_NOT_OPEN, "closing a closed port is PORT_NOT_OPEN");
    }

    /* --- 10. the second RDR port: 2 channels x 16 blocks (the live log at
     * 15.586108), so portSize 0x8000, and its own index slot --------------- */
    {
        put_param(2, 16, 0, 0.0f);
        CHECK_EQ(port_open(m, kParam, kNum), 0, "PortOpen(2ch, 16 blocks) answers 0");
        CHECK_EQ(vm::read32(kNum), 0, "PortClose returned port 0 to the free list, so it is reused");
        CHECK_EQ(m.ports[0].server_index, 1, "the reused slot keeps its server_index");
        CHECK_EQ(port_open(m, kParam, kNum), 0, "a second PortOpen(2ch, 16 blocks) answers 0");
        const uint32_t num = vm::read32(kNum);
        CHECK_EQ(num, 1, "the second port is port 1");
        Port& p = m.ports[1];
        CHECK_EQ(p.num_channels, 2, "2 channels");
        CHECK_EQ(p.num_blocks, 16, "16 blocks");
        CHECK_EQ(p.size, 2u * 16u * 256u * 4u, "portSize is 2*16*256*4");
        CHECK_EQ(p.size, 0x8000, "portSize is 0x8000");
        CHECK_EQ(p.index, m.index_area + p.server_index * 16, "its index slot is its own");
        CHECK(p.index != m.ports[0].index, "the two ports have different index slots");
        CHECK_EQ(port_start(m, 1), 0, "PortStart on port 1 answers 0");
        CHECK_EQ(vm::read64(p.index), 0, "port 1's index starts at 0");
        step(m, [](uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {});
        CHECK_EQ(vm::read64(p.index), 1, "port 1's index advanced while port 0 is closed");
        CHECK_EQ(vm::read64(m.ports[0].index), 0, "port 0's index did not move (it is closed)");
    }

    /* --- 11. PORT_FULL at the ninth port (cellAudio.cpp:1742-1745) -------- */
    {
        put_param(2, 2, 0, 0.0f);
        uint32_t opened = 0;
        for (uint32_t i = 0; i < PORT_COUNT; i++) {
            if (m.ports[i].state != PortState::closed) opened++;
        }
        int extra = 0;
        for (int i = 0; i < 8; i++) {
            const int32_t rc = port_open(m, kParam, kNum);
            if (rc == 0) extra++;
            else {
                CHECK_EQ(rc, ERR_PORT_FULL, "the ninth PortOpen is PORT_FULL");
                break;
            }
        }
        CHECK_EQ(opened + extra, PORT_COUNT - 0, "eight ports can be open at once");
        CHECK_EQ(port_open(m, kParam, kNum), ERR_PORT_FULL, "a further PortOpen is still PORT_FULL");
        /* Release the filler ports so the later blocks keep a free slot; port 1
         * stays open because the timestamp and block-tag blocks use it. */
        for (uint32_t i = 2; i < PORT_COUNT; i++) {
            if (m.ports[i].state != PortState::closed) CHECK_EQ(port_close(m, i), 0, "a filler port closes");
        }
    }

    /* --- 12. SetPortLevel (cellAudio.cpp:2006-2044) ----------------------- */
    {
        CHECK_EQ(set_port_level(m, 1, 0.5f), 0, "SetPortLevel on an open port answers 0");
        CHECK_EQ(m.ports[1].level, 0.5f, "the level is stored");
        CHECK_EQ(set_port_level(m, 1, -1.0f), 0, "a negative level still answers 0 (cellAudio.cpp:2038-2041)");
        CHECK_EQ(set_port_level(m, 7, 0.5f), ERR_PORT_NOT_OPEN, "SetPortLevel on a closed port is PORT_NOT_OPEN");
    }

    /* --- 13. the notify event queues ------------------------------------
     * CreateNotifyEventQueue answers a key from the 0x80004d494f323221 base,
     * depth 8 when the SDK is above 0x35ffff (cellAudio.cpp:2055-2057), and
     * SetNotifyEventQueue registers it so one mixed event arrives per period
     * with data1 = CELL_AUDIO_EVENT_MIX (0) and data3 = 0 without BEFOREMIX. */
    {
        CHECK_EQ(create_notify_event_queue(m, kId, kKey, 0), 0, "CreateNotifyEventQueue answers 0");
        const uint32_t id = vm::read32(kId);
        const uint64_t key = vm::read64(kKey);
        CHECK(id != 0, "the queue id is written");
        CHECK_EQ(key, NOTIFY_KEY_BASE, "the first key is the 0x80004d494f323221 base");
        CHECK_EQ(queue_depth(key), 8u, "depth 8 for an SDK above 0x35ffff");
        hle::audio::delivered().clear();
        CHECK_EQ(set_notify_event_queue(m, key, 0), 0, "SetNotifyEventQueue answers 0");
        CHECK_EQ(hle::audio::delivered().size(), 0u, "registration delivers no event into the title's queue");
        CHECK_EQ(set_notify_event_queue(m, key, 0), ERR_TRANS_EVENT, "a duplicate registration is TRANS_EVENT");
        CHECK_EQ(set_notify_event_queue(m, 0x1234, 0), ERR_TRANS_EVENT, "an unknown key is TRANS_EVENT");
        const uint64_t source = m.keys.back().source;
        CHECK_EQ(source >> 32, 1ull, "the event source carries the process id in its high half");
        CHECK_EQ(source & 0xffffffffull, EVENT_PORT_ID_BASE, "the first source is 0x0e000000 + 0*0x100");
        /* The port 1 that is still started now delivers exactly one event per
         * step, and the event is the console's: (source, MIX, 0, 0). */
        hle::audio::delivered().clear();
        step(m, [](uint64_t k, uint64_t s, uint64_t d1, uint64_t d2, uint64_t d3) {
            hle::audio::queue_send(k, s, d1, d2, d3);
        });
        CHECK_EQ(hle::audio::delivered().size(), 1u, "one period sends one event to the one registered queue");
        CHECK_EQ(hle::audio::delivered()[0].key, key, "delivered on the registered key");
        CHECK_EQ(hle::audio::delivered()[0].source, source, "with the source the console would use");
        CHECK_EQ(hle::audio::delivered()[0].d1, 0, "data1 is CELL_AUDIO_EVENT_MIX (0)");
        CHECK_EQ(hle::audio::delivered()[0].d2, 0, "data2 is 0");
        CHECK_EQ(hle::audio::delivered()[0].d3, 0, "data3 is 0 without CELL_AUDIO_EVENTFLAG_BEFOREMIX");

        /* A second queue, with BEFOREMIX: data3 becomes its own source. */
        CHECK_EQ(create_notify_event_queue(m, kId, kKey, 0), 0, "a second CreateNotifyEventQueue answers 0");
        const uint64_t key2 = vm::read64(kKey);
        CHECK_EQ(key2, NOTIFY_KEY_BASE + 1, "the second key is base+1");
        CHECK_EQ(set_notify_event_queue(m, key2, static_cast<uint32_t>(EVENTFLAG_BEFOREMIX)), 0,
                 "SetNotifyEventQueue with BEFOREMIX answers 0");
        const uint64_t source2 = m.keys.back().source;
        CHECK_EQ(source2 & 0xffffffffull, EVENT_PORT_ID_BASE + EVENT_PORT_ID_STEP,
                 "the second source is 0x0e000100");
        hle::audio::delivered().clear();
        step(m, [](uint64_t k, uint64_t s, uint64_t d1, uint64_t d2, uint64_t d3) {
            hle::audio::queue_send(k, s, d1, d2, d3);
        });
        CHECK_EQ(hle::audio::delivered().size(), 2u, "both queues get an event");
        bool bef = false;
        for (const Delivered& d : hle::audio::delivered())
            if (d.key == key2 && d.d3 == source2) bef = true;
        CHECK(bef, "with BEFOREMIX the event's data3 is the queue's own source word");

        /* DECIMATE_2: an event every other period (cellAudio.cpp:760-770). */
        CHECK_EQ(create_notify_event_queue(m, kId, kKey, 0), 0, "a third CreateNotifyEventQueue answers 0");
        const uint64_t key3 = vm::read64(kKey);
        CHECK_EQ(key3, NOTIFY_KEY_BASE + 2, "the third key is base+2");
        CHECK_EQ(set_notify_event_queue(m, key3, static_cast<uint32_t>(EVENTFLAG_DECIMATE_2)), 0,
                 "SetNotifyEventQueue with DECIMATE_2 answers 0");
        hle::audio::delivered().clear();
        for (int i = 0; i < 6; i++)
            step(m, [](uint64_t k, uint64_t s, uint64_t d1, uint64_t d2, uint64_t d3) {
                hle::audio::queue_send(k, s, d1, d2, d3);
            });
        int count3 = 0;
        for (const Delivered& d : hle::audio::delivered())
            if (d.key == key3) count3++;
        CHECK_EQ(count3, 3, "DECIMATE_2 delivers three events over six periods");

        /* NOMIX: nothing at all. */
        CHECK_EQ(create_notify_event_queue(m, kId, kKey, 0), 0, "a fourth CreateNotifyEventQueue answers 0");
        const uint64_t key4 = vm::read64(kKey);
        CHECK_EQ(set_notify_event_queue(m, key4, static_cast<uint32_t>(EVENTFLAG_NOMIX)), 0,
                 "SetNotifyEventQueue with NOMIX answers 0");
        hle::audio::delivered().clear();
        step(m, [](uint64_t k, uint64_t s, uint64_t d1, uint64_t d2, uint64_t d3) {
            hle::audio::queue_send(k, s, d1, d2, d3);
        });
        bool any4 = false;
        for (const Delivered& d : hle::audio::delivered())
            if (d.key == key4) any4 = true;
        CHECK(!any4, "NOMIX delivers nothing");

        /* Remove: the key is gone, a second Remove is TRANS_EVENT, and a
         * Remove with the wrong flags does not match. */
        CHECK_EQ(remove_notify_event_queue(m, key4, static_cast<uint32_t>(EVENTFLAG_NOMIX)), 0,
                 "RemoveNotifyEventQueue with the right flags answers 0");
        CHECK_EQ(remove_notify_event_queue(m, key4, static_cast<uint32_t>(EVENTFLAG_NOMIX)), ERR_TRANS_EVENT,
                 "a second Remove is TRANS_EVENT");
        CHECK_EQ(remove_notify_event_queue(m, key2, 0), ERR_TRANS_EVENT,
                 "Remove with different flags does not match the registration");
        CHECK_EQ(remove_notify_event_queue(m, key2, static_cast<uint32_t>(EVENTFLAG_BEFOREMIX)), 0,
                 "Remove with the registered flags answers 0");
    }

    /* --- 14. CreateNotifyEventQueue argument rules ----------------------- */
    {
        CHECK_EQ(create_notify_event_queue(m, 0, kKey, 0), ERR_PARAM, "a null id pointer is PARAM");
        CHECK_EQ(create_notify_event_queue(m, kId, 0, 0), ERR_PARAM, "a null key pointer is PARAM");
        CHECK_EQ(create_notify_event_queue(m, kId, kKey, 2), ERR_PARAM, "an unknown flag is PARAM");
    }

    /* --- 15. timestamps and block tags --------------------------------- */
    {
        Port& p = m.ports[1];
        /* The port's global counter is whatever the periods so far made it; ask
         * for a tag from the future and get TAG_NOT_FOUND (cellAudio.cpp:1955). */
        const uint64_t gc = p.global_counter;
        CHECK_EQ(get_port_timestamp(m, 1, gc + 100, kConfig), ERR_TAG_NOT_FOUND,
                 "a tag above the global counter is TAG_NOT_FOUND");
        std::memset(vm::ptr(kConfig), 0, 8);
        CHECK_EQ(get_port_timestamp(m, 1, gc, kConfig), 0, "the current tag answers 0");
        CHECK_EQ(vm::read64(kConfig), p.timestamp, "a delta of 0 reports the port's own timestamp");
        CHECK_EQ(get_port_timestamp(m, 1, gc - 1, kConfig), 0, "an older tag answers 0");
        CHECK_EQ(vm::read64(kConfig), p.timestamp - 5333, "one period back is timestamp - 5333 us");
        CHECK_EQ(get_port_timestamp(m, 5, 0, kConfig), ERR_PORT_NOT_OPEN, "a timestamp on a closed port is PORT_NOT_OPEN");

        std::memset(vm::ptr(kConfig), 0, 8);
        CHECK_EQ(get_port_block_tag(m, 1, 0, kConfig), 0, "a block tag answers 0");
        CHECK_EQ(vm::read64(kConfig), p.global_counter + 0 - p.cur_pos, "the tag is global_counter + block - cur_pos");
        CHECK_EQ(get_port_block_tag(m, 1, 16, kConfig), ERR_PARAM, "block 16 of 16 is PARAM");
        CHECK_EQ(get_port_block_tag(m, 1, 15, kConfig), 0, "block 15 of 16 answers 0");
        CHECK_EQ(get_port_block_tag(m, 4, 0, kConfig), ERR_PORT_NOT_OPEN, "a block tag on a closed port is PORT_NOT_OPEN");
    }

    /* --- 16. the Add*Data family writes the block the read index names ----
     * The port is 2ch x 16 blocks, so one block is 2*256*4 = 0x800 bytes.
     * cur_pos was advanced by every step above; the write must land at
     * addr + cur_pos*0x800 and nowhere else. */
    {
        Port& p = m.ports[1];
        const uint32_t cur = static_cast<uint32_t>(p.cur_pos);
        const uint32_t block_ofs = p.addr + cur * 2 * BLOCK_BYTES_PER_CHANNEL;
        std::memset(vm::ptr(p.addr), 0, p.size);
        for (uint32_t i = 0; i < 256 * 2; i++) write_be_float(kSrc + i * 4, 1.0f);
        CHECK_EQ(add_2ch_data(m, 1, kSrc, 256, 0.5f), 0, "Add2chData answers 0");
        CHECK(read_be_float(block_ofs) == 0.5f, "the sample landed in the block the read index names");
        CHECK(read_be_float(block_ofs + 4) == 0.5f, "the right channel too");
        CHECK(read_be_float(block_ofs + 256 * 2 * 4 - 4) == 0.5f, "and at the end of that block");
        CHECK_EQ(read_be_float(p.addr + ((cur + 1) % 16) * 2 * BLOCK_BYTES_PER_CHANNEL), 0.0f,
                 "the next block was not touched");
        /* A second call ADDS, it does not replace (cellAudio.cpp:2364). */
        CHECK_EQ(add_2ch_data(m, 1, kSrc, 256, 0.25f), 0, "a second Add2chData answers 0");
        CHECK(read_be_float(block_ofs) == 0.75f, "the second call adds to the buffer");
        /* Argument rules. */
        CHECK_EQ(add_2ch_data(m, 1, kSrc, 255, 1.0f), ERR_PARAM, "samples 255 is PARAM (must be 256)");
        CHECK_EQ(add_2ch_data(m, 1, 0, 256, 1.0f), ERR_PARAM, "a null src is PARAM");
        CHECK_EQ(add_2ch_data(m, 1, kSrc + 1, 256, 1.0f), ERR_PARAM, "an unaligned src is PARAM");
        CHECK_EQ(add_2ch_data(m, 3, kSrc, 256, 1.0f), ERR_PORT_NOT_OPEN, "a closed port is PORT_NOT_OPEN");
        /* The volume clamp: -20 becomes -16, NaN becomes 0 (cellAudio.cpp:2310). */
        std::memset(vm::ptr(p.addr), 0, p.size);
        CHECK_EQ(add_2ch_data(m, 1, kSrc, 256, -20.0f), 0, "a volume of -20 answers 0");
        CHECK(read_be_float(block_ofs) == -16.0f, "the volume is clamped to -16");
        std::memset(vm::ptr(p.addr), 0, p.size);
        float nan = 0.0f;
        const uint32_t bits = 0x7fc00000u;
        std::memcpy(&nan, &bits, 4);
        CHECK_EQ(add_2ch_data(m, 1, kSrc, 256, nan), 0, "a NaN volume answers 0");
        CHECK(read_be_float(block_ofs) == 0.0f, "a NaN volume mixes nothing");
    }

    /* --- 17. a 2ch Add2chData mixes L/R into an 8ch port's front pair only
     * (cellAudio.cpp:2352-2366) ------------------------------------------- */
    {
        put_param(8, 2, 0, 0.0f);
        CHECK_EQ(port_open(m, kParam, kNum), 0, "an 8ch port opens");
        const uint32_t num = vm::read32(kNum);
        CHECK_EQ(port_start(m, num), 0, "and starts");
        Port& p = m.ports[num];
        std::memset(vm::ptr(p.addr), 0, p.size);
        for (uint32_t i = 0; i < 256 * 2; i++) write_be_float(kSrc + i * 4, 2.0f);
        const uint32_t block_ofs = p.addr + static_cast<uint32_t>(p.cur_pos) * 8 * BLOCK_BYTES_PER_CHANNEL;
        CHECK_EQ(add_2ch_data(m, num, kSrc, 256, 1.0f), 0, "Add2chData into an 8ch port answers 0");
        CHECK(read_be_float(block_ofs + 0) == 2.0f, "left went to channel 0");
        CHECK(read_be_float(block_ofs + 4) == 2.0f, "right went to channel 1");
        CHECK(read_be_float(block_ofs + 8) == 0.0f, "channel 2 (center) was not touched");
        CHECK(read_be_float(block_ofs + 16) == 0.0f, "channel 4 (rear left) was not touched");

        /* 6ch into the same 8ch port, RPCS3's channel map: L C R SL SR LFE ->
         * 0 2 1 3 4 5 (cellAudio.cpp:2409-2418). */
        const uint32_t block2 = p.addr + static_cast<uint32_t>(p.cur_pos) * 8 * BLOCK_BYTES_PER_CHANNEL;
        std::memset(vm::ptr(p.addr), 0, p.size);
        for (uint32_t i = 0; i < 256 * 6; i++) write_be_float(kSrc + i * 4, float(i % 6) + 1.0f);
        CHECK_EQ(add_6ch_data(m, num, kSrc, 1.0f), 0, "Add6chData into an 8ch port answers 0");
        CHECK(read_be_float(block2 + 0) == 1.0f, "src 0 (front L) -> dst channel 0");
        CHECK(read_be_float(block2 + 8) == 2.0f, "src 1 (center) -> dst channel 2");
        CHECK(read_be_float(block2 + 4) == 3.0f, "src 2 (front R) -> dst channel 1");
        CHECK(read_be_float(block2 + 16) == 4.0f, "src 3 (surround L) -> dst channel 4");
        CHECK(read_be_float(block2 + 20) == 5.0f, "src 4 (surround R) -> dst channel 5");
        CHECK(read_be_float(block2 + 12) == 6.0f, "src 5 (LFE) -> dst channel 3");
        CHECK_EQ(add_6ch_data(m, 1, kSrc, 1.0f), 0, "Add6chData on a 2ch port answers 0 and does nothing");
    }

    /* --- 18. AddData over the port's own channel count ------------------- */
    {
        /* Port 1 is the 2ch port. */
        Port& p = m.ports[1];
        std::memset(vm::ptr(p.addr), 0, p.size);
        for (uint32_t i = 0; i < 256 * 2; i++) write_be_float(kSrc + i * 4, 3.0f);
        const uint32_t block_ofs = p.addr + static_cast<uint32_t>(p.cur_pos) * 2 * BLOCK_BYTES_PER_CHANNEL;
        CHECK_EQ(add_data(m, 1, kSrc, 256, 2.0f), 0, "AddData answers 0");
        CHECK(read_be_float(block_ofs) == 6.0f, "AddData mixes samples*channels values");
        CHECK(read_be_float(block_ofs + 4 * 511) == 6.0f, "the last of the 512 values too");
    }

    /* --- 19. Quit closes every port and clears the queues -------------
     * cellAudio.cpp:1637-1650: ports closed, shared memory released, queues NOT
     * cleared by the module (they are LV2 objects), init cleared. */
    {
        CHECK_EQ(quit(m), 0, "Quit answers 0");
        CHECK(!m.is_init, "the manager is no longer initialised");
        bool all_closed = true;
        for (const Port& p : m.ports) all_closed = all_closed && p.state == PortState::closed;
        CHECK(all_closed, "Quit closed every port");
        CHECK_EQ(m.index_area, 0, "the index area was released");
        CHECK_EQ(quit(m), ERR_NOT_INIT, "a second Quit is NOT_INIT");
        /* And a re-Init works, which is what a title that ends and restarts its
         * audio does. */
        CHECK_EQ(init(m), 0, "a re-Init after Quit answers 0");
        CHECK_EQ(init(m), ERR_ALREADY_INIT, "and a second one is ALREADY_INIT again");
        quit(m);
    }

    /* --- 20. the SDK gate on the port count ----------------------------- */
    {
        hle::audio::g_sdk = 0x350000;   /* <= 0x35ffff: four ports */
        Manager m2;
        CHECK_EQ(init(m2), 0, "Init on an old-SDK console answers 0");
        CHECK_EQ(m2.free_count, 4, "an SDK of 0x350000 gives four ports (cellAudio.cpp:1590)");
        put_param(2, 2, 0, 0.0f);
        uint32_t opened = 0;
        for (int i = 0; i < 6; i++) {
            const int32_t rc = port_open(m2, kParam, kNum);
            if (rc == 0) opened++;
        }
        CHECK_EQ(opened, 4, "only four ports can be opened on the old SDK");
        CHECK_EQ(port_open(m2, kParam, kNum), ERR_PORT_FULL, "the fifth is PORT_FULL");
        quit(m2);
        hle::audio::g_sdk = 0x430001;
    }

    std::printf("%s: %d checks, %d failures\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails ? 1 : 0;
}
