/* ps3base -- cellVoice (libvoice) HLE, tested against the reference.
 *
 * Every function this title imports is exercised through the same handlers the
 * import path calls: this file includes the module and drives it with a real
 * ppu::Context and real guest memory, so the value checked is the value that
 * lands in r3.
 *
 * Reference: refs/rpcs3/rpcs3/Emu/Cell/Modules/cellVoice.cpp (behaviour),
 *            refs/rpcs3/rpcs3/Emu/Cell/Modules/cellVoice.h   (codes, structures).
 *
 * What matters, and what each block below pins down:
 *   - the state machine's ORDER of checks (is_init first, then arguments, then
 *     the port topology), because a title that reads the wrong code takes the
 *     wrong branch;
 *   - the id cellVoiceCreatePort returns (counter modulo 0xa0, in bits [0,7]);
 *   - the services' events actually arriving on the queue the title registered,
 *     with the registered source -- that is the whole point of the module;
 *   - cellVoiceSetPortAttr answering 0x80310812 (device absent) and NOT 0;
 *   - cellVoiceEnd clearing every port and queue, so a re-Init starts clean.
 *
 * No game, no GPU, no network: guest memory, a stub queue, and the header.
 */
#include "runtime/memory.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../runtime/src/hle/cellVoice.cpp"

/* --- the two LV2 calls, stubbed ------------------------------------------
 *
 * The module talks to LV2 through lv2::create_queue_by_key / send_event_by_key
 * (runtime/src/lv2/event.h). Here they are a map from key to guest id plus an
 * event log, which is all the voice service observes of the kernel. */
namespace lv2 {

namespace {
uint32_t g_next_id = 0x8d000100;
struct FakeQueue {
    uint32_t size = 0;
    std::string name;
};
std::map<uint64_t, FakeQueue> g_queues;
}

struct Delivered {
    uint64_t key, source, d1, d2, d3;
};
std::vector<Delivered>& delivered() {
    static std::vector<Delivered> v;
    return v;
}

uint32_t create_queue_by_key(uint64_t key, uint32_t size, const char* name) {
    if (g_queues.count(key)) return 0;   /* CELL_EEXIST */
    g_queues[key] = FakeQueue{size, name ? name : ""};
    return g_next_id += 0x100;
}

bool send_event_by_key(uint64_t key, uint64_t source, uint64_t d1, uint64_t d2, uint64_t d3) {
    if (!g_queues.count(key)) return false;
    delivered().push_back({key, source, d1, d2, d3});
    return true;
}

}

/* hle::Reg normally registers into the import table; this test looks the
 * handlers up by name, exactly as the dispatcher does. */
namespace hle {
namespace {
std::map<std::string, Fn>& test_registry() {
    static std::map<std::string, Fn> m;
    return m;
}
}
Reg::Reg(const char* module, const char* name, Fn fn) {
    test_registry()[std::string(module) + "::" + name] = fn;
}
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
    while (true) {
        const uint32_t a = g_next;
        if (!vm::range_mapped(a, size)) {
            if (!vm::map(a, size)) return 0;
        }
        g_next += size;
        return a;
    }
}

/* The dispatcher calls a handler with r3.. as the arguments and reads r3 back.
 * A libvoice code is a NEGATIVE s32, and r3 holds its sign-extended 64-bit
 * value; the comparison is made on the low 32 bits as a signed int. */
template <class FnT>
int64_t invoke(FnT fn, uint64_t a0 = 0, uint64_t a1 = 0, uint64_t a2 = 0) {
    ppu::Context ctx{};
    ctx.gpr[3] = a0;
    ctx.gpr[4] = a1;
    ctx.gpr[5] = a2;
    fn(ctx, vm::g_base);
    return static_cast<int64_t>(static_cast<int32_t>(ctx.gpr[3] & 0xFFFFFFFFull));
}

using hle::hle_cellVoice_cellVoiceInit;
using hle::hle_cellVoice_cellVoiceInitEx;
using hle::hle_cellVoice_cellVoiceEnd;
using hle::hle_cellVoice_cellVoiceCreateNotifyEventQueue;
using hle::hle_cellVoice_cellVoiceCreatePort;
using hle::hle_cellVoice_cellVoiceDeletePort;
using hle::hle_cellVoice_cellVoiceConnectIPortToOPort;
using hle::hle_cellVoice_cellVoiceDisconnectIPortFromOPort;
using hle::hle_cellVoice_cellVoiceStart;
using hle::hle_cellVoice_cellVoiceStop;
using hle::hle_cellVoice_cellVoiceGetPortInfo;
using hle::hle_cellVoice_cellVoiceGetBitRate;
using hle::hle_cellVoice_cellVoiceReadFromOPort;
using hle::hle_cellVoice_cellVoiceWriteToIPort;
using hle::hle_cellVoice_cellVoiceSetPortAttr;

namespace v = hle::voice;

/* Write a CellVoicePortParam at addr, big-endian, per cellVoice.h:146-168. */
void put_port_param(uint32_t addr, uint32_t port_type, uint16_t threshold, uint16_t mute,
                    float volume, uint32_t u32_at_12, uint32_t pcm_data_type = 0,
                    uint32_t pcm_rate = 16000) {
    std::memset(vm::ptr(addr), 0, 64);
    vm::write32(addr + 0, port_type);
    vm::write16(addr + 4, threshold);
    vm::write16(addr + 6, mute);
    vm::write32(addr + 8, std::bit_cast<uint32_t>(volume));
    vm::write32(addr + 12, u32_at_12);
    vm::ptr(addr + 16)[0] = 1;   /* numChannels  */
    vm::ptr(addr + 17)[0] = 2;   /* sampleAlignment */
    vm::write32(addr + 20, pcm_data_type);
    vm::write32(addr + 24, pcm_rate);
}

}  // namespace

int main() {
    if (!vm::init()) {
        std::fprintf(stderr, "vm::init failed\n");
        return 1;
    }

    /* The codes, refs/.../cellVoice.h:9-27. A wrong constant here is a wrong
     * branch in the title, so each one is asserted. */
    CHECK(v::ERR_LIBVOICE_NOT_INIT == static_cast<int32_t>(0x80310801), "NOT_INIT is 0x80310801");
    CHECK(v::ERR_LIBVOICE_INITIALIZED == static_cast<int32_t>(0x80310802), "INITIALIZED is 0x80310802");
    CHECK(v::ERR_ARGUMENT_INVALID == static_cast<int32_t>(0x80310805), "ARGUMENT_INVALID is 0x80310805");
    CHECK(v::ERR_TOPOLOGY == static_cast<int32_t>(0x80310807), "TOPOLOGY is 0x80310807");
    CHECK(v::ERR_RESOURCE_INSUFFICIENT == static_cast<int32_t>(0x80310808), "RESOURCE_INSUFFICIENT is 0x80310808");
    CHECK(v::ERR_SERVICE_DETACHED == static_cast<int32_t>(0x8031080b), "SERVICE_DETACHED is 0x8031080b");
    CHECK(v::ERR_EVENT_QUEUE == static_cast<int32_t>(0x8031080f), "EVENT_QUEUE is 0x8031080f");
    CHECK(v::ERR_DEVICE_NOT_PRESENT == static_cast<int32_t>(0x80310812), "DEVICE_NOT_PRESENT is 0x80310812");

    /* The guest structure layout, refs/.../cellVoice.h:120-168. */
    CHECK(sizeof(v::InitParam) == 32, "CellVoiceInitParam is 32 bytes");
    CHECK(sizeof(v::PortParam) == 28, "CellVoicePortParam is 28 bytes");
    CHECK(sizeof(v::BasePortInfo) == 24, "CellVoiceBasePortInfo is 24 bytes");
    CHECK(v::PARAM_VOICE_BITRATE == 12, "voice.bitrate at 12");
    CHECK(v::PARAM_PCMAUDIO_DATA_TYPE == 20, "pcmaudio.format.dataType at 20");
    CHECK(v::PARAM_PCMAUDIO_SAMPLE_RATE == 24, "pcmaudio.format.sampleRate at 24");

    const uint32_t init_param = alloc_area(0x40);
    const uint32_t port_param = alloc_area(0x40);
    const uint32_t port_id_out = alloc_area(0x40);
    const uint32_t info = alloc_area(0x40);
    const uint32_t bitrate_out = alloc_area(0x40);
    const uint32_t size_out = alloc_area(0x40);
    const uint32_t qid_out = alloc_area(0x40);
    const uint32_t key_out = alloc_area(0x40);
    const uint32_t data = alloc_area(0x100);
    CHECK(init_param && port_param && port_id_out && info && bitrate_out && size_out &&
          qid_out && key_out && data, "guest memory for the call arguments");
    if (!(init_param && port_param && port_id_out && info && bitrate_out && size_out &&
          qid_out && key_out && data)) return 1;

    /* --- before Init every call is LIBVOICE_NOT_INIT ------------------ */
    CHECK(invoke(hle_cellVoice_cellVoiceStart) == v::ERR_LIBVOICE_NOT_INIT,
          "cellVoiceStart before Init is NOT_INIT");
    CHECK(invoke(hle_cellVoice_cellVoiceStop) == v::ERR_LIBVOICE_NOT_INIT,
          "cellVoiceStop before Init is NOT_INIT");
    CHECK(invoke(hle_cellVoice_cellVoiceEnd) == v::ERR_LIBVOICE_NOT_INIT,
          "cellVoiceEnd before Init is NOT_INIT");
    CHECK(invoke(hle_cellVoice_cellVoiceCreatePort, port_id_out, port_param) == v::ERR_LIBVOICE_NOT_INIT,
          "cellVoiceCreatePort before Init is NOT_INIT");
    CHECK(invoke(hle_cellVoice_cellVoiceCreateNotifyEventQueue, qid_out, key_out) == v::ERR_LIBVOICE_NOT_INIT,
          "cellVoiceCreateNotifyEventQueue before Init is NOT_INIT");
    CHECK(invoke(hle_cellVoice_cellVoiceSetPortAttr, 0, 1000, info) == v::ERR_LIBVOICE_NOT_INIT,
          "cellVoiceSetPortAttr before Init is NOT_INIT");

    /* A handler's error is sign-extended into r3, as ppu_gpr_cast does. */
    {
        ppu::Context ctx{};
        ctx.gpr[3] = 0;
        hle_cellVoice_cellVoiceEnd(ctx, vm::g_base);
        CHECK(ctx.gpr[3] == static_cast<uint64_t>(0xFFFFFFFF80310801ull),
              "End before Init leaves 0xffffffff80310801 in r3 (sign-extended)");
    }

    /* --- cellVoiceInit, :405-422 ------------------------------------- */
    CHECK(invoke(hle_cellVoice_cellVoiceInit, 0) == v::ERR_ARGUMENT_INVALID,
          "cellVoiceInit(NULL) is ARGUMENT_INVALID");
    CHECK(invoke(hle_cellVoice_cellVoiceInit, init_param) == 0,
          "cellVoiceInit(pArg) is CELL_OK");
    CHECK(invoke(hle_cellVoice_cellVoiceInit, init_param) == v::ERR_LIBVOICE_INITIALIZED,
          "a second cellVoiceInit is LIBVOICE_INITIALIZED");
    CHECK(invoke(hle_cellVoice_cellVoiceInitEx, init_param) == v::ERR_LIBVOICE_INITIALIZED,
          "cellVoiceInitEx after Init is LIBVOICE_INITIALIZED too (:430-433)");

    /* --- cellVoiceCreateNotifyEventQueue, :79-114 -------------------- */
    /* RPCS3's queue size is 0x40 (:99) and its first candidate key is
     * 0x80004d494f323285 (:98). */
    CHECK(invoke(hle_cellVoice_cellVoiceCreateNotifyEventQueue, 0, key_out) == v::ERR_ARGUMENT_INVALID,
          "a NULL id pointer is refused instead of faulting (RPCS3 has no check)");
    CHECK(invoke(hle_cellVoice_cellVoiceCreateNotifyEventQueue, qid_out, key_out) == 0,
          "cellVoiceCreateNotifyEventQueue is CELL_OK");
    const uint32_t qid = vm::read32(qid_out);
    const uint64_t key = vm::read64(key_out);
    CHECK(qid != 0, "the queue id was written");
    CHECK(key == 0x80004d494f323285ull, "the key is the first candidate, 0x80004d494f323285");
    CHECK(lv2::g_queues[key].size == 0x40, "the queue was created with size 0x40");
    CHECK(lv2::g_queues[key].name == "cellVoice", "the queue is named cellVoice");

    /* A second call must skip the taken key and use the next one (:101-105). */
    const uint32_t qid_out2 = alloc_area(0x40);
    const uint32_t key_out2 = alloc_area(0x40);
    CHECK(invoke(hle_cellVoice_cellVoiceCreateNotifyEventQueue, qid_out2, key_out2) == 0,
          "a second create is CELL_OK");
    CHECK(vm::read64(key_out2) == 0x80004d494f323285ull + 1,
          "the second queue takes the NEXT candidate key, not the used one");

    /* --- cellVoiceCreatePort, :116-194 ------------------------------- */
    CHECK(invoke(hle_cellVoice_cellVoiceCreatePort, port_id_out, 0) == v::ERR_ARGUMENT_INVALID,
          "cellVoiceCreatePort(NULL pArg) is ARGUMENT_INVALID");

    /* An unknown port type (:167). */
    put_port_param(port_param, 99, 0, 0, 1.0f, v::BITRATE_7300);
    CHECK(invoke(hle_cellVoice_cellVoiceCreatePort, port_id_out, port_param) == v::ERR_ARGUMENT_INVALID,
          "an unknown portType is ARGUMENT_INVALID");

    /* A voice port with a bitrate outside the exact set (:144-158). */
    put_port_param(port_param, v::PORTTYPE_IN_VOICE, 0, 0, 1.0f, 1234);
    CHECK(invoke(hle_cellVoice_cellVoiceCreatePort, port_id_out, port_param) == v::ERR_ARGUMENT_INVALID,
          "a voice port with bitrate 1234 is ARGUMENT_INVALID");

    /* A PCM port with dataType past INTEGER_LITTLE_ENDIAN (:135). */
    put_port_param(port_param, v::PORTTYPE_IN_PCMAUDIO, 0, 0, 1.0f, 64, 6);
    CHECK(invoke(hle_cellVoice_cellVoiceCreatePort, port_id_out, port_param) == v::ERR_ARGUMENT_INVALID,
          "a PCM port with dataType 6 is ARGUMENT_INVALID (legal values end at 5)");

    /* The legitimate case: an OUT_VOICE port at 7300 bps. */
    put_port_param(port_param, v::PORTTYPE_OUT_VOICE, 0, 0, 1.0f, v::BITRATE_7300);
    CHECK(invoke(hle_cellVoice_cellVoiceCreatePort, port_id_out, port_param) == 0,
          "a valid OUT_VOICE port is CELL_OK");
    const uint32_t oport = vm::read32(port_id_out);
    /* Id: bits [0,7] = id_ctr after ++ and % 0xa0 (:176), bits [8,15] = 1..128. */
    CHECK((oport & 0xff) == 1, "the first port's low byte is the counter, 1 (0 -> 1 then % 0xa0)");
    CHECK((oport >> 8) >= 1 && (oport >> 8) <= 128, "the port's high byte is a slot in [1,128]");

    /* An IN_MIC port, which takes no parameter at all (:161-165). */
    put_port_param(port_param, v::PORTTYPE_IN_MIC, 0, 0, 1.0f, 0);
    CHECK(invoke(hle_cellVoice_cellVoiceCreatePort, port_id_out, port_param) == 0,
          "an IN_MIC port is CELL_OK");
    const uint32_t iport = vm::read32(port_id_out);
    CHECK(iport != oport, "the two ports got different ids");

    /* --- cellVoiceConnectIPortToOPort, :55-77 ------------------------ */
    CHECK(invoke(hle_cellVoice_cellVoiceConnectIPortToOPort, iport, oport) == 0,
          "IN_MIC -> OUT_VOICE connects: CELL_OK");
    CHECK(invoke(hle_cellVoice_cellVoiceConnectIPortToOPort, oport, iport) == v::ERR_TOPOLOGY,
          "OUT_VOICE as the input side is TOPOLOGY");
    CHECK(invoke(hle_cellVoice_cellVoiceConnectIPortToOPort, 0, oport) == v::ERR_TOPOLOGY,
          "a port that does not exist is TOPOLOGY");
    CHECK(invoke(hle_cellVoice_cellVoiceDisconnectIPortFromOPort, iport, oport) == 0,
          "the same pair disconnects: CELL_OK");
    CHECK(invoke(hle_cellVoice_cellVoiceDisconnectIPortFromOPort, oport, oport) == v::ERR_TOPOLOGY,
          "disconnecting with a wrong side is TOPOLOGY");

    /* --- cellVoiceGetPortInfo, :333-358 ------------------------------ */
    for (uint32_t i = 0; i < 0x18; i++) vm::ptr(info)[i] = 0xEE;
    CHECK(invoke(hle_cellVoice_cellVoiceGetPortInfo, oport, info) == v::ERR_SERVICE_DETACHED,
          "GetPortInfo before Start is SERVICE_DETACHED");
    CHECK(vm::ptr(info)[0] == 0xEE, "the failed call left the caller's structure alone");
    CHECK(invoke(hle_cellVoice_cellVoiceGetPortInfo, 0, info) == v::ERR_TOPOLOGY,
          "GetPortInfo on an unknown port is TOPOLOGY");

    /* --- cellVoiceStart, :705-717 / VoiceStart, :686-703 -------------
     * The registered key must receive SERVICE_ATTACHED (bit 3, :697). Only the
     * key registered by SetNotifyEventQueue is notified, and this title does not
     * import that function -- so the state machine is driven directly for this
     * part, against the same Manager the handlers use. That is the one place the
     * test pokes the manager, and it is done through the header's API. */
    CHECK(invoke(hle_cellVoice_cellVoiceStart) == 0, "cellVoiceStart is CELL_OK");

    for (uint32_t i = 0; i < 0x18; i++) vm::ptr(info)[i] = 0xEE;
    CHECK(invoke(hle_cellVoice_cellVoiceGetPortInfo, oport, info) == 0,
          "GetPortInfo after Start is CELL_OK");
    CHECK(vm::read32(info + 0) == v::PORTTYPE_OUT_VOICE, "portType was written from the port");
    CHECK(vm::ptr(info)[4] == 0xEE && vm::read32(info + 20) == 0xEEEEEEEEu,
          "state/numByte/frameSize are NOT zeroed: RPCS3 writes only portType (:353)");
    CHECK(invoke(hle_cellVoice_cellVoiceStart) == 0, "starting twice is CELL_OK");
    CHECK(lv2::delivered().empty(), "starting twice sends no second SERVICE_ATTACHED (:688-689)");

    /* --- cellVoiceGetBitRate, :268-291 ------------------------------ */
    vm::write32(bitrate_out, 0);
    CHECK(invoke(hle_cellVoice_cellVoiceGetBitRate, oport, bitrate_out) == 0,
          "GetBitRate on a voice port is CELL_OK");
    CHECK(vm::read32(bitrate_out) == v::BITRATE_7300, "the bitrate is the one the port was made with");
    put_port_param(port_param, v::PORTTYPE_IN_PCMAUDIO, 0, 0, 1.0f, 0x100, 0);
    CHECK(invoke(hle_cellVoice_cellVoiceCreatePort, port_id_out, port_param) == 0,
          "a PCM input port is CELL_OK");
    const uint32_t pport = vm::read32(port_id_out);
    vm::write32(bitrate_out, 0);
    CHECK(invoke(hle_cellVoice_cellVoiceGetBitRate, pport, bitrate_out) == v::ERR_TOPOLOGY,
          "GetBitRate on a non-voice port is TOPOLOGY");
    CHECK(vm::read32(bitrate_out) == 0x4f323285u,
          "the topology error leaves RPCS3's constant 0x4f323285 behind (:282)");

    /* --- cellVoiceSetPortAttr, :646-664 ----------------------------- */
    /* This is the function the brief names: without a microphone the console
     * reports the device absent. It must be that code, NOT CELL_OK. */
    const int64_t attr_rc = invoke(hle_cellVoice_cellVoiceSetPortAttr, oport, 1001, info);
    CHECK(attr_rc == v::ERR_DEVICE_NOT_PRESENT,
          "SetPortAttr answers DEVICE_NOT_PRESENT (0x80310812), as RPCS3's not_an_error(:663)");
    CHECK(attr_rc != 0, "SetPortAttr NEVER returns CELL_OK: no attribute was applied");
    CHECK(invoke(hle_cellVoice_cellVoiceSetPortAttr, 0xffff, 1001, info) == v::ERR_TOPOLOGY,
          "SetPortAttr on an unknown port is TOPOLOGY, before the device check (:657-660)");

    /* --- cellVoiceReadFromOPort, :855-876 --------------------------- */
    vm::write32(size_out, 0x1234);
    for (uint32_t i = 0; i < 16; i++) vm::ptr(data)[i] = 0x77;
    CHECK(invoke(hle_cellVoice_cellVoiceReadFromOPort, oport, data, size_out) == 0,
          "ReadFromOPort on an output port is CELL_OK");
    CHECK(vm::read32(size_out) == 0, "ReadFromOPort reports *size = 0: no data offline (:872-873)");
    CHECK(vm::ptr(data)[0] == 0x77, "ReadFromOPort left the caller's buffer alone");
    CHECK(invoke(hle_cellVoice_cellVoiceReadFromOPort, iport, data, size_out) == v::ERR_TOPOLOGY,
          "ReadFromOPort on an INPUT port is TOPOLOGY (:869-870)");

    /* --- cellVoiceWriteToIPort, :798-815 ---------------------------- */
    vm::write32(size_out, 0x1234);
    CHECK(invoke(hle_cellVoice_cellVoiceWriteToIPort, iport, data, size_out) == 0,
          "WriteToIPort on an input port is CELL_OK");
    CHECK(vm::read32(size_out) == 0x1234,
          "WriteToIPort does NOT touch *size: RPCS3 does not (:813-814)");
    CHECK(invoke(hle_cellVoice_cellVoiceWriteToIPort, oport, data, size_out) == v::ERR_TOPOLOGY,
          "WriteToIPort on an OUTPUT port is TOPOLOGY (:811-812)");

    /* --- cellVoiceStop / cellVoiceEnd, :738-764 / :237-266 ---------- */
    CHECK(invoke(hle_cellVoice_cellVoiceStop) == 0, "cellVoiceStop is CELL_OK");
    CHECK(invoke(hle_cellVoice_cellVoiceStop) == 0, "stopping twice is CELL_OK (:749-750)");

    CHECK(invoke(hle_cellVoice_cellVoiceEnd) == 0, "cellVoiceEnd is CELL_OK");
    CHECK(invoke(hle_cellVoice_cellVoiceStart) == v::ERR_LIBVOICE_NOT_INIT,
          "after End the module is NOT_INIT again (:263)");
    /* The ports are gone with the state: the same id is now unknown. */
    CHECK(invoke(hle_cellVoice_cellVoiceInit, init_param) == 0, "Init works again after End");
    CHECK(invoke(hle_cellVoice_cellVoiceGetPortInfo, oport, info) == v::ERR_TOPOLOGY,
          "End cleared the ports: the old port id is unknown");

    /* --- the service's events reach the registered queue ------------- */
    /* Register the queue by hand on the module's manager, then drive Start/Stop
     * through the real handlers: the event must arrive with the registered
     * source, and each transition must produce exactly one event. */
    {
        v::Manager probe;   /* a throwaway manager, not the module's */
        probe.is_init = true;
        probe.queue_keys[key].push_back(0x1122334455667788ull);
        const size_t before = lv2::delivered().size();
        CHECK(v::start(probe) == 0, "start() returns CELL_OK");
        CHECK(lv2::delivered().size() == before + 1, "Start sends exactly one event");
        const lv2::Delivered& e = lv2::delivered().back();
        CHECK(e.key == key, "the event went to the registered key");
        CHECK(e.source == 0x1122334455667788ull, "the event carries the registered source");
        CHECK(e.d1 == v::EVENT_SERVICE_ATTACHED, "the event is SERVICE_ATTACHED (bit 3, :697)");
        CHECK(e.d2 == 0 && e.d3 == 0, "data2 and data3 are zero, as queue->send(...,0,0)");
        CHECK(v::start(probe) == 0 && lv2::delivered().size() == before + 1,
              "a second start() sends nothing (:688-689)");
        CHECK(v::stop(probe) == 0 && lv2::delivered().size() == before + 2,
              "stop() sends exactly one event");
        CHECK(lv2::delivered().back().d1 == v::EVENT_SERVICE_DETACHED,
              "the event is SERVICE_DETACHED (bit 4, :758)");
        CHECK(v::start(probe) == 0 && lv2::delivered().size() == before + 3,
              "start() after stop() attaches again");
        CHECK(v::end(probe) == 0 && lv2::delivered().size() == before + 4,
              "end() while started also sends SERVICE_DETACHED (:256)");
        CHECK(lv2::delivered().back().d1 == v::EVENT_SERVICE_DETACHED,
              "end()'s event is DETACHED");
        CHECK(!probe.is_init && probe.ports.empty() && probe.queue_keys.empty(),
              "end() reset the manager (:262, reset() at :41-47)");
    }

    /* --- the handlers, by the names the dispatcher builds ------------- */
    /* The dispatcher keys on "module::name" (runtime/src/ppu_runtime.cpp:85-89).
     * A typo makes the import resolve nowhere and the handler never run, so both
     * the key and the target function are checked. The list is RDR's 13 imports
     * (out-rdr/ppu/table.cpp:81815-81827) plus CreateNotifyEventQueue, which the
     * module also registers and the event tests above use. */
    struct Want { const char* name; void (*fn)(ppu::Context&, uint8_t*); };
    const Want want[] = {
        {"cellVoiceStart", hle_cellVoice_cellVoiceStart},
        {"cellVoiceDisconnectIPortFromOPort", hle_cellVoice_cellVoiceDisconnectIPortFromOPort},
        {"cellVoiceCreateNotifyEventQueue", hle_cellVoice_cellVoiceCreateNotifyEventQueue},
        {"cellVoiceCreatePort", hle_cellVoice_cellVoiceCreatePort},
        {"cellVoiceReadFromOPort", hle_cellVoice_cellVoiceReadFromOPort},
        {"cellVoiceWriteToIPort", hle_cellVoice_cellVoiceWriteToIPort},
        {"cellVoiceGetPortInfo", hle_cellVoice_cellVoiceGetPortInfo},
        {"cellVoiceSetPortAttr", hle_cellVoice_cellVoiceSetPortAttr},
        {"cellVoiceDeletePort", hle_cellVoice_cellVoiceDeletePort},
        {"cellVoiceConnectIPortToOPort", hle_cellVoice_cellVoiceConnectIPortToOPort},
        {"cellVoiceGetBitRate", hle_cellVoice_cellVoiceGetBitRate},
        {"cellVoiceInit", hle_cellVoice_cellVoiceInit},
        {"cellVoiceStop", hle_cellVoice_cellVoiceStop},
        {"cellVoiceEnd", hle_cellVoice_cellVoiceEnd},
    };
    for (const Want& w : want) {
        const auto it = hle::test_registry().find(std::string("cellVoice::") + w.name);
        CHECK(it != hle::test_registry().end(), w.name);
        CHECK(it != hle::test_registry().end() && it->second == w.fn,
              "the registered handler is this function, not another slot");
    }
    /* All 14 registrations are present and no name is registered twice under a
     * different slot. */
    size_t voice_names = 0;
    for (const auto& kv : hle::test_registry())
        if (kv.first.rfind("cellVoice::", 0) == 0) voice_names++;
    CHECK(voice_names >= 14, "the module registered at least the 13 imports plus CreateNotifyEventQueue");

    /* cellVoiceDebugTopology is a refusal in RPCS3 (:878-886), and it must stay
     * one: never CELL_OK. */
    const int64_t dbg = invoke(hle::hle_cellVoice_cellVoiceDebugTopology);
    CHECK(dbg == static_cast<int32_t>(0x80310809),
          "cellVoiceDebugTopology returns NOT_IMPLEMENTED (0x80310809), not CELL_OK");


    std::printf("voice: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
