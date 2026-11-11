/* ps3base -- sys_io's keyboard half: cellKb.
 *
 * This runtime has no USB keyboard, and neither does a console with no keyboard
 * plugged in. The console still answers: the query calls succeed and report
 * "nothing connected", the data calls answer CELL_KB_ERROR_NO_DEVICE, and the
 * buffers the caller passed in are left alone. That is what RPCS3 does with its
 * default (null) keyboard handler
 * (refs/rpcs3/rpcs3/Emu/system_config.h:289, refs/rpcs3/rpcs3/Emu/Io/Null/NullKeyboardHandler.h:10-23),
 * and it is the specification followed here -- not a stub, and not CELL_OK from
 * a read that returned no keycodes.
 *
 * The state machine and every constant live in sysio_hle.h so tests/sysio.cpp
 * drives the same decisions this file runs; this file is the guest-memory half:
 * argument extraction, structure writes, return codes.
 *
 * Per function, the reference line whose behaviour is reproduced:
 *   cellKbInit              refs/rpcs3/rpcs3/Emu/Cell/Modules/cellKb.cpp:78-101
 *   cellKbEnd               refs/rpcs3/rpcs3/Emu/Cell/Modules/cellKb.cpp:103-122
 *   cellKbClearBuf          refs/rpcs3/rpcs3/Emu/Cell/Modules/cellKb.cpp:124-157
 *   cellKbGetInfo           refs/rpcs3/rpcs3/Emu/Cell/Modules/cellKb.cpp:309-339
 *   cellKbRead              refs/rpcs3/rpcs3/Emu/Cell/Modules/cellKb.cpp:341-396
 *   cellKbSetCodeType       refs/rpcs3/rpcs3/Emu/Cell/Modules/cellKb.cpp:398-424
 *   cellKbSetLEDStatus      refs/rpcs3/rpcs3/Emu/Cell/Modules/cellKb.cpp:426-453
 *   cellKbSetReadMode       refs/rpcs3/rpcs3/Emu/Cell/Modules/cellKb.cpp:455-485
 *   cellKbGetConfiguration  refs/rpcs3/rpcs3/Emu/Cell/Modules/cellKb.cpp:487-522
 */
#include "common.h"
#include "sysio_hle.h"

#include <cstdint>
#include <cstring>

namespace hle {

namespace {

/* One keyboard consumer, shared by every entry point below. RPCS3 keeps it in
 * the keyboard handler (refs/.../Emu/Io/KeyboardHandler.h:173, m_consumers), one
 * per process, which is what this is. */
sysio::KbState g_kb;

/* true when the caller handed us a pointer inside mapped guest memory: a write
 * to an unmapped address faults, and RPCS3 treats a null pointer as
 * CELL_KB_ERROR_INVALID_PARAMETER rather than dereferencing it. */
bool mapped(uint32_t ea, uint32_t len) {
    return ea && vm::range_mapped(ea, len);
}

}  // namespace

/* cellKbInit( u32 max_connect ), refs/.../cellKb.cpp:78.
 * Returns CELL_OK, CELL_KB_ERROR_ALREADY_INITIALIZED or
 * CELL_KB_ERROR_INVALID_PARAMETER. The sys_config_start at :95 is not
 * reproduced: it creates an lv2 event queue and a PPU thread that only ever
 * forwards pad state changes (refs/.../sys_io_.cpp:144-175). No guest state
 * depends on it, and nothing here claims it ran. */
HLE_FUNC(sys_io, cellKbInit) {
    const uint32_t max_connect = a32(ctx, 0);
    const int32_t rc = g_kb.init(max_connect);
    if (rc == 0)
        LOG_INFO("sys_io", "cellKbInit(%u): no keyboard attached, %u port(s) reported",
                 max_connect, g_kb.max_connect);
    else
        LOG_WARN("sys_io", "cellKbInit(%u) -> 0x%08x", max_connect, static_cast<uint32_t>(rc));
    ret(ctx, rc);
}

/* cellKbEnd( void ), refs/.../cellKb.cpp:103.
 * Returns CELL_OK or CELL_KB_ERROR_UNINITIALIZED. */
HLE_FUNC(sys_io, cellKbEnd) {
    const int32_t rc = g_kb.end();
    if (rc != 0) LOG_WARN("sys_io", "cellKbEnd() -> 0x%08x", static_cast<uint32_t>(rc));
    ret(ctx, rc);
}

/* cellKbClearBuf( u32 port_no ), refs/.../cellKb.cpp:124. */
HLE_FUNC(sys_io, cellKbClearBuf) {
    const uint32_t port_no = a32(ctx, 0);
    const int32_t rc = g_kb.clear_buf(port_no);
    ret(ctx, rc);
}

/* cellKbGetInfo( CellKbInfo* info ), refs/.../cellKb.cpp:309.
 *
 * The guest structure is CellKbInfo (refs/.../cellKb.h:37-43):
 *   u32 max_connect; u32 now_connect; u32 info; u8 status[127];
 * which is 140 bytes with the trailing padding sizeof gives it. RPCS3 zeroes
 * sizeof(CellKbInfo) bytes (:323) and then fills every field, so the all-zero
 * report for an absent keyboard the ticket describes is this memset plus the
 * disconnected statuses -- a real answer, not an empty one. */
HLE_FUNC(sys_io, cellKbGetInfo) {
    const uint32_t info_ea = a32(ctx, 0);
    /* Order from :317-321: uninitialized first, then the pointer. */
    if (!g_kb.initialized) return ret(ctx, sysio::KB_ERROR_UNINITIALIZED);
    if (!mapped(info_ea, sysio::KB_INFO_SIZE)) return ret(ctx, sysio::KB_ERROR_INVALID_PARAMETER);

    std::memset(vm::ptr(info_ea), 0, sysio::KB_INFO_SIZE);
    vm::write32(info_ea + sysio::KB_INFO_OFF_MAX_CONNECT, g_kb.max_connect);
    vm::write32(info_ea + sysio::KB_INFO_OFF_NOW_CONNECT, g_kb.now_connect);
    vm::write32(info_ea + sysio::KB_INFO_OFF_INFO, g_kb.info);
    /* The 127 status bytes come after the three u32 fields (:333-336). They are
     * already zero here; written explicitly so the layout is not implicit. */
    for (uint32_t i = 0; i < sysio::KB_MAX_KEYBOARDS; i++)
        vm::write8(info_ea + sysio::KB_INFO_OFF_STATUS + i, g_kb.status[i]);
    ret(ctx, 0);
}

/* cellKbRead( u32 port_no, CellKbData* data ), refs/.../cellKb.cpp:341.
 *
 * CellKbData (refs/.../cellKb.h:45-51): u32 led; u32 mkey; s32 len; u16 keycode[62].
 * With no keyboard attached the reference returns NO_DEVICE at :359-360, BEFORE
 * any store into data -- so the caller's buffer keeps whatever it held. Writing
 * zeros or a zero length there instead would tell the title "a read happened and
 * no key is down", which is not the same statement. */
HLE_FUNC(sys_io, cellKbRead) {
    const uint32_t port_no = a32(ctx, 0);
    const uint32_t data_ea = a32(ctx, 1);
    /* RPCS3's parameter check (:352) is on the pointer, not on its mapping: an
     * unmapped pointer is still "not null" to it, and with no device the
     * reference returns NO_DEVICE before touching the structure either way. So
     * the mapping is only consulted on the success path, where it stands in for
     * the fault RPCS3 would take. */
    const int32_t rc = g_kb.read(port_no, data_ea != 0);
    if (rc == 0) {
        if (!mapped(data_ea, sysio::KB_DATA_SIZE)) return ret(ctx, sysio::KB_ERROR_INVALID_PARAMETER);
        /* A connected keyboard would be copied here (:366-382). Unreachable with
         * the no-device model, kept so the offsets are written down rather than
         * assumed. */
        vm::write32(data_ea + sysio::KB_DATA_OFF_LED, g_kb.led);
        vm::write32(data_ea + sysio::KB_DATA_OFF_MKEY, g_kb.mkey);
        vm::write32(data_ea + sysio::KB_DATA_OFF_LEN, static_cast<uint32_t>(g_kb.len));
    }
    ret(ctx, rc);
}

/* cellKbSetCodeType( u32 port_no, u32 type ), refs/.../cellKb.cpp:398. */
HLE_FUNC(sys_io, cellKbSetCodeType) {
    const uint32_t port_no = a32(ctx, 0), type = a32(ctx, 1);
    const int32_t rc = g_kb.set_code_type(port_no, type);
    ret(ctx, rc);
}

/* cellKbSetLEDStatus( u32 port_no, u8 led ), refs/.../cellKb.cpp:426.
 * The second parameter is a u8 in the reference signature, so only its low byte
 * is the value the range check at :440 sees. */
HLE_FUNC(sys_io, cellKbSetLEDStatus) {
    const uint32_t port_no = a32(ctx, 0);
    const uint32_t led = a32(ctx, 1) & 0xFFu;
    const int32_t rc = g_kb.set_led_status(port_no, led);
    ret(ctx, rc);
}

/* cellKbSetReadMode( u32 port_no, u32 rmode ), refs/.../cellKb.cpp:455. */
HLE_FUNC(sys_io, cellKbSetReadMode) {
    const uint32_t port_no = a32(ctx, 0), rmode = a32(ctx, 1);
    const int32_t rc = g_kb.set_read_mode(port_no, rmode);
    ret(ctx, rc);
}

/* cellKbGetConfiguration( u32 port_no, CellKbConfig* config ),
 * refs/.../cellKb.cpp:487. Not one of RDR's 14 imports; implemented because it
 * reads the same state and answers the same no-device result, and leaving it
 * unregistered would let the generic path hand back a fabricated CELL_OK.
 *
 * CellKbConfig (refs/.../cellKb.h:53-58): u32 arrange; u32 read_mode; u32 code_type.
 * RPCS3 checks the device status BEFORE the pointer (:507-512), so a caller with
 * a null config and no keyboard gets NO_DEVICE, not INVALID_PARAMETER. */
HLE_FUNC(sys_io, cellKbGetConfiguration) {
    const uint32_t port_no = a32(ctx, 0);
    const uint32_t config_ea = a32(ctx, 1);
    const int32_t rc = g_kb.get_configuration(port_no, mapped(config_ea, sysio::KB_CONFIG_SIZE));
    if (rc != 0) return ret(ctx, rc);
    vm::write32(config_ea + sysio::KB_CONFIG_OFF_ARRANGE, g_kb.arrange[port_no]);
    vm::write32(config_ea + sysio::KB_CONFIG_OFF_READ_MODE, g_kb.read_mode[port_no]);
    vm::write32(config_ea + sysio::KB_CONFIG_OFF_CODE_TYPE, g_kb.code_type[port_no]);
    ret(ctx, 0);
}

}  // namespace hle
