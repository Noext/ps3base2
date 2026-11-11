/* ps3base -- cellSysutil's audio-out, video-out, system-parameter, BGM, cache,
 * OSK and progress-bar surface, tested against the reference through the SAME
 * header the imports run (runtime/src/hle/sysutil_hle.h).
 *
 * Every expectation here is a line in refs/rpcs3, named at the check. The two
 * properties that matter most:
 *   - the structures are written at the header's offsets and sizes, so a caller
 *     that reads its own CellAudioOutDeviceInfo (0x88 bytes) is not overrun and
 *     does not read a stale field as a capability;
 *   - an unimplemented path says so, and every path the console performs
 *     answers what the console answers (a stereo configuration for an AV-cable
 *     console, CELL_CANCEL for a save deletion nothing selected).
 *
 * No game data, no GPU, no network: guest memory comes from vm::init and the
 * values are read back out of it.
 */
#include "hle/sysutil_hle.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace su = hle::sysutil;

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

/* The state is a singleton, and one section's writes would otherwise be read
 * back by the next one. */
void reset_state() { su::state() = su::State{}; }

}  // namespace

int main() {
    if (!vm::init()) {
        std::fprintf(stderr, "vm::init failed\n");
        return 1;
    }
    const uint32_t buf = alloc_area(0x2000);
    CHECK(buf != 0, "guest memory for the structure writes");
    if (!buf) return 1;

    /* --- constants, one check per family of return codes ----------------- */
    /* refs/.../cellAudioOut.h:7-17 */
    CHECK(su::AUDIO_OUT_ERR_ILLEGAL_PARAMETER == static_cast<int32_t>(0x8002b242),
          "CELL_AUDIO_OUT_ERROR_ILLEGAL_PARAMETER is 0x8002b242");
    CHECK(su::AUDIO_OUT_ERR_PARAMETER_OUT_OF_RANGE == static_cast<int32_t>(0x8002b243),
          "CELL_AUDIO_OUT_ERROR_PARAMETER_OUT_OF_RANGE is 0x8002b243");
    CHECK(su::AUDIO_OUT_ERR_UNSUPPORTED_AUDIO_OUT == static_cast<int32_t>(0x8002b245),
          "CELL_AUDIO_OUT_ERROR_UNSUPPORTED_AUDIO_OUT is 0x8002b245");
    /* refs/.../cellVideoOut.h:6-17 */
    CHECK(su::VIDEO_OUT_ERR_ILLEGAL_PARAMETER == static_cast<int32_t>(0x8002b222),
          "CELL_VIDEO_OUT_ERROR_ILLEGAL_PARAMETER is 0x8002b222");
    CHECK(su::VIDEO_OUT_ERR_DEVICE_NOT_FOUND == static_cast<int32_t>(0x8002b224),
          "CELL_VIDEO_OUT_ERROR_DEVICE_NOT_FOUND is 0x8002b224");
    CHECK(su::VIDEO_OUT_ERR_UNSUPPORTED_DISPLAY_MODE == static_cast<int32_t>(0x8002b226),
          "CELL_VIDEO_OUT_ERROR_UNSUPPORTED_DISPLAY_MODE is 0x8002b226");
    CHECK(su::VIDEO_OUT_ERR_ILLEGAL_CONFIGURATION == static_cast<int32_t>(0x8002b221),
          "CELL_VIDEO_OUT_ERROR_ILLEGAL_CONFIGURATION is 0x8002b221");
    /* refs/.../cellSysutil.h:51-60, :243-258, :262-266 */
    CHECK(su::SYSUTIL_ERR_VALUE == static_cast<int32_t>(0x8002b102),
          "CELL_SYSUTIL_ERROR_VALUE is 0x8002b102");
    CHECK(su::SYSUTIL_ERR_SIZE == static_cast<int32_t>(0x8002b103),
          "CELL_SYSUTIL_ERROR_SIZE is 0x8002b103");
    CHECK(su::SYSCACHE_ERR_PARAM == static_cast<int32_t>(0x8002bc03),
          "CELL_SYSCACHE_ERROR_PARAM is 0x8002bc03");
    CHECK(su::SYSCACHE_ERR_NOTMOUNTED == static_cast<int32_t>(0x8002bc04),
          "CELL_SYSCACHE_ERROR_NOTMOUNTED is 0x8002bc04");
    CHECK(su::SYSCACHE_RET_OK_CLEARED == 0 && su::SYSCACHE_RET_OK_RELAYED == 1,
          "the syscache success values are 0 (cleared) and 1 (relayed)");
    /* refs/.../cellOskDialog.h:17 and :172 */
    CHECK(su::OSK_ERR_PARAM == static_cast<int32_t>(0x8002b504), "CELL_OSKDIALOG_ERROR_PARAM is 0x8002b504");
    CHECK(su::OSK_DEVICE_MASK_PAD == 0xff, "CELL_OSKDIALOG_DEVICE_MASK_PAD is 0xff");
    /* refs/.../cellMsgDialog.h:17-21 */
    CHECK(su::MSG_ERR_PARAM == static_cast<int32_t>(0x8002b301), "CELL_MSGDIALOG_ERROR_PARAM is 0x8002b301");
    CHECK(su::MSG_ERR_DIALOG_NOT_OPENED == static_cast<int32_t>(0x8002b302),
          "CELL_MSGDIALOG_ERROR_DIALOG_NOT_OPENED is 0x8002b302");
    /* refs/.../ErrorCodes.h:68 */
    CHECK(su::CELL_CANCEL == 1, "CELL_CANCEL is 1");

    /* --- structure sizes, from the headers' field lists ------------------ */
    /* cellAudioOut.h:123-167, cellVideoOut.h:168-218, cellSysutil.h:289-318 */
    CHECK(su::AUDIO_OUT_CONFIGURATION_SIZE == 16, "CellAudioOutConfiguration is 16 bytes");
    CHECK(su::AUDIO_OUT_SOUND_MODE_SIZE == 8, "CellAudioOutSoundMode is 8 bytes");
    CHECK(su::AUDIO_OUT_DEVICE_INFO_SIZE == 0x88,
          "CellAudioOutDeviceInfo is 0x88 bytes (8 + 16 sound modes), not 0x1C8");
    CHECK(su::AUDIO_OUT_STATE_SIZE == 0x14, "CellAudioOutState is 8 + be32 + sound mode = 0x14");
    CHECK(su::VIDEO_OUT_CONFIGURATION_SIZE == 16, "CellVideoOutConfiguration is 16 bytes");
    CHECK(su::VIDEO_OUT_OPTION_SIZE == 4, "CellVideoOutOption is 4 bytes");
    CHECK(su::VIDEO_OUT_STATE_SIZE == 16, "CellVideoOutState is 16 bytes");
    CHECK(su::VIDEO_OUT_DISPLAY_MODE_SIZE == 8, "CellVideoOutDisplayMode is 8 bytes");
    CHECK(su::VIDEO_OUT_RESOLUTION_SIZE == 4, "CellVideoOutResolution is 4 bytes");
    CHECK(su::BGM_STATUS_SIZE == 32, "CellSysutilBgmPlaybackStatus is 32 bytes");
    CHECK(su::SYSCACHE_PARAM_PATH_OFFSET == 32, "CellSysCacheParam getCachePath is at offset 32");
    CHECK(su::SYSCACHE_PARAM_SIZE == 32 + 1055 + 4, "CellSysCacheParam is 1091 bytes");

    /* --- the sound-mode table ------------------------------------------- */
    /* refs/.../cellAudioOut.cpp:159-163: with the default stereo format, the
     * primary gets exactly one mode, Linear PCM 2 Ch. 48 kHz. */
    reset_state();
    const su::ModeTable modes = su::default_modes();
    CHECK(modes.count == 1, "the default mode list has exactly one entry (stereo LPCM)");
    CHECK(modes.modes[0].type == su::AUDIO_OUT_CODING_TYPE_LPCM, "that entry is LPCM");
    CHECK(modes.modes[0].channel == 2, "that entry is 2 channels");
    CHECK(modes.modes[0].fs == su::AUDIO_OUT_FS_48KHZ, "that entry is 48 kHz");
    CHECK(modes.modes[0].layout == 0x1, "that entry is SPEAKER_LAYOUT_2CH");

    /* --- cellAudioOutGetNumberOfDevice (:472-487) ----------------------- */
    uint32_t count = 0xFFFF;
    CHECK(su::audio_out_number_of_device(su::AUDIO_OUT_PRIMARY, count) == 0 && count == 1,
          "the primary output has one device");
    CHECK(su::audio_out_number_of_device(su::AUDIO_OUT_SECONDARY, count) == 0 && count == 0,
          "the secondary output has no device");
    CHECK(su::audio_out_number_of_device(7, count) == su::AUDIO_OUT_ERR_ILLEGAL_PARAMETER,
          "any other audioOut is ILLEGAL_PARAMETER");

    /* --- cellAudioOutGetSoundAvailability (:226-253) --------------------- */
    CHECK(su::audio_out_sound_availability(0, su::AUDIO_OUT_CODING_TYPE_LPCM, su::AUDIO_OUT_FS_48KHZ) == 2,
          "LPCM at 48 kHz is available at 2 channels");
    CHECK(su::audio_out_sound_availability(0, su::AUDIO_OUT_CODING_TYPE_LPCM, 0x10) == 0,
          "LPCM at 96 kHz is NOT available: the previous body ignored fs and said 2");
    CHECK(su::audio_out_sound_availability(0, su::AUDIO_OUT_CODING_TYPE_AC3, su::AUDIO_OUT_FS_48KHZ) == 0,
          "AC3 is not available: the console has no AC3 mode in the stereo configuration");
    CHECK(su::audio_out_sound_availability(1, su::AUDIO_OUT_CODING_TYPE_LPCM, su::AUDIO_OUT_FS_48KHZ) == 0,
          "the secondary output answers 0 channels rather than an error");
    CHECK(su::audio_out_sound_availability(9, su::AUDIO_OUT_CODING_TYPE_LPCM, su::AUDIO_OUT_FS_48KHZ) == 0,
          "an unknown output answers 0 as well (the reference returns not_an_error(0))");

    /* --- cellAudioOutGetConfiguration (:437-470) ------------------------ */
    reset_state();
    su::AudioOut view;
    CHECK(su::audio_out_get_configuration(su::AUDIO_OUT_PRIMARY, view) == 0, "the primary configuration is readable");
    CHECK(view.channel == 2 && view.encoder == su::AUDIO_OUT_CODING_TYPE_LPCM,
          "the initial configuration is 2-channel LPCM");
    CHECK(su::audio_out_get_configuration(su::AUDIO_OUT_SECONDARY, view) ==
              su::AUDIO_OUT_ERR_UNSUPPORTED_AUDIO_OUT,
          "the secondary output cannot be reconfigured (UNSUPPORTED_AUDIO_OUT)");
    CHECK(su::audio_out_get_configuration(2, view) == su::AUDIO_OUT_ERR_ILLEGAL_PARAMETER,
          "an unknown audioOut is ILLEGAL_PARAMETER");

    /* --- cellAudioOutConfigure (:339-435) ------------------------------- */
    reset_state();
    su::AudioOut want;
    want.channel = 6;
    want.encoder = su::AUDIO_OUT_CODING_TYPE_LPCM;
    want.downmixer = su::AUDIO_OUT_DOWNMIXER_TYPE_A;
    CHECK(su::audio_out_configure(su::AUDIO_OUT_PRIMARY, want) == 0, "configuring the primary returns CELL_OK");
    CHECK(su::audio_out_get_configuration(su::AUDIO_OUT_PRIMARY, view) == 0, "the configuration reads back");
    CHECK(view.channel == 6, "the channel count the title set is stored");
    CHECK(view.downmixer == su::AUDIO_OUT_DOWNMIXER_TYPE_A, "a downMixer within range is stored");
    CHECK(view.sound_mode.channel == 2,
          "no 6-channel mode exists in the stereo table, so the sound mode keeps its previous value (:393-397)");
    CHECK(view.state == su::AUDIO_OUT_OUTPUT_STATE_ENABLED,
          "after the reset cycle the output reports ENABLED again (:410, :418)");

    /* An out-of-range downMixer is IGNORED and the previous valid one kept
     * (refs/.../cellAudioOut.cpp:373-381). */
    su::AudioOut bad;
    bad.channel = 6;
    bad.encoder = su::AUDIO_OUT_CODING_TYPE_LPCM;
    bad.downmixer = 3;
    CHECK(su::audio_out_configure(su::AUDIO_OUT_PRIMARY, bad) == 0,
          "an invalid downMixer is not an error, it is ignored");
    su::audio_out_get_configuration(su::AUDIO_OUT_PRIMARY, view);
    CHECK(view.downmixer == su::AUDIO_OUT_DOWNMIXER_TYPE_A,
          "the previous valid downMixer survives an invalid one");
    CHECK(su::audio_out_configure(su::AUDIO_OUT_SECONDARY, want) == su::AUDIO_OUT_ERR_UNSUPPORTED_AUDIO_OUT,
          "configure on the secondary is UNSUPPORTED_AUDIO_OUT");
    CHECK(su::audio_out_configure(4, want) == su::AUDIO_OUT_ERR_ILLEGAL_PARAMETER,
          "configure on an unknown output is ILLEGAL_PARAMETER");

    /* --- cellAudioOutGetState (:282-337) -------------------------------- */
    reset_state();
    bool secondary_past_end = false;
    CHECK(su::audio_out_get_state(su::AUDIO_OUT_PRIMARY, 0, view, secondary_past_end) == 0,
          "device 0 of the primary is readable");
    CHECK(!secondary_past_end && view.state == su::AUDIO_OUT_OUTPUT_STATE_ENABLED,
          "the primary reports an ENABLED output");
    CHECK(su::audio_out_get_state(su::AUDIO_OUT_PRIMARY, 1, view, secondary_past_end) ==
              su::AUDIO_OUT_ERR_PARAMETER_OUT_OF_RANGE,
          "device 1 of the primary is PARAMETER_OUT_OF_RANGE");
    /* The odd one: past the end of the SECONDARY is CELL_OK with a constant
     * state and layout (:302-311). A title that reads them reads what the
     * console writes, so it must not be 'fixed' into an error. */
    std::memset(vm::ptr(buf), 0, 0x40);
    CHECK(su::audio_out_get_state(su::AUDIO_OUT_SECONDARY, 0, view, secondary_past_end) == 0,
          "past the end of the secondary is CELL_OK, not an error");
    CHECK(secondary_past_end, "that case is flagged so the constant state is written");
    su::write_audio_out_secondary_past_end(buf);
    CHECK(vm::ptr(buf)[0] == 0x10, "the state byte is 0x10, as the console returns (:307)");
    CHECK(vm::read32(buf + 16) == 0xD00C1680u, "soundMode.layout is 0xD00C1680, as the console returns (:308)");
    CHECK(su::audio_out_get_state(6, 0, view, secondary_past_end) == su::AUDIO_OUT_ERR_ILLEGAL_PARAMETER,
          "an unknown output is ILLEGAL_PARAMETER");

    /* --- the audio-out state structure at the header's offsets ---------- */
    reset_state();
    std::memset(vm::ptr(buf), 0xAA, 0x40);
    su::AudioOut out0 = su::state().out[0];
    out0.state = su::AUDIO_OUT_OUTPUT_STATE_DISABLED;
    out0.encoder = su::AUDIO_OUT_CODING_TYPE_LPCM;
    out0.downmixer = su::AUDIO_OUT_DOWNMIXER_TYPE_B;
    su::write_audio_out_state(buf, out0);
    CHECK(vm::ptr(buf)[0] == 1, "CellAudioOutState.state is at offset 0");
    CHECK(vm::ptr(buf)[1] == 0, "CellAudioOutState.encoder is at offset 1");
    CHECK(vm::read32(buf + 8) == 2, "CellAudioOutState.downMixer is at offset 8");
    CHECK(vm::ptr(buf + 12)[1] == 2, "CellAudioOutState.soundMode.channel is at offset 13");
    CHECK(vm::read32(buf + 16) == 0x1, "CellAudioOutState.soundMode.layout is at offset 16");
    CHECK(static_cast<uint8_t>(vm::ptr(buf)[0x14]) == 0xAA,
          "nothing is written past the 0x14-byte structure");

    std::memset(vm::ptr(buf), 0xAA, 0x100);
    su::write_audio_out_device_info(buf, out0);
    CHECK(vm::ptr(buf)[0] == su::AUDIO_OUT_PORT_HDMI, "CellAudioOutDeviceInfo.portType is at offset 0");
    CHECK(vm::ptr(buf)[1] == 1, "availableModeCount is at offset 1 and counts the one mode");
    CHECK(vm::ptr(buf)[2] == su::AUDIO_OUT_DEVICE_STATE_AVAILABLE, "state is at offset 2 and reports AVAILABLE");
    CHECK(vm::read16(buf + 6) == 13, "latency is at offset 6 and is the reference's 13");
    CHECK(vm::ptr(buf + 8)[0] == su::AUDIO_OUT_CODING_TYPE_LPCM, "availableModes[0].type is at offset 8");
    CHECK(vm::ptr(buf + 8)[1] == 2, "availableModes[0].channel is at offset 9");
    CHECK(vm::ptr(buf + 8)[2] == su::AUDIO_OUT_FS_48KHZ, "availableModes[0].fs is at offset 10");
    CHECK(vm::read32(buf + 8 + 4) == 0x1, "availableModes[0].layout is at offset 12");
    CHECK(static_cast<uint8_t>(vm::ptr(buf)[0x88]) == 0xAA,
          "nothing is written past the 0x88-byte structure");

    /* --- cellAudioOutGetDeviceInfo (:489-536) --------------------------- */
    reset_state();
    bool zeroed = false;
    CHECK(su::audio_out_get_device_info(su::AUDIO_OUT_PRIMARY, 0, view, zeroed) == 0,
          "device 0 of the primary has a readable device info");
    CHECK(!zeroed && view.modes.count == 1, "that info carries the one stereo mode");
    CHECK(su::audio_out_get_device_info(su::AUDIO_OUT_PRIMARY, 1, view, zeroed) ==
              su::AUDIO_OUT_ERR_PARAMETER_OUT_OF_RANGE,
          "device 1 of the primary is PARAMETER_OUT_OF_RANGE");
    CHECK(su::audio_out_get_device_info(su::AUDIO_OUT_SECONDARY, 0, view, zeroed) == 0 && zeroed,
          "past the end of the secondary is CELL_OK with a zeroed structure (:505-513)");
    CHECK(su::audio_out_get_device_info(3, 0, view, zeroed) == su::AUDIO_OUT_ERR_ILLEGAL_PARAMETER,
          "an unknown output is ILLEGAL_PARAMETER");

    /* --- cellAudioOutSetCopyControl (:538-563) -------------------------- */
    reset_state();
    CHECK(su::audio_out_set_copy_control(su::AUDIO_OUT_SECONDARY, 3) ==
              su::AUDIO_OUT_ERR_ILLEGAL_PARAMETER,
          "the control range is checked BEFORE audioOut");
    CHECK(su::audio_out_set_copy_control(su::AUDIO_OUT_SECONDARY, 0) ==
              su::AUDIO_OUT_ERR_UNSUPPORTED_AUDIO_OUT,
          "the secondary output refuses a valid control");
    CHECK(su::audio_out_set_copy_control(2, 0) == su::AUDIO_OUT_ERR_ILLEGAL_PARAMETER,
          "an unknown output is ILLEGAL_PARAMETER");
    CHECK(su::audio_out_set_copy_control(su::AUDIO_OUT_PRIMARY, 2) == 0, "COPY_NEVER is accepted");
    CHECK(su::state().out[0].copy_control == 2, "the accepted value is stored");
    CHECK(su::audio_out_set_copy_control(su::AUDIO_OUT_PRIMARY, 3) ==
              su::AUDIO_OUT_ERR_ILLEGAL_PARAMETER,
          "a control above COPY_NEVER is refused");

    /* --- the resolution table (:91-123) --------------------------------- */
    uint16_t w = 0, h = 0;
    CHECK(su::resolution_info(1, w, h) && w == 1920 && h == 1080, "resolution id 1 is 1920x1080");
    CHECK(su::resolution_info(2, w, h) && w == 1280 && h == 720, "resolution id 2 is 1280x720");
    CHECK(su::resolution_info(4, w, h) && w == 720 && h == 480, "resolution id 4 is 720x480");
    CHECK(su::resolution_info(5, w, h) && w == 720 && h == 576, "resolution id 5 is 720x576");
    CHECK(su::resolution_info(0x0A, w, h) && w == 1600 && h == 1080, "resolution id 0x0a is 1600x1080");
    CHECK(su::resolution_info(0x0D, w, h) && w == 960 && h == 1080, "resolution id 0x0d is 960x1080");
    CHECK(su::resolution_info(0x81, w, h) && w == 1280 && h == 1470, "resolution id 0x81 is the 720 3D mode");
    CHECK(su::resolution_info(0x92, w, h), "resolution id 0x92 has an entry in the reference's table");
    CHECK(su::resolution_info(0xA1, w, h), "resolution id 0xa1 has an entry in the reference's table");
    CHECK(!su::resolution_info(3, w, h), "id 3 is not in the table");
    CHECK(!su::resolution_info(0x80, w, h), "id 0x80 is not in the table");
    std::memset(vm::ptr(buf), 0xAA, 8);
    su::write_video_out_resolution(buf, 1280, 720);
    CHECK(vm::read16(buf) == 1280 && vm::read16(buf + 2) == 720,
          "CellVideoOutResolution is width then height, both be u16");
    CHECK(static_cast<uint8_t>(vm::ptr(buf)[4]) == 0xAA, "nothing is written past 4 bytes");

    /* --- cellVideoOutGetState (:125-163) -------------------------------- */
    reset_state();
    su::DisplayMode mode;
    int32_t color_space = 0, output_state = 0;
    CHECK(su::video_out_get_state(su::VIDEO_OUT_PRIMARY, 0, mode, color_space, output_state) == 0,
          "the primary video-out state is readable");
    CHECK(output_state == su::VIDEO_OUT_OUTPUT_STATE_ENABLED, "the primary reports ENABLED");
    CHECK(color_space == su::VIDEO_OUT_COLOR_SPACE_RGB, "the colour space is RGB");
    CHECK(mode.resolution_id == 2, "with no configuration the reported mode is this runtime's 720p default");
    CHECK(mode.aspect == su::VIDEO_OUT_ASPECT_16_9, "the aspect is 16:9");
    CHECK(mode.refresh_rates == su::VIDEO_OUT_REFRESH_RATE_59_94HZ,
          "refreshRates is 59.94 Hz, as the reference reports (:152)");
    CHECK(su::video_out_get_state(su::VIDEO_OUT_PRIMARY, 1, mode, color_space, output_state) ==
              su::VIDEO_OUT_ERR_DEVICE_NOT_FOUND,
          "a device index past the count is DEVICE_NOT_FOUND");
    /* The secondary has zero devices, and the count is checked before the
     * switch (:133-136), so this is DEVICE_NOT_FOUND -- the DISABLED branch at
     * :155-157 is unreachable because of it. */
    CHECK(su::video_out_get_state(su::VIDEO_OUT_SECONDARY, 0, mode, color_space, output_state) ==
              su::VIDEO_OUT_ERR_DEVICE_NOT_FOUND,
          "the secondary has no device, so its state is DEVICE_NOT_FOUND (:133-136)");
    /* refs/.../cellVideoOut.cpp:133-136 collapses a negative device count into
     * DEVICE_NOT_FOUND, so that is what an unsupported videoOut produces here
     * -- the underlying UNSUPPORTED_VIDEO_OUT is a code this call never
     * returns. */
    CHECK(su::video_out_get_state(5, 0, mode, color_space, output_state) ==
              su::VIDEO_OUT_ERR_DEVICE_NOT_FOUND,
          "an unsupported videoOut collapses into DEVICE_NOT_FOUND (:133-136)");

    /* The secondary answer above wrote nothing into 'mode'; use an explicitly
     * empty one so the offsets are checked against known values. */
    std::memset(vm::ptr(buf), 0xAA, 0x20);
    su::write_video_out_state(buf, su::DisplayMode{}, su::VIDEO_OUT_COLOR_SPACE_RGB,
                              su::VIDEO_OUT_OUTPUT_STATE_DISABLED);
    CHECK(vm::ptr(buf)[0] == su::VIDEO_OUT_OUTPUT_STATE_DISABLED, "CellVideoOutState.state is at offset 0");
    CHECK(vm::ptr(buf)[1] == su::VIDEO_OUT_COLOR_SPACE_RGB, "CellVideoOutState.colorSpace is at offset 1");
    CHECK(vm::ptr(buf + 8)[0] == 0, "displayMode.resolutionId is at offset 8");
    CHECK(static_cast<uint8_t>(vm::ptr(buf)[16]) == 0xAA,
          "nothing is written past the 16-byte state structure");

    /* --- cellVideoOutConfigure (:185-236) ------------------------------- */
    reset_state();
    const uint32_t k720[4] = {2, su::VIDEO_OUT_BUFFER_COLOR_FORMAT_X8R8G8B8, su::VIDEO_OUT_ASPECT_16_9, 5120};
    CHECK(su::video_out_configure(k720) == 0, "a valid 720p configuration is accepted");
    CHECK(su::state().video_resolution == 2 && su::state().video_pitch == 5120,
          "the configuration is stored, so GetConfiguration can report it back");
    CHECK(su::video_out_resolution_availability(0, 2, su::VIDEO_OUT_ASPECT_AUTO) == 1,
          "the configured resolution is reported available");
    CHECK(su::video_out_resolution_availability(0, 1, su::VIDEO_OUT_ASPECT_AUTO) == 0,
          "an unconfigured resolution is reported unavailable");

    const uint32_t kZeroPitch[4] = {2, 0, su::VIDEO_OUT_ASPECT_16_9, 0};
    CHECK(su::video_out_configure(kZeroPitch) == su::VIDEO_OUT_ERR_PARAMETER_OUT_OF_RANGE,
          "a zero pitch is PARAMETER_OUT_OF_RANGE (:196-199)");
    const uint32_t kId92[4] = {0x92, 0, su::VIDEO_OUT_ASPECT_16_9, 5120};
    CHECK(su::video_out_configure(kId92) == su::VIDEO_OUT_ERR_ILLEGAL_CONFIGURATION,
          "resolution id 0x92 is ILLEGAL_CONFIGURATION (:201-204)");
    const uint32_t kId3[4] = {3, 0, su::VIDEO_OUT_ASPECT_16_9, 5120};
    CHECK(su::video_out_configure(kId3) == su::VIDEO_OUT_ERR_UNSUPPORTED_DISPLAY_MODE,
          "an id outside the table is UNSUPPORTED_DISPLAY_MODE (:207-214)");
    const uint32_t k3d[4] = {0x81, 0, su::VIDEO_OUT_ASPECT_16_9, 5120};
    CHECK(su::video_out_configure(k3d) == su::VIDEO_OUT_ERR_UNSUPPORTED_DISPLAY_MODE,
          "a 3D mode is UNSUPPORTED_DISPLAY_MODE with stereo output off");
    const uint32_t kBadAspect[4] = {4, 0, 9, 4 * 720};
    CHECK(su::video_out_configure(kBadAspect) == 0, "an unknown aspect is not an error");
    CHECK(su::state().video_aspect == su::VIDEO_OUT_ASPECT_16_9,
          "an unknown aspect resolves to the configured one (:229-235)");
    const uint32_t kAutoAspect[4] = {4, 0, su::VIDEO_OUT_ASPECT_AUTO, 4 * 720};
    CHECK(su::video_out_configure(kAutoAspect) == 0 && su::state().video_aspect == su::VIDEO_OUT_ASPECT_16_9,
          "AUTO resolves to the configured aspect too");

    /* --- cellVideoOutGetConfiguration (:242-274) ------------------------ */
    su::State view_state;
    bool zero = false;
    CHECK(su::video_out_get_configuration(su::VIDEO_OUT_PRIMARY, view_state, zero) == 0 && !zero,
          "the primary configuration is readable");
    CHECK(view_state.video_resolution == 4 && view_state.video_pitch == 4 * 720,
          "it reports the configuration that was just set");
    CHECK(su::video_out_get_configuration(su::VIDEO_OUT_SECONDARY, view_state, zero) == 0 && zero,
          "the secondary returns CELL_OK with a zeroed configuration (:268-270)");
    CHECK(su::video_out_get_configuration(3, view_state, zero) == su::VIDEO_OUT_ERR_UNSUPPORTED_VIDEO_OUT,
          "any other videoOut is UNSUPPORTED_VIDEO_OUT (:272)");
    std::memset(vm::ptr(buf), 0xAA, 0x20);
    su::write_video_out_configuration(buf, su::state());
    CHECK(vm::ptr(buf)[0] == 4, "CellVideoOutConfiguration.resolutionId is at offset 0");
    CHECK(vm::ptr(buf)[1] == 0, "format is at offset 1");
    CHECK(vm::ptr(buf)[2] == 2, "aspect is at offset 2");
    CHECK(vm::read32(buf + 12) == 4 * 720, "pitch is at offset 12");
    CHECK(static_cast<uint8_t>(vm::ptr(buf)[16]) == 0xAA, "nothing is written past 16 bytes");

    /* --- cellVideoOutGetResolutionAvailability (:438-490) --------------- */
    reset_state();
    CHECK(su::video_out_resolution_availability(0, 2, su::VIDEO_OUT_ASPECT_AUTO) == 1,
          "the default 720p mode is available");
    CHECK(su::video_out_resolution_availability(0, 2, su::VIDEO_OUT_ASPECT_16_9) == 1,
          "so is it for the matching aspect");
    CHECK(su::video_out_resolution_availability(0, 2, su::VIDEO_OUT_ASPECT_4_3) == 0,
          "but not for a different aspect (:447-450)");
    CHECK(su::video_out_resolution_availability(0, 1, su::VIDEO_OUT_ASPECT_AUTO) == 0,
          "1080p is not the configured mode, so it is unavailable");
    CHECK(su::video_out_resolution_availability(1, 2, su::VIDEO_OUT_ASPECT_AUTO) == 0,
          "the secondary always answers 0 (:486)");
    CHECK(su::video_out_resolution_availability(9, 2, su::VIDEO_OUT_ASPECT_AUTO) ==
              su::VIDEO_OUT_ERR_UNSUPPORTED_VIDEO_OUT,
          "an unknown videoOut is UNSUPPORTED_VIDEO_OUT (:489)");

    /* --- cellSysutilGetSystemParamInt (:515-612) ------------------------ */
    int32_t value = 0x7FFFFFFF;
    CHECK(su::system_param_int(su::PARAM_ID_LANG, value) == 0 && value == 1, "LANG reports ENGLISH_US (1)");
    CHECK(su::system_param_int(su::PARAM_ID_ENTER_BUTTON_ASSIGN, value) == 0 && value == 1,
          "ENTER_BUTTON_ASSIGN reports CROSS (1)");
    CHECK(su::system_param_int(su::PARAM_ID_DATE_FORMAT, value) == 0 && value == 1,
          "DATE_FORMAT reports DDMMYYYY (1)");
    CHECK(su::system_param_int(su::PARAM_ID_TIME_FORMAT, value) == 0 && value == 1,
          "TIME_FORMAT reports CLOCK24 (1)");
    CHECK(su::system_param_int(su::PARAM_ID_TIMEZONE, value) == 0 && value == 180,
          "TIMEZONE reports 180 (:540)");
    CHECK(su::system_param_int(su::PARAM_ID_SUMMERTIME, value) == 0 && value == 0, "SUMMERTIME reports 0");
    CHECK(su::system_param_int(su::PARAM_ID_GAME_PARENTAL_LEVEL, value) == 0 && value == 0,
          "GAME_PARENTAL_LEVEL reports OFF (0)");
    CHECK(su::system_param_int(su::PARAM_ID_GAME_PARENTAL_LEVEL0_RESTRICT, value) == 0 && value == 0,
          "GAME_PARENTAL_LEVEL0_RESTRICT reports OFF (0)");
    CHECK(su::system_param_int(su::PARAM_ID_CURRENT_USER_HAS_NP_ACCOUNT, value) == 0 && value == 0,
          "CURRENT_USER_HAS_NP_ACCOUNT reports no account while offline");
    CHECK(su::system_param_int(su::PARAM_ID_CAMERA_PLFREQ, value) == 0 && value == 0,
          "CAMERA_PLFREQ reports DISABLED (0)");
    CHECK(su::system_param_int(su::PARAM_ID_PAD_RUMBLE, value) == 0 && value == 1, "PAD_RUMBLE reports ON (1)");
    CHECK(su::system_param_int(su::PARAM_ID_KEYBOARD_TYPE, value) == 0 && value == 0,
          "KEYBOARD_TYPE reports the 101 mapping (0)");
    CHECK(su::system_param_int(su::PARAM_ID_JAPANESE_KEYBOARD_ENTRY_METHOD, value) == 0 && value == 0,
          "the Japanese entry method reports ROMAJI (0)");
    CHECK(su::system_param_int(su::PARAM_ID_CHINESE_KEYBOARD_ENTRY_METHOD, value) == 0 && value == 0,
          "the Chinese entry method reports ZHUYIN (0)");
    CHECK(su::system_param_int(su::PARAM_ID_PAD_AUTOOFF, value) == 0 && value == 0, "PAD_AUTOOFF reports OFF (0)");
    CHECK(su::system_param_int(su::PARAM_ID_MAGNETOMETER, value) == 0 && value == 0,
          "MAGNETOMETER reports OFF (0)");
    /* The license area is refused above SDK 0x23FFFF (:556-568), and RDR is a
     * 03.3000 title (docs/progress.md:406). */
    CHECK(su::system_param_int(su::PARAM_ID_LICENSE_AREA, value) == su::SYSUTIL_ERR_VALUE,
          "LICENSE_AREA is refused for a 03.3000 title, as the reference refuses it");
    CHECK(su::system_param_int(0x0999, value) == su::SYSUTIL_ERR_VALUE,
          "an unknown id is ERROR_VALUE, not a zero success");

    /* --- cellSysutilGetSystemParamString (:614-683) --------------------- */
    CHECK(su::system_param_string_check(su::PARAM_ID_NICKNAME, 0x80) == 0, "the nickname needs exactly 0x80 bytes");
    CHECK(su::system_param_string_check(su::PARAM_ID_NICKNAME, 0x81) == su::SYSUTIL_ERR_SIZE,
          "a larger buffer is ERROR_SIZE: the size must be equal, not at least (:656-659)");
    CHECK(su::system_param_string_check(su::PARAM_ID_CURRENT_USERNAME, 0x40) == 0,
          "the current username needs exactly 0x40 bytes");
    CHECK(su::system_param_string_check(su::PARAM_ID_CURRENT_USERNAME, 0x41) == su::SYSUTIL_ERR_SIZE,
          "an oversized username buffer is ERROR_SIZE");
    CHECK(su::system_param_string_check(su::PARAM_ID_X1012, 0x400) == 0, "id 0x1012 needs 0x400 bytes");
    CHECK(su::system_param_string_check(su::PARAM_ID_X1008, 4) == 0, "id 0x1008 needs 4 bytes");
    CHECK(su::system_param_string_check(0x1234, 4) == su::SYSUTIL_ERR_VALUE,
          "an unknown string id is ERROR_VALUE");
    CHECK(su::system_param_string(su::PARAM_ID_CURRENT_USERNAME, "Kratos") == "Kratos",
          "the current username comes from the account's file");
    CHECK(su::system_param_string(su::PARAM_ID_CURRENT_USERNAME, "") == "Unknown",
          "with no username file the answer is the reference's fallback (:638-645)");
    CHECK(!su::system_param_string(su::PARAM_ID_NICKNAME, "").empty(), "the nickname is never empty");

    /* --- BGM (:751-852) ------------------------------------------------- */
    reset_state();
    CHECK(su::state().bgm_player_state == 1 && su::state().bgm_enable_state == 1,
          "the BGM manager starts STOPPED and DISABLED (:757-761)");
    su::bgm_enable();
    CHECK(su::state().bgm_enable_state == 0, "enable moves enableState to ENABLE (:770)");
    CHECK(su::state().bgm_player_state == 1, "enable does not start playback: playerState stays STOP");
    std::memset(vm::ptr(buf), 0xAA, 0x40);
    su::write_bgm_status(buf);
    CHECK(vm::ptr(buf)[0] == 1, "CellSysutilBgmPlaybackStatus.playerState is at offset 0");
    CHECK(vm::ptr(buf)[1] == 0, "enableState is at offset 1");
    CHECK(static_cast<uint8_t>(vm::ptr(buf)[0x20]) == 0xAA, "nothing is written past the 32-byte structure");
    su::bgm_disable();
    CHECK(su::state().bgm_enable_state == 1, "disable moves enableState back to DISABLE (:805)");

    /* --- cellSysCacheMount / Clear (:155-262) --------------------------- */
    reset_state();
    CHECK(su::cache_clear() == su::SYSCACHE_ERR_NOTMOUNTED,
          "a clear before any mount is NOTMOUNTED, not a success");
    CHECK(su::valid_cache_id("BLES00680"), "an upper-case id is valid");
    CHECK(su::valid_cache_id("ABC-123_X"), "digits, '-' and '_' are valid");
    CHECK(!su::valid_cache_id("abc"), "lower case is not: sysutil_check_name_string rejects it (:452-509)");
    CHECK(su::valid_cache_id(""), "an empty id is explicitly allowed (:193)");
    CHECK(!su::valid_cache_id("_lead"), "an id starting with '_' is invalid");
    CHECK(!su::valid_cache_id("lower"), "lower case is invalid");
    CHECK(!su::valid_cache_id(std::string(32, 'A')), "an id of 32 characters is invalid (max 31)");
    CHECK(su::cache_dir_name("BLES00680", "PLAYER") == "BLES00680_PLAYER",
          "the directory name is the title id and the cache id joined by '_' (:204)");

    reset_state();
    std::string dir;
    CHECK(su::cache_mount("PLAYER", "BLES00680", dir) == su::SYSCACHE_RET_OK_CLEARED,
          "a first mount is RET_OK_CLEARED (0)");
    CHECK(dir == "BLES00680_PLAYER", "and names the directory after the title and the id");
    CHECK(su::cache_mount("PLAYER", "BLES00680", dir) == su::SYSCACHE_RET_OK_RELAYED,
          "mounting the same cache again is RET_OK_RELAYED (1) (:217-228)");
    CHECK(su::cache_clear() == su::SYSCACHE_RET_OK_CLEARED, "a clear after a mount succeeds");
    CHECK(su::cache_mount("bad id", "BLES00680", dir) == su::SYSCACHE_ERR_PARAM,
          "an invalid cache id is CELL_SYSCACHE_ERROR_PARAM (:193-198)");

    /* --- progress bars (:599-696) --------------------------------------- */
    CHECK(su::msg_progress_bar_count(0x1000) == 1, "progress_bar_count is bits 12-13 of the type (single)");
    CHECK(su::msg_progress_bar_count(0x2000) == 2, "two bars for 2 << 12");
    CHECK(su::msg_progress_bar_count(0x0) == 0, "no bars for a plain dialog");
    CHECK(su::msg_progress_bar_check(false, 0x1000, 0) == su::MSG_ERR_DIALOG_NOT_OPENED,
          "a progress-bar call with no dialog open is DIALOG_NOT_OPENED");
    CHECK(su::msg_progress_bar_check(true, 0x1000, 1) == su::MSG_ERR_PARAM,
          "an index past the bar count is PARAM");
    CHECK(su::msg_progress_bar_check(true, 0x1000, 0) == 0, "a valid index on an open dialog succeeds");
    CHECK(su::msg_progress_bar_set_msg_check(true, 0x1000, 0, false, 0) == su::MSG_ERR_PARAM,
          "a null message is PARAM (:603-606)");
    CHECK(su::msg_progress_bar_set_msg_check(true, 0x1000, 0, true, 64) == su::MSG_ERR_PARAM,
          "a message of 64 bytes or more is PARAM (:603-606)");
    CHECK(su::msg_progress_bar_set_msg_check(true, 0x1000, 0, true, 63) == 0,
          "a 63-byte message is accepted");
    CHECK(su::msg_progress_bar_set_msg_check(false, 0x1000, 0, true, 4) == su::MSG_ERR_DIALOG_NOT_OPENED,
          "the message is checked first, then the dialog state");

    /* --- OSK (:792-962) ------------------------------------------------- */
    CHECK(su::osk_abort_check(false) == su::MSG_ERR_DIALOG_NOT_OPENED,
          "abort with no OSK open is DIALOG_NOT_OPENED, not a success (:796-814)");
    CHECK(su::osk_abort_check(true) == 0, "abort with an open OSK succeeds");
    CHECK(su::osk_set_device_mask(0xff) == 0, "the PAD mask is accepted (:835-838)");
    CHECK(su::osk_set_device_mask(0x100) == su::OSK_ERR_PARAM, "a mask above PAD is PARAM");
    CHECK(su::osk_set_key_layout_option(1) == 0, "key-layout option 1 is accepted");
    CHECK(su::osk_set_key_layout_option(2) == 0, "key-layout option 2 is accepted");
    CHECK(su::osk_set_key_layout_option(3) == 0, "key-layout option 3 is accepted");
    CHECK(su::osk_set_key_layout_option(0) == su::OSK_ERR_PARAM, "option 0 is PARAM (:954-957)");
    CHECK(su::osk_set_key_layout_option(4) == su::OSK_ERR_PARAM, "option 4 is PARAM");

    std::printf("sysutil: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
