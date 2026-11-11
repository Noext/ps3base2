/* ps3base -- sys_io (cellKb + cellPad), driven through the SAME header the
 * imports use (runtime/src/hle/sysio_hle.h).
 *
 * No game data, no GPU, no network: the subject is the rules, and every rule
 * here is one of the return codes or argument checks RPCS3 performs, cited by
 * line. The properties this suite is built around:
 *
 *   1. the error values are the reference's, sign-extended (a cellKb error is a
 *      NEGATIVE s32);
 *   2. cellKbInit's order is already-initialized BEFORE the range check, so a
 *      failed range check leaves the module initializable;
 *   3. with no keyboard, the query calls succeed and report all-zero statuses
 *      while the data calls answer CELL_KB_ERROR_NO_DEVICE -- and ONLY the data
 *      calls refuse; cellKbSetCodeType on an in-range port is a success;
 *   4. cellKbSetLEDStatus answers FATAL (not NO_DEVICE) for an absent device and
 *      SYS_SETTING_FAILED for an out-of-range LED;
 *   5. cellKbEnd and cellKbInit are symmetric: End, then End again is
 *      UNINITIALIZED, and Init after End succeeds;
 *   6. the pad validates against CELL_MAX_PADS (127), not against 7, and only
 *      port 0 is connected -- the two bugs the previous implementation had;
 *   7. cellPadEnd answers UNINITIALIZED before init, and cellPadGetData never
 *      writes into a buffer it answers NO_DEVICE for.
 */
#include "hle/sysio_hle.h"

#include <cstdio>

namespace sio = hle::sysio;

static int fails = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; std::fprintf(stderr, "[FAIL] %s\n", msg); } \
} while (0)

int main() {
    /* --- 1. the error values, refs/.../cellKb.h:5-15 -------------------- */
    CHECK(sio::KB_ERROR_FATAL == static_cast<int32_t>(0x80121001), "KB_ERROR_FATAL is 0x80121001");
    CHECK(sio::KB_ERROR_INVALID_PARAMETER == static_cast<int32_t>(0x80121002),
          "KB_ERROR_INVALID_PARAMETER is 0x80121002");
    CHECK(sio::KB_ERROR_ALREADY_INITIALIZED == static_cast<int32_t>(0x80121003),
          "KB_ERROR_ALREADY_INITIALIZED is 0x80121003");
    CHECK(sio::KB_ERROR_UNINITIALIZED == static_cast<int32_t>(0x80121004),
          "KB_ERROR_UNINITIALIZED is 0x80121004");
    CHECK(sio::KB_ERROR_NO_DEVICE == static_cast<int32_t>(0x80121007), "KB_ERROR_NO_DEVICE is 0x80121007");
    CHECK(sio::KB_ERROR_SYS_SETTING_FAILED == static_cast<int32_t>(0x80121008),
          "KB_ERROR_SYS_SETTING_FAILED is 0x80121008");
    CHECK(sio::KB_ERROR_NO_DEVICE < 0, "a cellKb error is a NEGATIVE s32");

    /* refs/.../cellPad.h:7-19. */
    CHECK(sio::PAD_ERROR_UNINITIALIZED == static_cast<int32_t>(0x80121104),
          "PAD_ERROR_UNINITIALIZED is 0x80121104");
    CHECK(sio::PAD_ERROR_NO_DEVICE == static_cast<int32_t>(0x80121107), "PAD_ERROR_NO_DEVICE is 0x80121107");
    CHECK(sio::PAD_ERROR_INVALID_PARAMETER == static_cast<int32_t>(0x80121102),
          "PAD_ERROR_INVALID_PARAMETER is 0x80121102");
    CHECK(sio::PAD_ERROR_ALREADY_INITIALIZED == static_cast<int32_t>(0x80121103),
          "PAD_ERROR_ALREADY_INITIALIZED is 0x80121103");
    CHECK(sio::PAD_ERROR_UNSUPPORTED_GAMEPAD == static_cast<int32_t>(0x80121108),
          "PAD_ERROR_UNSUPPORTED_GAMEPAD is 0x80121108");

    /* --- the maxima and structure sizes, refs/.../Keyboard.h:10-14 ------ */
    CHECK(sio::KB_MAX_KEYCODES == 62, "CELL_KB_MAX_KEYCODES is 62");
    CHECK(sio::KB_MAX_KEYBOARDS == 127, "CELL_KB_MAX_KEYBOARDS is 127");
    CHECK(sio::KB_INFO_SIZE == 140, "CellKbInfo is 140 bytes (3 * u32 + u8[127], padded)");
    CHECK(sio::KB_DATA_SIZE == 136, "CellKbData is 136 bytes (3 * u32 + u16[62])");
    CHECK(sio::KB_CONFIG_SIZE == 12, "CellKbConfig is 12 bytes (3 * u32)");
    CHECK(sio::KB_INFO_OFF_STATUS == 12, "the status array starts at offset 12");
    CHECK(sio::KB_DATA_OFF_KEYCODE == 12, "the keycode array starts at offset 12");
    CHECK(sio::PAD_INFO2_SIZE == 124, "CellPadInfo2 is 124 bytes (12 + 4 * 28)");
    CHECK(sio::PAD_DATA_SIZE == 132, "CellPadData is 132 bytes (4 + 128)");
    CHECK(sio::PAD_MAX_PORT_NUM == 7 && sio::PAD_MAX_PADS == 127,
          "CELL_PAD_MAX_PORT_NUM is 7 and CELL_MAX_PADS is 127");
    CHECK(sio::PAD_CAPABILITY_STANDARD == 0x1F,
          "the standard capability set is 0x1F, the 0x7 the old handler reported was wrong");

    /* --- 2./3. cellKbInit's order and the no-device state -------------- */
    {
        sio::KbState kb;
        /* Uninitialized first: every entry point refuses with UNINITIALIZED,
         * refs/.../cellKb.cpp:317-318, :349-350, :406-407, :434-435, :463-464,
         * :495-496. */
        CHECK(kb.read(0, true) == sio::KB_ERROR_UNINITIALIZED, "cellKbRead before Init is UNINITIALIZED");
        CHECK(kb.clear_buf(0) == sio::KB_ERROR_UNINITIALIZED, "cellKbClearBuf before Init is UNINITIALIZED");
        CHECK(kb.set_code_type(0, 0) == sio::KB_ERROR_UNINITIALIZED,
              "cellKbSetCodeType before Init is UNINITIALIZED");
        CHECK(kb.set_led_status(0, 0) == sio::KB_ERROR_UNINITIALIZED,
              "cellKbSetLEDStatus before Init is UNINITIALIZED");
        CHECK(kb.set_read_mode(0, 0) == sio::KB_ERROR_UNINITIALIZED,
              "cellKbSetReadMode before Init is UNINITIALIZED");
        CHECK(kb.get_configuration(0, true) == sio::KB_ERROR_UNINITIALIZED,
              "cellKbGetConfiguration before Init is UNINITIALIZED");
        CHECK(kb.end() == sio::KB_ERROR_UNINITIALIZED, "cellKbEnd before Init is UNINITIALIZED");

        /* The range check, refs/.../cellKb.cpp:89-93: 0 and >127 are invalid. */
        CHECK(kb.init(0) == sio::KB_ERROR_INVALID_PARAMETER, "cellKbInit(0) is INVALID_PARAMETER");
        CHECK(kb.init(128) == sio::KB_ERROR_INVALID_PARAMETER, "cellKbInit(128) is INVALID_PARAMETER");
        /* A cancelled init leaves the module initializable (:91 init.cancel()). */
        CHECK(!kb.initialized, "a failed cellKbInit did not leave the module initialized");
        CHECK(kb.init(1) == 0, "cellKbInit(1) succeeds after a rejected one");
        CHECK(kb.initialized, "cellKbInit sets the initialized flag");
        /* The clamp at :98: std::min(max_connect, 7u). */
        CHECK(kb.max_connect == 1, "cellKbInit(1) reports max_connect 1");
        CHECK(kb.init(2) == sio::KB_ERROR_ALREADY_INITIALIZED,
              "a second cellKbInit is ALREADY_INITIALIZED, checked before the range test");
        CHECK(kb.init(0) == sio::KB_ERROR_ALREADY_INITIALIZED,
              "ALREADY_INITIALIZED wins over the parameter check -- the reference's order");

        /* The no-device report: max_connect set, now_connect 0, status all 0. */
        CHECK(kb.max_connect == 1, "the failed re-init did not change max_connect");
        CHECK(kb.now_connect == 0, "now_connect is 0 with no keyboard");
        CHECK(kb.info == 0, "KbInfo.info is 0 (not intercepted)");
        bool all_disconnected = true;
        for (uint32_t i = 0; i < sio::KB_MAX_KEYBOARDS; i++)
            if (kb.status[i] != sio::KB_STATUS_DISCONNECTED) all_disconnected = false;
        CHECK(all_disconnected, "all 127 status bytes are DISCONNECTED -- the Info buffer stays zero");

        /* The data calls refuse; refs/.../cellKb.cpp:359-360, :143-144. */
        CHECK(kb.read(0, true) == sio::KB_ERROR_NO_DEVICE,
              "cellKbRead on a valid pointer with no device is NO_DEVICE");
        CHECK(kb.clear_buf(0) == sio::KB_ERROR_NO_DEVICE, "cellKbClearBuf with no device is NO_DEVICE");
        /* A null pointer is still a parameter error, checked BEFORE the device
         * (:352). */
        CHECK(kb.read(0, false) == sio::KB_ERROR_INVALID_PARAMETER,
              "cellKbRead with a null pointer is INVALID_PARAMETER, before the device test");
        /* A port outside the 127 range is a parameter error before the device
         * test too. */
        CHECK(kb.read(127, true) == sio::KB_ERROR_INVALID_PARAMETER,
              "cellKbRead(127,...) is INVALID_PARAMETER (127 is not a port)");

        /* cellKbSetCodeType stores nothing for an absent keyboard but is a
         * SUCCESS for a port inside the consumer's range that exists, and for a
         * port beyond it (:415-416). Only the range and the type are errors. */
        CHECK(kb.set_code_type(0, sio::KB_CODETYPE_RAW) == 0,
              "cellKbSetCodeType on an in-range port is CELL_OK even with no device");
        CHECK(kb.set_code_type(0, 2) == sio::KB_ERROR_INVALID_PARAMETER,
              "cellKbSetCodeType(type 2) is INVALID_PARAMETER");
        CHECK(kb.set_code_type(127, 0) == sio::KB_ERROR_INVALID_PARAMETER,
              "cellKbSetCodeType(127,...) is INVALID_PARAMETER");
        CHECK(kb.set_read_mode(0, sio::KB_RMODE_PACKET) == 0,
              "cellKbSetReadMode on an in-range port is CELL_OK even with no device");
        CHECK(kb.set_read_mode(0, 2) == sio::KB_ERROR_INVALID_PARAMETER,
              "cellKbSetReadMode(mode 2) is INVALID_PARAMETER");

        /* 4. cellKbSetLEDStatus: FATAL for an absent device (:446-447),
         * SYS_SETTING_FAILED for an LED above 7 (:440-441). */
        CHECK(kb.set_led_status(0, 0) == sio::KB_ERROR_FATAL,
              "cellKbSetLEDStatus with no device is FATAL, not NO_DEVICE");
        CHECK(kb.set_led_status(0, 8) == sio::KB_ERROR_SYS_SETTING_FAILED,
              "cellKbSetLEDStatus(8) is SYS_SETTING_FAILED");
        CHECK(kb.set_led_status(0, 7) != sio::KB_ERROR_SYS_SETTING_FAILED,
              "cellKbSetLEDStatus(7) is in range -- 7 is the boundary, not an error");
        CHECK(kb.set_led_status(127, 0) == sio::KB_ERROR_INVALID_PARAMETER,
              "cellKbSetLEDStatus(127,...) is INVALID_PARAMETER, before the LED range");

        /* 5. End/Init symmetry, refs/.../cellKb.cpp:103-122 and :78-101. */
        CHECK(kb.end() == 0, "cellKbEnd after Init is CELL_OK");
        CHECK(!kb.initialized, "cellKbEnd clears the initialized flag");
        CHECK(kb.end() == sio::KB_ERROR_UNINITIALIZED, "cellKbEnd twice is UNINITIALIZED");
        CHECK(kb.init(4) == 0, "cellKbInit after cellKbEnd succeeds");
        CHECK(kb.max_connect == 4, "the re-init reports the new max_connect");
    }

    /* cellKbGetConfiguration's check order, refs/.../cellKb.cpp:507-512: the
     * device status before the pointer. */
    {
        sio::KbState kb;
        kb.init(1);
        CHECK(kb.get_configuration(0, false) == sio::KB_ERROR_NO_DEVICE,
              "a NULL config with no keyboard is NO_DEVICE -- the device test comes first");
        CHECK(kb.get_configuration(127, false) == sio::KB_ERROR_INVALID_PARAMETER,
              "cellKbGetConfiguration(127,...) is INVALID_PARAMETER, which comes before the device test");
    }

    /* A connected keyboard takes the other path. Not a state this runtime can
     * produce, but the branches are asserted so a future device cannot silently
     * break them. */
    {
        sio::KbState kb;
        kb.init(1);
        kb.status[0] = sio::KB_STATUS_CONNECTED;
        kb.now_connect = 1;
        CHECK(kb.read(0, true) == 0, "a connected keyboard makes cellKbRead succeed");
        CHECK(kb.clear_buf(0) == 0, "a connected keyboard makes cellKbClearBuf succeed");
        CHECK(kb.set_led_status(0, 4) == 0, "a connected keyboard accepts an LED value");
        CHECK(kb.led == 4, "cellKbSetLEDStatus stored the LED value");
        CHECK(kb.get_configuration(0, true) == 0,
              "a connected keyboard makes cellKbGetConfiguration succeed");
        CHECK(kb.get_configuration(0, false) == sio::KB_ERROR_INVALID_PARAMETER,
              "with a device, a null config is INVALID_PARAMETER");
        CHECK(kb.code_type[0] == sio::KB_CODETYPE_ASCII,
              "the default code type is ASCII (KeyboardHandler.h:99)");
        CHECK(kb.read_mode[0] == sio::KB_RMODE_INPUTCHAR,
              "the default read mode is INPUTCHAR (KeyboardHandler.h:98)");
        CHECK(kb.arrange[0] == 0, "the default arrangement is CELL_KB_MAPPING_101");
    }

    /* --- 6./7. the pad ------------------------------------------------ */
    {
        sio::PadState pad;
        CHECK(pad.end() == sio::PAD_ERROR_UNINITIALIZED,
              "cellPadEnd before Init is UNINITIALIZED (it used to answer CELL_OK)");
        CHECK(pad.get_data(0, true) == sio::PAD_ERROR_UNINITIALIZED,
              "cellPadGetData before Init is UNINITIALIZED");
        CHECK(pad.set_port_setting(0, 0) == sio::PAD_ERROR_UNINITIALIZED,
              "cellPadSetPortSetting before Init is UNINITIALIZED");
        CHECK(pad.set_act_direct(0, 0, true, true) == sio::PAD_ERROR_UNINITIALIZED,
              "cellPadSetActDirect before Init is UNINITIALIZED");
        CHECK(pad.get_info2(true) == sio::PAD_ERROR_UNINITIALIZED,
              "cellPadGetInfo2 before Init is UNINITIALIZED");

        /* The range is CELL_MAX_PADS = 127, refs/.../cellPad.cpp:268-269. The old
         * handler clamped to 7 here and accepted 0. */
        CHECK(pad.init(0) == sio::PAD_ERROR_INVALID_PARAMETER, "cellPadInit(0) is INVALID_PARAMETER");
        CHECK(pad.init(128) == sio::PAD_ERROR_INVALID_PARAMETER, "cellPadInit(128) is INVALID_PARAMETER");
        CHECK(pad.init(8) == 0, "cellPadInit(8) is VALID -- the range is 127, not 7");
        CHECK(pad.max_connect == 8, "cellPadInit(8) keeps max_connect 8");
        CHECK(pad.get_max_connect() == 7, "get_max_connect clamps 8 to CELL_PAD_MAX_PORT_NUM (7)");
        CHECK(pad.init(1) == sio::PAD_ERROR_ALREADY_INITIALIZED, "a second cellPadInit is ALREADY_INITIALIZED");

        /* One pad, on port 0. */
        CHECK(pad.is_reportedly_connected(0), "port 0 is reported connected");
        CHECK(!pad.is_reportedly_connected(1), "port 1 is NOT connected -- one host pad, not seven");
        CHECK(pad.now_connect() == 1, "now_connect is 1, not 7");
        CHECK(pad.capability(0) == 0x1F, "port 0 carries the standard capability set");
        CHECK(pad.capability(1) == 0, "a disconnected port carries no capability");
        CHECK(pad.device_type(0) == sio::PAD_DEV_TYPE_STANDARD, "port 0 is a standard pad");

        /* cellPadGetData's check order, refs/.../cellPad.cpp:703-717. */
        CHECK(pad.get_data(0, true) == 0, "cellPadGetData(0) with the host pad is CELL_OK");
        CHECK(pad.get_data(127, true) == sio::PAD_ERROR_INVALID_PARAMETER,
              "cellPadGetData(127,...) is INVALID_PARAMETER");
        CHECK(pad.get_data(0, false) == sio::PAD_ERROR_INVALID_PARAMETER,
              "cellPadGetData with a null data pointer is INVALID_PARAMETER");
        CHECK(pad.get_data(7, true) == sio::PAD_ERROR_NO_DEVICE,
              "cellPadGetData(7) with max_connect 8 is NO_DEVICE (beyond the clamp)");
        CHECK(pad.get_data(1, true) == sio::PAD_ERROR_NO_DEVICE,
              "cellPadGetData(1) is NO_DEVICE -- no second pad");

        /* cellPadSetPortSetting: only PRESS and SENSOR are stored, and a port in
         * 0..127 beyond the clamp is a CELL_OK that stores nothing. */
        CHECK(pad.port_setting[0] == 0, "the port setting starts at 0 (PRESS_OFF | SENSOR_OFF)");
        CHECK(pad.set_port_setting(0, sio::PAD_SETTING_PRESS_ON) == 0, "cellPadSetPortSetting succeeds");
        CHECK(pad.port_setting[0] == sio::PAD_SETTING_PRESS_ON, "the PRESS bit was stored");
        CHECK(pad.set_port_setting(0, sio::PAD_SETTING_SENSOR_ON) == 0, "a second set succeeds");
        CHECK(pad.port_setting[0] == sio::PAD_SETTING_SENSOR_ON, "the PRESS bit was cleared by omission");
        CHECK(pad.set_port_setting(127, 0) == sio::PAD_ERROR_INVALID_PARAMETER,
              "cellPadSetPortSetting(127,...) is INVALID_PARAMETER");
        CHECK(pad.set_port_setting(100, 0) == 0,
              "a port in 7..126 is CELL_OK and stores nothing -- not NO_DEVICE");

        /* cellPadSetActDirect, refs/.../cellPad.cpp:871-904. */
        CHECK(pad.set_act_direct(0, 0x430001, true, true) == 0,
              "cellPadSetActDirect on the host pad is CELL_OK (it has an actuator)");
        CHECK(pad.set_act_direct(0, 0x430001, false, true) == sio::PAD_ERROR_INVALID_PARAMETER,
              "a null param is INVALID_PARAMETER");
        CHECK(pad.set_act_direct(0, 0x430001, true, false) == sio::PAD_ERROR_INVALID_PARAMETER,
              "a non-zero reserved byte is INVALID_PARAMETER for a modern SDK");
        /* The reserved-bit test is skipped for an SDK at or below 0x1FFFFF (:884). */
        CHECK(pad.set_act_direct(0, 0x1FFFFF, true, false) == 0,
              "an old SDK skips the reserved-bit test -- the comparison is signed and version-gated");
        CHECK(pad.set_act_direct(1, 0x430001, true, true) == sio::PAD_ERROR_NO_DEVICE,
              "cellPadSetActDirect(1) is NO_DEVICE");
        CHECK(pad.set_act_direct(127, 0x430001, true, true) == sio::PAD_ERROR_INVALID_PARAMETER,
              "cellPadSetActDirect(127) is INVALID_PARAMETER");

        /* The reported length follows the port setting,
         * refs/.../cellPad.cpp:539-565. A constant 24 handed every title sensor
         * fields it had not asked for. */
        CHECK(sio::PadState::data_len(0) == 8,
              "no setting reports CELL_PAD_LEN_CHANGE_DEFAULT (8)");
        CHECK(sio::PadState::data_len(sio::PAD_SETTING_PRESS_ON) == 20,
              "PRESS_ON reports CELL_PAD_LEN_CHANGE_PRESS_ON (20)");
        CHECK(sio::PadState::data_len(sio::PAD_SETTING_SENSOR_ON) == 24,
              "SENSOR_ON reports CELL_PAD_LEN_CHANGE_SENSOR_ON (24)");
        CHECK(sio::PadState::data_len(sio::PAD_SETTING_PRESS_ON | sio::PAD_SETTING_SENSOR_ON) == 24,
              "SENSOR_ON wins over PRESS_ON, as the reference orders the two tests");
        CHECK(sio::PAD_LEN_CHANGE_DEFAULT == 8 && sio::PAD_LEN_CHANGE_PRESS_ON == 20 &&
                  sio::PAD_LEN_CHANGE_SENSOR_ON == 24,
              "the three lengths are 8, 20 and 24 (cellPad.h:27-33)");

        /* cellPadGetInfo2's argument check, refs/.../cellPad.cpp:964-968. */
        CHECK(pad.get_info2(false) == sio::PAD_ERROR_INVALID_PARAMETER,
              "cellPadGetInfo2 with a null pointer is INVALID_PARAMETER");
        CHECK(pad.get_info2(true) == 0, "cellPadGetInfo2 with a pointer is CELL_OK");

        CHECK(pad.end() == 0, "cellPadEnd after Init is CELL_OK");
        CHECK(pad.max_connect == 0, "cellPadEnd resets max_connect");
        CHECK(pad.end() == sio::PAD_ERROR_UNINITIALIZED, "cellPadEnd twice is UNINITIALIZED");
        CHECK(pad.init(1) == 0, "cellPadInit after cellPadEnd succeeds");
    }

    /* The maximum accepted argument, both modules: 127 is in range for the
     * init and out of range for the data calls. */
    {
        sio::KbState kb;
        CHECK(kb.init(127) == 0, "cellKbInit(127) is the largest valid argument");
        CHECK(kb.max_connect == 7, "cellKbInit(127) clamps the reported count to 7");
        sio::PadState pad;
        CHECK(pad.init(127) == 0, "cellPadInit(127) is the largest valid argument");
        CHECK(pad.get_max_connect() == 7, "cellPadInit(127) clamps to 7");
    }

    std::printf("sysio: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
