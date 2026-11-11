/* ps3base -- cellVoice (libvoice) HLE, for Red Dead Redemption (BLES00680).
 *
 * The title's 13 imports are out-rdr/ppu/table.cpp:81815-81827; the behaviour
 * and every return code below is refs/rpcs3/rpcs3/Emu/Cell/Modules/cellVoice.cpp.
 * The state machine itself lives in voice_hle.h so tests/voice.cpp drives the
 * same code, with the same argument checks and the same answers.
 *
 * Two deviations from RPCS3, both deliberate and both logged:
 *
 *  1. RPCS3 dereferences guest pointers unchecked -- its own comments say so
 *     ("No nullptr check!", cellVoice.cpp:279, :352, :393). A null or unmapped
 *     pointer there faults the emulator. Here the pointer is checked and the
 *     function returns the code it returns for a bad argument; no value is ever
 *     invented for the caller.
 *  2. cellVoiceGetSignalState is not imported by this title. It is implemented
 *     anyway, because it is the same body as cellVoiceSetPortAttr (detached
 *     microphone, :360-378), and leaving a registered name unimplemented would
 *     turn a real answer into an ENOSYS.
 *
 * cellVoiceDebugTopology is not imported either. RPCS3's body is
 * UNIMPLEMENTED_FUNC followed by CELL_VOICE_ERROR_NOT_IMPLEMENTED (:878-886), so
 * that is what it returns here -- an explicit named refusal, which is what the
 * reference itself does. Nothing else in the module falls back to a generic OK.
 */
#include "common.h"
#include "voice_hle.h"

#include "../lv2/event.h"

#include <mutex>

namespace hle {

namespace {

using namespace hle::voice;   /* the state machine, voice_hle.h */

std::mutex g_lock;
Manager g_manager;

/* libvoice's errors are negative s32 values; ret() takes an int64_t and the
 * dispatcher truncates to r3, so sign-extension is what the caller sees. */
inline void ret_err(ppu::Context& ctx, int32_t code) {
    ret(ctx, static_cast<int64_t>(code));
}

}

/* The two LV2 calls the state machine needs; see voice_hle.h. The unit test
 * supplies its own versions, which is what keeps this module testable without a
 * scheduler or a guest thread. */
namespace voice {

uint32_t queue_create(uint64_t key, uint32_t size, const char* name) {
    return lv2::create_queue_by_key(key, size, name);
}

bool queue_send(uint64_t key, uint64_t source, uint64_t d1, uint64_t d2, uint64_t d3) {
    return lv2::send_event_by_key(key, source, d1, d2, d3);
}

}

/* cellVoiceInit(vm::ptr<CellVoiceInitParam> pArg)
 * refs/.../cellVoice.cpp:405-422 -- INITIALIZED, then ARGUMENT_INVALID, then
 * CELL_OK and is_init = true. */
HLE_FUNC(cellVoice, cellVoiceInit) {
    const uint32_t p_arg = a32(ctx, 0);
    std::lock_guard lk(g_lock);
    const int32_t rc = init(g_manager, p_arg);
    if (rc) LOG_DEBUG("cellVoice", "cellVoiceInit(pArg=0x%x) -> 0x%08x", p_arg, static_cast<uint32_t>(rc));
    ret_err(ctx, rc);
}

/* cellVoiceInitEx, refs/.../cellVoice.cpp:424-441 -- same body as Init. */
HLE_FUNC(cellVoice, cellVoiceInitEx) {
    const uint32_t p_arg = a32(ctx, 0);
    std::lock_guard lk(g_lock);
    ret_err(ctx, init(g_manager, p_arg));
}

/* cellVoiceEnd() -- refs/.../cellVoice.cpp:237-266. */
HLE_FUNC(cellVoice, cellVoiceEnd) {
    std::lock_guard lk(g_lock);
    ret_err(ctx, end(g_manager));
}

/* cellVoiceCreateNotifyEventQueue(vm::ptr<u32> id, vm::ptr<u64> key)
 * refs/.../cellVoice.cpp:79-114. Queue size 0x40, ten candidate keys. */
HLE_FUNC(cellVoice, cellVoiceCreateNotifyEventQueue) {
    const uint32_t id_out = a32(ctx, 0), key_out = a32(ctx, 1);
    std::lock_guard lk(g_lock);
    const int32_t rc = create_notify_event_queue(g_manager, id_out, key_out);
    if (rc) LOG_WARN("cellVoice", "cellVoiceCreateNotifyEventQueue(id=0x%x, key=0x%x) -> 0x%08x",
                     id_out, key_out, static_cast<uint32_t>(rc));
    ret_err(ctx, rc);
}

/* cellVoiceCreatePort(vm::ptr<u32> portId, vm::cptr<CellVoicePortParam> pArg)
 * refs/.../cellVoice.cpp:116-194. */
HLE_FUNC(cellVoice, cellVoiceCreatePort) {
    const uint32_t out = a32(ctx, 0), arg = a32(ctx, 1);
    std::lock_guard lk(g_lock);
    const int32_t rc = create_port(g_manager, out, arg);
    if (rc) LOG_WARN("cellVoice", "cellVoiceCreatePort(portId=0x%x, pArg=0x%x) -> 0x%08x",
                     out, arg, static_cast<uint32_t>(rc));
    else LOG_DEBUG("cellVoice", "cellVoiceCreatePort -> 0x%x", vm::read32(out));
    ret_err(ctx, rc);
}

/* cellVoiceDeletePort(u32 portId) -- refs/.../cellVoice.cpp:196-211. */
HLE_FUNC(cellVoice, cellVoiceDeletePort) {
    const uint32_t id = a32(ctx, 0);
    std::lock_guard lk(g_lock);
    const int32_t rc = delete_port(g_manager, id);
    if (rc) LOG_WARN("cellVoice", "cellVoiceDeletePort(portId=%u) -> 0x%08x", id, static_cast<uint32_t>(rc));
    ret_err(ctx, rc);
}

/* cellVoiceConnectIPortToOPort(u32 ips, u32 ops) -- refs/...:55-77. */
HLE_FUNC(cellVoice, cellVoiceConnectIPortToOPort) {
    const uint32_t ips = a32(ctx, 0), ops = a32(ctx, 1);
    std::lock_guard lk(g_lock);
    const int32_t rc = connect(g_manager, ips, ops);
    if (rc) LOG_WARN("cellVoice", "cellVoiceConnectIPortToOPort(ips=%u, ops=%u) -> 0x%08x",
                     ips, ops, static_cast<uint32_t>(rc));
    ret_err(ctx, rc);
}

/* cellVoiceDisconnectIPortFromOPort(u32 ips, u32 ops) -- refs/...:213-235. */
HLE_FUNC(cellVoice, cellVoiceDisconnectIPortFromOPort) {
    const uint32_t ips = a32(ctx, 0), ops = a32(ctx, 1);
    std::lock_guard lk(g_lock);
    const int32_t rc = disconnect(g_manager, ips, ops);
    if (rc) LOG_WARN("cellVoice", "cellVoiceDisconnectIPortFromOPort(ips=%u, ops=%u) -> 0x%08x",
                     ips, ops, static_cast<uint32_t>(rc));
    ret_err(ctx, rc);
}

/* cellVoiceStart() -- refs/.../cellVoice.cpp:705-717, VoiceStart at :686-703.
 * The SERVICE_ATTACHED event is delivered to every registered queue key. */
HLE_FUNC(cellVoice, cellVoiceStart) {
    std::lock_guard lk(g_lock);
    ret_err(ctx, start(g_manager));
}

/* cellVoiceStop() -- refs/.../cellVoice.cpp:738-764. */
HLE_FUNC(cellVoice, cellVoiceStop) {
    std::lock_guard lk(g_lock);
    ret_err(ctx, stop(g_manager));
}

/* cellVoiceGetPortInfo(u32 portId, vm::ptr<CellVoiceBasePortInfo> pInfo)
 * refs/.../cellVoice.cpp:333-358. */
HLE_FUNC(cellVoice, cellVoiceGetPortInfo) {
    const uint32_t id = a32(ctx, 0), info = a32(ctx, 1);
    std::lock_guard lk(g_lock);
    const int32_t rc = get_port_info(g_manager, id, info);
    if (rc) LOG_DEBUG("cellVoice", "cellVoiceGetPortInfo(portId=%u, pInfo=0x%x) -> 0x%08x",
                      id, info, static_cast<uint32_t>(rc));
    ret_err(ctx, rc);
}

/* cellVoiceGetBitRate(u32 portId, vm::ptr<u32> bitrate)
 * refs/.../cellVoice.cpp:268-291, including the 0x4f323285 placeholder. */
HLE_FUNC(cellVoice, cellVoiceGetBitRate) {
    const uint32_t id = a32(ctx, 0), out = a32(ctx, 1);
    std::lock_guard lk(g_lock);
    const int32_t rc = get_bit_rate(g_manager, id, out);
    if (rc) LOG_DEBUG("cellVoice", "cellVoiceGetBitRate(portId=%u, bitrate=0x%x) -> 0x%08x",
                      id, out, static_cast<uint32_t>(rc));
    ret_err(ctx, rc);
}

/* cellVoiceReadFromOPort(u32 ops, vm::ptr<void> data, vm::ptr<u32> size)
 * refs/.../cellVoice.cpp:855-876: *size = 0 and CELL_OK for an output port. */
HLE_FUNC(cellVoice, cellVoiceReadFromOPort) {
    const uint32_t ops = a32(ctx, 0), data = a32(ctx, 1), size = a32(ctx, 2);
    std::lock_guard lk(g_lock);
    const int32_t rc = read_from_o_port(g_manager, ops, data, size);
    if (rc) LOG_DEBUG("cellVoice", "cellVoiceReadFromOPort(ops=%u) -> 0x%08x", ops, static_cast<uint32_t>(rc));
    ret_err(ctx, rc);
}

/* cellVoiceWriteToIPort(u32 ips, vm::cptr<void> data, vm::ptr<u32> size)
 * refs/.../cellVoice.cpp:798-815. */
HLE_FUNC(cellVoice, cellVoiceWriteToIPort) {
    const uint32_t ips = a32(ctx, 0), data = a32(ctx, 1), size = a32(ctx, 2);
    std::lock_guard lk(g_lock);
    const int32_t rc = write_to_i_port(g_manager, ips, data, size);
    if (rc) LOG_WARN("cellVoice", "cellVoiceWriteToIPort(ips=%u, data=0x%x, size=0x%x) -> 0x%08x",
                     ips, data, size, static_cast<uint32_t>(rc));
    ret_err(ctx, rc);
}

/* cellVoiceSetPortAttr(u32 portId, u32 attr, vm::ptr<void> attrValue)
 * refs/.../cellVoice.cpp:646-664: a detached microphone, reported as the raw
 * code 0x80310812 through not_an_error (:663). */
HLE_FUNC(cellVoice, cellVoiceSetPortAttr) {
    const uint32_t id = a32(ctx, 0), attr = a32(ctx, 1), value = a32(ctx, 2);
    std::lock_guard lk(g_lock);
    const int32_t rc = set_port_attr(g_manager, id, attr, value);
    if (rc) LOG_TRACE("cellVoice", "cellVoiceSetPortAttr(portId=%u, attr=%u, value=0x%x) -> 0x%08x",
                      id, attr, value, static_cast<uint32_t>(rc));
    ret_err(ctx, rc);
}

/* cellVoiceGetPortAttr(u32 portId, u32 attr, vm::ptr<void> attrValue)
 * refs/.../cellVoice.cpp:313-331 -- the same detached-microphone answer (:330).
 * Not imported by this title; kept so the module's surface is complete. */
HLE_FUNC(cellVoice, cellVoiceGetPortAttr) {
    const uint32_t id = a32(ctx, 0), attr = a32(ctx, 1), value = a32(ctx, 2);
    std::lock_guard lk(g_lock);
    ret_err(ctx, get_port_attr(g_manager, id, attr, value));
}

/* cellVoiceGetSignalState -- refs/.../cellVoice.cpp:360-378, same body (:377). */
HLE_FUNC(cellVoice, cellVoiceGetSignalState) {
    const uint32_t id = a32(ctx, 0), attr = a32(ctx, 1), value = a32(ctx, 2);
    std::lock_guard lk(g_lock);
    ret_err(ctx, get_port_attr(g_manager, id, attr, value));
}

/* cellVoicePausePortAll / cellVoiceResumePortAll /
 * cellVoiceSetMuteFlagAll: refs/.../cellVoice.cpp:462-474, :531-543, :602-615.
 * Each validates is_init and returns CELL_OK without touching a port, and
 * SetMuteFlagAll explicitly does NOT change bMute (:613). That is the console's
 * answer, so it is the answer here. None of the three is imported by this
 * title; they exist because the module is otherwise incomplete on its own. */
HLE_FUNC(cellVoice, cellVoicePausePortAll) {
    std::lock_guard lk(g_lock);
    ret_err(ctx, g_manager.is_init ? 0 : ERR_LIBVOICE_NOT_INIT);
}

HLE_FUNC(cellVoice, cellVoiceResumePortAll) {
    std::lock_guard lk(g_lock);
    ret_err(ctx, g_manager.is_init ? 0 : ERR_LIBVOICE_NOT_INIT);
}

HLE_FUNC(cellVoice, cellVoiceSetMuteFlagAll) {
    std::lock_guard lk(g_lock);
    ret_err(ctx, g_manager.is_init ? 0 : ERR_LIBVOICE_NOT_INIT);
}

/* cellVoiceDebugTopology -- refs/.../cellVoice.cpp:878-886. Not imported by this
 * title. RPCS3's body is UNIMPLEMENTED_FUNC and the answer is
 * CELL_VOICE_ERROR_NOT_IMPLEMENTED; that stays an explicit refusal here. */
HLE_FUNC(cellVoice, cellVoiceDebugTopology) {
    std::lock_guard lk(g_lock);
    if (!g_manager.is_init) return ret_err(ctx, ERR_LIBVOICE_NOT_INIT);
    LOG_WARN("cellVoice", "cellVoiceDebugTopology is not implemented (refs/.../cellVoice.cpp:878-886)");
    ret_err(ctx, static_cast<int32_t>(0x80310809));  /* CELL_VOICE_ERROR_NOT_IMPLEMENTED, :18 */
}

}

/* cellLibprof: two imports the same title reaches, and RPCS3's whole body for
 * both is UNIMPLEMENTED_FUNC + CELL_OK
 * (refs/rpcs3/rpcs3/Emu/Cell/Modules/cellLibprof.cpp:12-21). The console has no
 * user trace backend attached, so the call is a documented no-op there too. */
HLE_FUNC(cellLibprof, cellUserTraceRegister) {
    LOG_DEBUG("cellLibprof", "cellUserTraceRegister -> CELL_OK (no trace backend; cellLibprof.cpp:12-16)");
    hle::ret(ctx, 0);
}

HLE_FUNC(cellLibprof, cellUserTraceUnregister) {
    hle::ret(ctx, 0);
}
