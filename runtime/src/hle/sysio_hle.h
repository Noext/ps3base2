/* ps3base -- the decision logic of sys_io in a header, so a unit test drives
 * the same code the imports run.
 *
 * sys_io is the USB input module: the pad (cellPad), the keyboard (cellKb) and
 * the mouse (cellMouse). Red Dead Redemption imports 14 of its functions --
 * 6 pad and 8 keyboard, no mouse -- and this header holds the argument checks,
 * the state, and the return code for each, because those are the part that has
 * to match the console.
 *
 * KEYBOARD: this runtime has no USB keyboard, and neither does a console with
 * no keyboard plugged in. The console still answers: the query calls succeed and
 * report "nothing connected", the data calls answer CELL_KB_ERROR_NO_DEVICE, and
 * the buffers the caller passed in are left alone. That is what RPCS3 does with
 * its default (null) keyboard handler
 * (refs/rpcs3/rpcs3/Emu/system_config.h:289
 * "cfg::_enum<keyboard_handler> keyboard{ this, "Keyboard", keyboard_handler::null };",
 * refs/rpcs3/rpcs3/Emu/Io/Null/NullKeyboardHandler.h:10-23, which leaves every
 * status[] at 0). So the Info buffers the ticket describes as "restent a zero"
 * are a real answer, not a stub.
 *
 * PAD: one pad exists on port 0 and is fed from the host device, so the pad
 * calls take their connected-device path -- the same path a console takes with a
 * DualShock plugged in.
 *
 * Sources, by name and line:
 *   Keyboard
 *     refs/rpcs3/rpcs3/Emu/Cell/Modules/cellKb.h:5-15       CellKbError
 *     refs/rpcs3/rpcs3/Emu/Cell/Modules/cellKb.h:37-58      CellKbInfo/Data/Config
 *     refs/rpcs3/rpcs3/Emu/Io/Keyboard.h:5-32               info bit, maxima, status, modes
 *     refs/rpcs3/rpcs3/Emu/Io/KeyboardHandler.h:58-65       KbInfo defaults
 *     refs/rpcs3/rpcs3/Emu/Io/KeyboardHandler.h:95-100      KbConfig defaults
 *     refs/rpcs3/rpcs3/Emu/Cell/Modules/cellKb.cpp:78-522   the ten handlers
 *   Pad
 *     refs/rpcs3/rpcs3/Emu/Cell/Modules/cellPad.h:7-19      CellPadError
 *     refs/rpcs3/rpcs3/Emu/Cell/Modules/cellPad.h:55-99     CellPadInfo/Info2/CapabilityInfo
 *     refs/rpcs3/rpcs3/Emu/Cell/Modules/cellPad.h:117-140   pad_info, get_max_connect,
 *                                                           is_reportedly_connected
 *     refs/rpcs3/rpcs3/Emu/Io/pad_types.h:87-102           port status and settings
 *     refs/rpcs3/rpcs3/Emu/Io/pad_types.h:134-148          capabilities, device types
 *     refs/rpcs3/rpcs3/Emu/Io/pad_types.h:345-358          maxima and CellPadData
 *     refs/rpcs3/rpcs3/Emu/Cell/Modules/cellPad.cpp:257-289 cellPadInit
 *     refs/rpcs3/rpcs3/Emu/Cell/Modules/cellPad.cpp:291-304 cellPadEnd
 *     refs/rpcs3/rpcs3/Emu/Cell/Modules/cellPad.cpp:695-723 cellPadGetData
 *     refs/rpcs3/rpcs3/Emu/Cell/Modules/cellPad.cpp:868-909 cellPadSetActDirect
 *     refs/rpcs3/rpcs3/Emu/Cell/Modules/cellPad.cpp:956-1003 cellPadGetInfo2
 *     refs/rpcs3/rpcs3/Emu/Cell/Modules/cellPad.cpp:1036-1067 cellPadSetPortSetting
 *     refs/rpcs3/rpcs3/Emu/Io/PadHandler.cpp:518            standard-pad capabilities
 */
#ifndef PS3BASE_HLE_SYSIO_H
#define PS3BASE_HLE_SYSIO_H

#include <cstdint>

namespace hle {
namespace sysio {

/* ---------------------------------------------------------------- keyboard */

/* refs/.../cellKb.h:5-15. A cellKb error is a NEGATIVE s32. */
constexpr int32_t KB_ERROR_FATAL = static_cast<int32_t>(0x80121001);
constexpr int32_t KB_ERROR_INVALID_PARAMETER = static_cast<int32_t>(0x80121002);
constexpr int32_t KB_ERROR_ALREADY_INITIALIZED = static_cast<int32_t>(0x80121003);
constexpr int32_t KB_ERROR_UNINITIALIZED = static_cast<int32_t>(0x80121004);
constexpr int32_t KB_ERROR_RESOURCE_ALLOCATION_FAILED = static_cast<int32_t>(0x80121005);
constexpr int32_t KB_ERROR_READ_FAILED = static_cast<int32_t>(0x80121006);
constexpr int32_t KB_ERROR_NO_DEVICE = static_cast<int32_t>(0x80121007);
constexpr int32_t KB_ERROR_SYS_SETTING_FAILED = static_cast<int32_t>(0x80121008);

/* refs/.../Keyboard.h:10-14. */
constexpr uint32_t KB_MAX_KEYCODES = 62;
constexpr uint32_t KB_MAX_KEYBOARDS = 127;

/* refs/.../Keyboard.h:5-8. */
constexpr uint32_t KB_INFO_INTERCEPTED = 1;

/* refs/.../Keyboard.h:16-20. */
constexpr uint8_t KB_STATUS_DISCONNECTED = 0x00;
constexpr uint8_t KB_STATUS_CONNECTED = 0x01;

/* refs/.../Keyboard.h:22-26. */
constexpr uint32_t KB_RMODE_INPUTCHAR = 0;
constexpr uint32_t KB_RMODE_PACKET = 1;

/* refs/.../Keyboard.h:28-32. */
constexpr uint32_t KB_CODETYPE_RAW = 0;
constexpr uint32_t KB_CODETYPE_ASCII = 1;

/* cellKbInit passes std::min(max_connect, 7u) to the handler
 * (refs/.../cellKb.cpp:98), so that is the number of Keyboard objects the
 * consumer holds -- not KB_MAX_KEYBOARDS, which is only the port range check.
 * The same 7 is CELL_PAD_MAX_PORT_NUM (refs/.../pad_types.h:348). */
constexpr uint32_t KB_CONSUMER_PORTS = 7;

/* Sizes of the guest structures, from the field lists in cellKb.h:37-58. sizeof
 * gives CellKbInfo 140 bytes (3 * u32 + u8[127], rounded up for alignment),
 * CellKbData 136 (3 * u32 + u16[62]) and CellKbConfig 12 (3 * u32).
 * cellKbGetInfo memsets info.size() bytes (refs/.../cellKb.cpp:323), i.e. the
 * padded 140. */
constexpr uint32_t KB_INFO_SIZE = 140;
constexpr uint32_t KB_DATA_SIZE = 136;
constexpr uint32_t KB_CONFIG_SIZE = 12;

/* Byte offsets inside the guest structures, from those same field lists. */
constexpr uint32_t KB_INFO_OFF_MAX_CONNECT = 0;
constexpr uint32_t KB_INFO_OFF_NOW_CONNECT = 4;
constexpr uint32_t KB_INFO_OFF_INFO = 8;
constexpr uint32_t KB_INFO_OFF_STATUS = 12;
constexpr uint32_t KB_DATA_OFF_LED = 0;
constexpr uint32_t KB_DATA_OFF_MKEY = 4;
constexpr uint32_t KB_DATA_OFF_LEN = 8;
constexpr uint32_t KB_DATA_OFF_KEYCODE = 12;
constexpr uint32_t KB_CONFIG_OFF_ARRANGE = 0;
constexpr uint32_t KB_CONFIG_OFF_READ_MODE = 4;
constexpr uint32_t KB_CONFIG_OFF_CODE_TYPE = 8;

/* The keyboard consumer's state, as far as the guest can observe it.
 *
 * With RPCS3's default (null) handler every keyboard reports DISCONNECTED, so
 * cellKbRead and cellKbClearBuf return NO_DEVICE (cellKb.cpp:359-360 and
 * :143-144) and cellKbSetLEDStatus returns FATAL (:446-447). The status array is
 * kept, rather than replaced by a boolean, because cellKbGetInfo writes all
 * KB_MAX_KEYBOARDS of them (:333-336). */
struct KbState {
    bool initialized = false;
    /* KbInfo.max_connect, already clamped to the consumer's port count: RPCS3
     * stores the clamped value in the consumer's info (:98) and reports it back
     * verbatim (:329). */
    uint32_t max_connect = 0;
    uint32_t now_connect = 0;
    uint32_t info = 0;
    uint8_t status[KB_MAX_KEYBOARDS] = {};
    /* Per-port KbConfig, defaults from KeyboardHandler.h:95-100: US 101,
     * inputchar, ASCII. */
    uint32_t arrange[KB_CONSUMER_PORTS] = {};
    uint32_t read_mode[KB_CONSUMER_PORTS] = {};
    uint32_t code_type[KB_CONSUMER_PORTS] = {KB_CODETYPE_ASCII, KB_CODETYPE_ASCII,
                                             KB_CODETYPE_ASCII, KB_CODETYPE_ASCII,
                                             KB_CODETYPE_ASCII, KB_CODETYPE_ASCII,
                                             KB_CODETYPE_ASCII};
    /* CellKbData: no key was ever read because no device is attached. */
    uint32_t led = 0;
    uint32_t mkey = 0;
    int32_t len = 0;

    uint32_t keyboards() const { return max_connect; }
    bool connected(uint32_t port) const {
        return port < max_connect && status[port] == KB_STATUS_CONNECTED;
    }

    /* cellKbInit(max_connect), refs/.../cellKb.cpp:78-101.
     * Order matters: the already-initialized test comes BEFORE the parameter
     * test, because RPCS3 calls init.init() first (:84) and only then validates
     * (:89). A failed range check cancels the init (:91), so a later valid
     * cellKbInit still succeeds. */
    int32_t init(uint32_t max) {
        if (initialized) return KB_ERROR_ALREADY_INITIALIZED;
        if (max == 0 || max > KB_MAX_KEYBOARDS) return KB_ERROR_INVALID_PARAMETER;
        initialized = true;
        max_connect = max < KB_CONSUMER_PORTS ? max : KB_CONSUMER_PORTS;
        now_connect = 0;  /* NullKeyboardHandler.h:15-17: no device. */
        info = 0;
        for (uint32_t i = 0; i < KB_MAX_KEYBOARDS; i++) status[i] = KB_STATUS_DISCONNECTED;
        for (uint32_t i = 0; i < KB_CONSUMER_PORTS; i++) {
            arrange[i] = 0;  /* CELL_KB_MAPPING_101, Keyboard.h:183. */
            read_mode[i] = KB_RMODE_INPUTCHAR;
            code_type[i] = KB_CODETYPE_ASCII;
        }
        led = 0;
        mkey = 0;
        len = 0;
        return 0;
    }

    /* cellKbEnd, refs/.../cellKb.cpp:103-122. */
    int32_t end() {
        if (!initialized) return KB_ERROR_UNINITIALIZED;
        initialized = false;
        max_connect = 0;
        now_connect = 0;
        info = 0;
        for (uint32_t i = 0; i < KB_MAX_KEYBOARDS; i++) status[i] = KB_STATUS_DISCONNECTED;
        return 0;
    }

    /* cellKbClearBuf, refs/.../cellKb.cpp:124-157. */
    int32_t clear_buf(uint32_t port) {
        if (!initialized) return KB_ERROR_UNINITIALIZED;
        if (port >= KB_MAX_KEYBOARDS) return KB_ERROR_INVALID_PARAMETER;
        if (port >= keyboards() || status[port] != KB_STATUS_CONNECTED)
            return KB_ERROR_NO_DEVICE;
        /* Reached only with a connected keyboard; the buffer is emptied the way
         * :146-154 does it. */
        led = 0;
        mkey = 0;
        len = 0;
        return 0;
    }

    /* cellKbRead(port_no, data). The pointer test is the caller's: this returns
     * INVALID_PARAMETER for a null data pointer, and NO_DEVICE when the data
     * pointer is valid but nothing is attached -- in which case RPCS3 writes
     * NOTHING into the caller's CellKbData (:359-360 returns before any store),
     * so neither does the handler. */
    int32_t read(uint32_t port, bool has_data) {
        if (!initialized) return KB_ERROR_UNINITIALIZED;
        if (port >= KB_MAX_KEYBOARDS || !has_data) return KB_ERROR_INVALID_PARAMETER;
        if (port >= keyboards() || status[port] != KB_STATUS_CONNECTED)
            return KB_ERROR_NO_DEVICE;
        /* A connected keyboard would copy led/mkey/len and the keycodes here
         * (:366-375). There is no device to copy from. */
        return 0;
    }

    /* cellKbSetCodeType, refs/.../cellKb.cpp:398-424. */
    int32_t set_code_type(uint32_t port, uint32_t type) {
        if (!initialized) return KB_ERROR_UNINITIALIZED;
        if (port >= KB_MAX_KEYBOARDS || type > KB_CODETYPE_ASCII)
            return KB_ERROR_INVALID_PARAMETER;
        /* A port inside the range but beyond the consumer's keyboards is a
         * success that stores nothing (:415-416) -- not NO_DEVICE. */
        if (port >= keyboards()) return 0;
        code_type[port] = type;
        return 0;
    }

    /* cellKbSetLEDStatus, refs/.../cellKb.cpp:426-453. The disconnected case is
     * FATAL here, not NO_DEVICE (:446-447). */
    int32_t set_led_status(uint32_t port, uint32_t value) {
        if (!initialized) return KB_ERROR_UNINITIALIZED;
        if (port >= KB_MAX_KEYBOARDS) return KB_ERROR_INVALID_PARAMETER;
        if (value > 7) return KB_ERROR_SYS_SETTING_FAILED;
        if (port >= keyboards() || status[port] != KB_STATUS_CONNECTED)
            return KB_ERROR_FATAL;
        led = value;
        return 0;
    }

    /* cellKbSetReadMode, refs/.../cellKb.cpp:455-485. */
    int32_t set_read_mode(uint32_t port, uint32_t rmode) {
        if (!initialized) return KB_ERROR_UNINITIALIZED;
        if (port >= KB_MAX_KEYBOARDS || rmode > KB_RMODE_PACKET)
            return KB_ERROR_INVALID_PARAMETER;
        if (port >= keyboards()) return 0;
        read_mode[port] = rmode;
        return 0;
    }

    /* cellKbGetConfiguration(port_no, config) -- the sequence of checks is
     * RPCS3's: init, port range, THEN device status, and only then the pointer
     * (:495-512), with the comment "tests show that config is checked only after
     * the device's status". */
    int32_t get_configuration(uint32_t port, bool has_config) {
        if (!initialized) return KB_ERROR_UNINITIALIZED;
        if (port >= KB_MAX_KEYBOARDS) return KB_ERROR_INVALID_PARAMETER;
        if (port >= keyboards() || status[port] != KB_STATUS_CONNECTED)
            return KB_ERROR_NO_DEVICE;
        if (!has_config) return KB_ERROR_INVALID_PARAMETER;
        return 0;
    }
};

/* --------------------------------------------------------------------- pad */

/* refs/.../cellPad.h:7-19. */
constexpr int32_t PAD_ERROR_FATAL = static_cast<int32_t>(0x80121101);
constexpr int32_t PAD_ERROR_INVALID_PARAMETER = static_cast<int32_t>(0x80121102);
constexpr int32_t PAD_ERROR_ALREADY_INITIALIZED = static_cast<int32_t>(0x80121103);
constexpr int32_t PAD_ERROR_UNINITIALIZED = static_cast<int32_t>(0x80121104);
constexpr int32_t PAD_ERROR_RESOURCE_ALLOCATION_FAILED = static_cast<int32_t>(0x80121105);
constexpr int32_t PAD_ERROR_DATA_READ_FAILED = static_cast<int32_t>(0x80121106);
constexpr int32_t PAD_ERROR_NO_DEVICE = static_cast<int32_t>(0x80121107);
constexpr int32_t PAD_ERROR_UNSUPPORTED_GAMEPAD = static_cast<int32_t>(0x80121108);
constexpr int32_t PAD_ERROR_TOO_MANY_DEVICES = static_cast<int32_t>(0x80121109);
constexpr int32_t PAD_ERROR_EBUSY = static_cast<int32_t>(0x8012110a);

/* refs/.../pad_types.h:345-352. */
constexpr uint32_t PAD_ACTUATOR_MAX = 2;
constexpr uint32_t PAD_MAX_PORT_NUM = 7;
constexpr uint32_t PAD_MAX_CODES = 64;
constexpr uint32_t PAD_MAX_PADS = 127;

/* refs/.../pad_types.h:87-102. */
constexpr uint32_t PAD_STATUS_CONNECTED = 0x00000001;
constexpr uint32_t PAD_SETTING_PRESS_ON = 0x00000002;
constexpr uint32_t PAD_SETTING_SENSOR_ON = 0x00000004;

/* The lengths cellPadGetData reports, refs/.../cellPad.h:27-33. */
constexpr uint32_t PAD_LEN_NO_CHANGE = 0;
constexpr uint32_t PAD_LEN_CHANGE_DEFAULT = 8;
constexpr uint32_t PAD_LEN_CHANGE_PRESS_ON = 20;
constexpr uint32_t PAD_LEN_CHANGE_SENSOR_ON = 24;

/* refs/.../pad_types.h:134-141. The standard pad's set, which is what
 * PadHandler.cpp:518 advertises: conformity, press mode, high-precision sticks,
 * actuator, sensor mode. */
constexpr uint32_t PAD_CAPABILITY_CONFORMITY = 0x1;
constexpr uint32_t PAD_CAPABILITY_PRESS_MODE = 0x2;
constexpr uint32_t PAD_CAPABILITY_SENSOR_MODE = 0x4;
constexpr uint32_t PAD_CAPABILITY_HP_ANALOG_STICK = 0x8;
constexpr uint32_t PAD_CAPABILITY_ACTUATOR = 0x10;
constexpr uint32_t PAD_CAPABILITY_STANDARD = PAD_CAPABILITY_CONFORMITY | PAD_CAPABILITY_PRESS_MODE |
                                             PAD_CAPABILITY_HP_ANALOG_STICK |
                                             PAD_CAPABILITY_ACTUATOR | PAD_CAPABILITY_SENSOR_MODE;

/* refs/.../pad_types.h:143-148. */
constexpr uint32_t PAD_DEV_TYPE_STANDARD = 0;

/* CellPadData: s32 len then u16 button[64] = 4 + 128 = 132 bytes
 * (refs/.../pad_types.h:354-358). */
constexpr uint32_t PAD_DATA_SIZE = 132;

/* CellPadInfo2 (refs/.../cellPad.h:65-74): three u32 then four u32[7] arrays,
 * 12 + 112 = 124 bytes. cellPadGetInfo2 memsets sizeof(CellPadInfo2)
 * (refs/.../cellPad.cpp:974), which is the 124. */
constexpr uint32_t PAD_INFO2_MAX_CONNECT = 0;
constexpr uint32_t PAD_INFO2_NOW_CONNECT = 4;
constexpr uint32_t PAD_INFO2_SYSTEM_INFO = 8;
constexpr uint32_t PAD_INFO2_PORT_STATUS = 12;
constexpr uint32_t PAD_INFO2_PORT_SETTING = 40;
constexpr uint32_t PAD_INFO2_DEVICE_CAPABILITY = 68;
constexpr uint32_t PAD_INFO2_DEVICE_TYPE = 96;
constexpr uint32_t PAD_INFO2_SIZE = 124;

/* The pad state this runtime presents: the host's pad is bound to port 0 and
 * reported connected, exactly the state a console is in with a DualShock
 * attached. PadState is the reference's pad_info (refs/.../cellPad.h:117-142)
 * with the device model folded in. */
struct PadState {
    uint32_t max_connect = 0;      /* raw, 1..127 (:261-278) */
    uint32_t port_setting[PAD_MAX_PORT_NUM] = {};
    /* One physical pad on port 0 (this runtime's host device). */
    bool connected[PAD_MAX_PORT_NUM] = {true, false, false, false, false, false, false};

    /* refs/.../cellPad.h:129-132. */
    uint32_t get_max_connect() const {
        return max_connect < PAD_MAX_PORT_NUM ? max_connect : PAD_MAX_PORT_NUM;
    }
    /* refs/.../cellPad.h:136-139. */
    bool is_reportedly_connected(uint32_t port) const {
        return port < get_max_connect() && connected[port];
    }
    uint32_t capability(uint32_t port) const {
        return is_reportedly_connected(port) ? PAD_CAPABILITY_STANDARD : 0;
    }
    uint32_t device_type(uint32_t port) const {
        return is_reportedly_connected(port) ? PAD_DEV_TYPE_STANDARD : 0;
    }
    uint32_t port_status(uint32_t port) const {
        return is_reportedly_connected(port) ? PAD_STATUS_CONNECTED : 0;
    }

    /* cellPadInit(max_connect), refs/.../cellPad.cpp:257-289.
     * The range test is against CELL_MAX_PADS (127), not against 7; the clamp to
     * 7 happens only in get_max_connect(). Getting this wrong rejected a valid
     * cellPadInit(8). */
    int32_t init(uint32_t max) {
        if (max_connect) return PAD_ERROR_ALREADY_INITIALIZED;
        if (max == 0 || max > PAD_MAX_PADS) return PAD_ERROR_INVALID_PARAMETER;
        max_connect = max;
        for (uint32_t i = 0; i < PAD_MAX_PORT_NUM; i++) port_setting[i] = 0;  /* PRESS_OFF | SENSOR_OFF */
        return 0;
    }

    /* cellPadEnd, refs/.../cellPad.cpp:291-304. */
    int32_t end() {
        if (!max_connect) return PAD_ERROR_UNINITIALIZED;
        max_connect = 0;
        return 0;
    }

    /* cellPadGetData(port_no, data), refs/.../cellPad.cpp:695-723.
     * The order is: uninitialized, then port/data range, then the device, then
     * the reported-connection test. A disconnected pad answers
     * not_an_error(CELL_PAD_ERROR_NO_DEVICE) (:721): the same 0x80121107 value,
     * returned without being reported as a failure. Nothing is written into the
     * caller's CellPadData in that case, so the handler must not zero it either. */
    int32_t get_data(uint32_t port, bool has_data) {
        if (!max_connect) return PAD_ERROR_UNINITIALIZED;
        if (port >= PAD_MAX_PADS || !has_data) return PAD_ERROR_INVALID_PARAMETER;
        if (port >= get_max_connect()) return PAD_ERROR_NO_DEVICE;
        if (!is_reportedly_connected(port)) return PAD_ERROR_NO_DEVICE;
        return 0;
    }

    /* cellPadSetPortSetting(port_no, port_setting), refs/.../cellPad.cpp:1036-1067.
     * Only the PRESS and SENSOR bits are stored (:1054-1062); a port in
     * 0..127 but beyond 7 is a CELL_OK that stores nothing (:1050-1052, "not
     * returned in this case" for NO_DEVICE). */
    int32_t set_port_setting(uint32_t port, uint32_t setting) {
        if (!max_connect) return PAD_ERROR_UNINITIALIZED;
        if (port >= PAD_MAX_PADS) return PAD_ERROR_INVALID_PARAMETER;
        if (port >= PAD_MAX_PORT_NUM) return 0;
        if (setting & PAD_SETTING_PRESS_ON)
            port_setting[port] |= PAD_SETTING_PRESS_ON;
        else
            port_setting[port] &= ~PAD_SETTING_PRESS_ON;
        if (setting & PAD_SETTING_SENSOR_ON)
            port_setting[port] |= PAD_SETTING_SENSOR_ON;
        else
            port_setting[port] &= ~PAD_SETTING_SENSOR_ON;
        return 0;
    }

    /* cellPadSetActDirect(port_no, param), refs/.../cellPad.cpp:868-909.
     * The reserved-bits test runs only when the process SDK version is above
     * 0x1FFFFF (:882-890), and it is a SIGNED comparison, so a version above
     * 0x7FFFFFFF would skip it. sdk_ver here comes from the title's process
     * parameters (runtime/src/loader.cpp:89). */
    int32_t set_act_direct(uint32_t port, uint32_t sdk_ver, bool has_param,
                           bool reserved_clear) {
        if (!max_connect) return PAD_ERROR_UNINITIALIZED;
        if (port >= PAD_MAX_PADS || !has_param) return PAD_ERROR_INVALID_PARAMETER;
        if (static_cast<int32_t>(sdk_ver) > 0x1FFFFF && !reserved_clear)
            return PAD_ERROR_INVALID_PARAMETER;
        if (port >= get_max_connect()) return PAD_ERROR_NO_DEVICE;
        if (!is_reportedly_connected(port)) return PAD_ERROR_NO_DEVICE;
        /* A pad without the actuator capability is refused, not silently
         * accepted (:902-904). The standard set this runtime advertises DOES
         * have it, so the answer for port 0 is CELL_OK. */
        if (!(capability(port) & PAD_CAPABILITY_ACTUATOR))
            return PAD_ERROR_UNSUPPORTED_GAMEPAD;
        return 0;
    }

    /* cellPadGetInfo2(info), refs/.../cellPad.cpp:956-1003. */
    int32_t get_info2(bool has_info) {
        if (!max_connect) return PAD_ERROR_UNINITIALIZED;
        if (!has_info) return PAD_ERROR_INVALID_PARAMETER;
        return 0;
    }

    /* The length cellPadGetData reports, from the port setting
     * (refs/.../cellPad.cpp:539-565):
     *   SENSOR_ON -> CELL_PAD_LEN_CHANGE_SENSOR_ON (24)
     *   PRESS_ON  -> CELL_PAD_LEN_CHANGE_PRESS_ON  (20)
     *   neither   -> CELL_PAD_LEN_CHANGE_DEFAULT   (8)
     * and the header word is "(0x7 << 4) | min(len / 2, 15)" (:574). A constant
     * 24 handed a title sensor fields it never asked for.
     *
     * DELIBERATE DEVIATION, and the only one in this module: RPCS3 also reports
     * CELL_PAD_LEN_NO_CHANGE (0) when no button changed and SENSOR_ON is off
     * (:556-565), where this returns the sized length every call. fill()
     * resamples the host device on each call and this runtime keeps no pad state
     * cache to diff against, so reproducing the no-change case means reporting
     * nothing at all for a neutral pad -- which starves a title that reads input
     * every frame. The size is therefore always reported. */
    static uint32_t data_len(uint32_t setting) {
        if (setting & PAD_SETTING_SENSOR_ON) return PAD_LEN_CHANGE_SENSOR_ON;
        if (setting & PAD_SETTING_PRESS_ON) return PAD_LEN_CHANGE_PRESS_ON;
        return PAD_LEN_CHANGE_DEFAULT;
    }

    /* The value cellPadGetInfo2 reports for now_connect: ports that are
     * reported connected (:998-1000). */
    uint32_t now_connect() const {
        uint32_t n = 0;
        for (uint32_t i = 0; i < get_max_connect(); i++)
            if (connected[i]) n++;
        return n;
    }
};

}  // namespace sysio
}  // namespace hle

#endif
