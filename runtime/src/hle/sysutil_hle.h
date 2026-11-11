/* ps3base -- cellSysutil's audio-out, video-out, system-parameter, BGM and
 * cache surface, kept in a header so the tests drive the same code the imports
 * run.
 *
 * Every constant, structure layout and return code below is taken from the
 * PS3 reference in refs/, never from memory (AGENTS.md). The citations name
 * the file and the line:
 *
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellAudioOut.h:7-17     errors
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellAudioOut.h:19-120    enums
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellAudioOut.h:123-167   structures
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellAudioOut.cpp         behaviour
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellVideoOut.h:6-17      errors
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellVideoOut.h:25-239    enums/structures
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellVideoOut.cpp:91-490  behaviour
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellSysutil.h:8-330      errors, ids
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellSysutil.cpp:515-852  behaviour
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellSysCache.cpp:155-262 behaviour
 *
 * What this runtime can and cannot do is spelled out per function, because the
 * distinction is the depot's rule: a function the console performs answers what
 * the console answers; whatever genuinely cannot be done here says so in the
 * log instead of returning a success nobody implemented.
 *
 * There is no audio or video device behind this runtime. A console with an AV
 * cable reports a stereo LPCM configuration, so that is what these functions
 * report -- a stereo configuration, not an error. The audio mixer already
 * accepts whatever channel count the title opens (runtime/src/hle/cellAudio.cpp
 * mixes 1, 2, 6 and 8 channels), so a stereo answer is consistent with what the
 * rest of this runtime does with the samples.
 */
#ifndef PS3BASE_HLE_SYSUTIL_H
#define PS3BASE_HLE_SYSUTIL_H

#include "runtime/memory.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace hle {
namespace sysutil {

/* ------------------------------------------------------------------ errors */

/* cellAudioOut errors, refs/.../cellAudioOut.h:7-17. */
constexpr int32_t AUDIO_OUT_ERR_NOT_IMPLEMENTED = static_cast<int32_t>(0x8002b240);
constexpr int32_t AUDIO_OUT_ERR_ILLEGAL_CONFIGURATION = static_cast<int32_t>(0x8002b241);
constexpr int32_t AUDIO_OUT_ERR_ILLEGAL_PARAMETER = static_cast<int32_t>(0x8002b242);
constexpr int32_t AUDIO_OUT_ERR_PARAMETER_OUT_OF_RANGE = static_cast<int32_t>(0x8002b243);
constexpr int32_t AUDIO_OUT_ERR_DEVICE_NOT_FOUND = static_cast<int32_t>(0x8002b244);
constexpr int32_t AUDIO_OUT_ERR_UNSUPPORTED_AUDIO_OUT = static_cast<int32_t>(0x8002b245);
constexpr int32_t AUDIO_OUT_ERR_UNSUPPORTED_SOUND_MODE = static_cast<int32_t>(0x8002b246);
constexpr int32_t AUDIO_OUT_ERR_CONDITION_BUSY = static_cast<int32_t>(0x8002b247);

/* cellVideoOut errors, refs/.../cellVideoOut.h:6-17. */
constexpr int32_t VIDEO_OUT_ERR_NOT_IMPLEMENTED = static_cast<int32_t>(0x8002b220);
constexpr int32_t VIDEO_OUT_ERR_ILLEGAL_CONFIGURATION = static_cast<int32_t>(0x8002b221);
constexpr int32_t VIDEO_OUT_ERR_ILLEGAL_PARAMETER = static_cast<int32_t>(0x8002b222);
constexpr int32_t VIDEO_OUT_ERR_PARAMETER_OUT_OF_RANGE = static_cast<int32_t>(0x8002b223);
constexpr int32_t VIDEO_OUT_ERR_DEVICE_NOT_FOUND = static_cast<int32_t>(0x8002b224);
constexpr int32_t VIDEO_OUT_ERR_UNSUPPORTED_VIDEO_OUT = static_cast<int32_t>(0x8002b225);
constexpr int32_t VIDEO_OUT_ERR_UNSUPPORTED_DISPLAY_MODE = static_cast<int32_t>(0x8002b226);
constexpr int32_t VIDEO_OUT_ERR_CONDITION_BUSY = static_cast<int32_t>(0x8002b227);
constexpr int32_t VIDEO_OUT_ERR_VALUE_IS_NOT_SET = static_cast<int32_t>(0x8002b228);

/* CELL_SYSUTIL_ERROR_VALUE and friends, refs/.../cellSysutil.h:51-60. */
constexpr int32_t SYSUTIL_ERR_TYPE = static_cast<int32_t>(0x8002b101);
constexpr int32_t SYSUTIL_ERR_VALUE = static_cast<int32_t>(0x8002b102);
constexpr int32_t SYSUTIL_ERR_SIZE = static_cast<int32_t>(0x8002b103);
constexpr int32_t SYSUTIL_ERR_NUM = static_cast<int32_t>(0x8002b104);
constexpr int32_t SYSUTIL_ERR_BUSY = static_cast<int32_t>(0x8002b105);

/* BGM errors, refs/.../cellSysutil.h:262-266. */
constexpr int32_t BGM_ERR_PARAM = static_cast<int32_t>(0x8002b101);
constexpr int32_t BGM_EX_ERR_PARAM = static_cast<int32_t>(0x8002d301);

/* cellSysCache, refs/.../cellSysutil.h:243-258. */
constexpr int32_t SYSCACHE_RET_OK_CLEARED = 0;
constexpr int32_t SYSCACHE_RET_OK_RELAYED = 1;
constexpr int32_t SYSCACHE_ERR_PARAM = static_cast<int32_t>(0x8002bc03);
constexpr int32_t SYSCACHE_ERR_NOTMOUNTED = static_cast<int32_t>(0x8002bc04);
constexpr uint32_t SYSCACHE_ID_SIZE = 32;
constexpr uint32_t SYSCACHE_PATH_MAX = 1055;

/* OSK, refs/.../cellOskDialog.h:17, :34-37 and :172. */
constexpr int32_t OSK_ERR_PARAM = static_cast<int32_t>(0x8002b504);
constexpr int32_t OSK_INPUT_RESULT_OK = 0;
constexpr int32_t OSK_INPUT_RESULT_CANCELED = 1;
constexpr int32_t OSK_INPUT_RESULT_ABORT = 2;
constexpr int32_t OSK_INPUT_RESULT_NO_INPUT_TEXT = 3;
constexpr uint32_t OSK_DEVICE_MASK_PAD = 0x000000ff;

/* msgdialog, refs/.../cellMsgDialog.h:13-21 and :72-90. */
constexpr int32_t MSG_ERR_PARAM = static_cast<int32_t>(0x8002b301);
constexpr int32_t MSG_ERR_DIALOG_NOT_OPENED = static_cast<int32_t>(0x8002b302);
constexpr uint32_t MSG_PROGRESSBAR_STRING_SIZE = 64;
constexpr uint32_t MSG_STRING_SIZE = 512;

/* --------------------------------------------------------- structure sizes
 *
 * Field order and sizes are the headers above, not a recollection. The numbers
 * matter: a handler that writes 0x3A0 bytes into a 0x1C8-byte structure
 * destroys the caller's frame and the fault lands somewhere else entirely
 * (AGENTS.md, the CellVideoOutDeviceInfo session).
 *
 * CellAudioOutConfiguration  (cellAudioOut.h:123-129)  u8,u8,u8[10],be u32 = 16
 * CellAudioOutSoundMode      (cellAudioOut.h:131-140)  u8,u8,u8,u8,be u32   =  8
 * CellAudioOutDeviceInfo     (cellAudioOut.h:142-150)  8 + 16*8            = 0x88
 * CellAudioOutState          (cellAudioOut.h:152-159)  8 + be32 + mode      = 0x14
 * CellVideoOutConfiguration  (cellVideoOut.h:206-213)  u8[3]+u8[9],be u32   = 16
 * CellVideoOutOption         (cellVideoOut.h:215-218)  be u32               =  4
 * CellVideoOutState          (cellVideoOut.h:198-204)  8 + DisplayMode      = 16
 * CellVideoOutDisplayMode    (cellVideoOut.h:168-176)  6 + be u16           =  8
 * CellVideoOutResolution     (cellVideoOut.h:178-182)  be u16*2             =  4
 * CellSysutilBgmPlaybackStatus (cellSysutil.h:289-296) 1+1+16+1+13          = 32
 * CellSysCacheParam          (cellSysutil.h:313-318)   32 + 1055 + 4        = 1091
 */
constexpr uint32_t AUDIO_OUT_CONFIGURATION_SIZE = 16;
constexpr uint32_t AUDIO_OUT_SOUND_MODE_SIZE = 8;
constexpr uint32_t AUDIO_OUT_DEVICE_INFO_SIZE = 0x88;
constexpr uint32_t AUDIO_OUT_STATE_SIZE = 0x14;
constexpr uint32_t VIDEO_OUT_CONFIGURATION_SIZE = 16;
constexpr uint32_t VIDEO_OUT_OPTION_SIZE = 4;
constexpr uint32_t VIDEO_OUT_STATE_SIZE = 16;
constexpr uint32_t VIDEO_OUT_DISPLAY_MODE_SIZE = 8;
constexpr uint32_t VIDEO_OUT_RESOLUTION_SIZE = 4;
constexpr uint32_t BGM_STATUS_SIZE = 32;
constexpr uint32_t SYSCACHE_PARAM_PATH_OFFSET = SYSCACHE_ID_SIZE;
constexpr uint32_t SYSCACHE_PARAM_SIZE = SYSCACHE_ID_SIZE + SYSCACHE_PATH_MAX + 4;

static_assert(VIDEO_OUT_DISPLAY_MODE_SIZE == 8, "CellVideoOutDisplayMode is 8 bytes");
static_assert(AUDIO_OUT_SOUND_MODE_SIZE == 8, "CellAudioOutSoundMode is 8 bytes");
static_assert(AUDIO_OUT_DEVICE_INFO_SIZE == 8 + 16 * AUDIO_OUT_SOUND_MODE_SIZE,
              "CellAudioOutDeviceInfo is 8 header bytes plus 16 sound modes");
static_assert(AUDIO_OUT_STATE_SIZE == 8 + 4 + AUDIO_OUT_SOUND_MODE_SIZE,
              "CellAudioOutState is 8 + be32 + CellAudioOutSoundMode");
static_assert(BGM_STATUS_SIZE == 1 + 1 + 16 + 1 + 13,
              "CellSysutilBgmPlaybackStatus is playerState, enableState, contentId[16], fadeRatio, reserved[13]");
static_assert(SYSCACHE_PARAM_PATH_OFFSET == 32, "CellSysCacheParam::getCachePath follows cacheId[32]");

/* --------------------------------------------------------------- enumerations
 *
 * Values from the headers, not an enum of ours: a title compares them against
 * constants compiled into it.
 */
constexpr uint32_t AUDIO_OUT_PRIMARY = 0;     /* cellAudioOut.h:21 */
constexpr uint32_t AUDIO_OUT_SECONDARY = 1;   /* cellAudioOut.h:22 */

constexpr uint32_t AUDIO_OUT_DOWNMIXER_NONE = 0;     /* cellAudioOut.h:27-29 */
constexpr uint32_t AUDIO_OUT_DOWNMIXER_TYPE_A = 1;
constexpr uint32_t AUDIO_OUT_DOWNMIXER_TYPE_B = 2;

constexpr uint8_t AUDIO_OUT_PORT_HDMI = 0;                 /* cellAudioOut.h:41 */
constexpr uint8_t AUDIO_OUT_DEVICE_STATE_AVAILABLE = 1;    /* cellAudioOut.h:52 */
constexpr uint8_t AUDIO_OUT_OUTPUT_STATE_ENABLED = 0;      /* cellAudioOut.h:57 */
constexpr uint8_t AUDIO_OUT_OUTPUT_STATE_DISABLED = 1;

constexpr uint8_t AUDIO_OUT_CODING_TYPE_LPCM = 0;          /* cellAudioOut.h:64 */
constexpr uint8_t AUDIO_OUT_CODING_TYPE_AC3 = 1;           /* cellAudioOut.h:65 */
constexpr uint8_t AUDIO_OUT_CODING_TYPE_DTS = 6;           /* cellAudioOut.h:70 */
constexpr uint8_t AUDIO_OUT_CHNUM_2 = 2;                   /* cellAudioOut.h:81 */
constexpr uint8_t AUDIO_OUT_FS_48KHZ = 0x04;               /* cellAudioOut.h:91 */
constexpr uint32_t AUDIO_OUT_SPEAKER_LAYOUT_2CH = 0x00000001;  /* cellAudioOut.h:101 */
constexpr uint32_t AUDIO_OUT_COPY_CONTROL_COPY_NEVER = 2;      /* cellAudioOut.h:119 */

constexpr uint32_t VIDEO_OUT_PRIMARY = 0;      /* cellVideoOut.h:21 */
constexpr uint32_t VIDEO_OUT_SECONDARY = 1;

constexpr uint8_t VIDEO_OUT_PORT_HDMI = 0x01;                 /* cellVideoOut.h:78 */
constexpr uint8_t VIDEO_OUT_COLOR_SPACE_RGB = 0x01;           /* cellVideoOut.h:117 */
constexpr uint32_t VIDEO_OUT_RESOLUTION_UNDEFINED = 0;        /* cellVideoOut.h:27 */
constexpr uint32_t VIDEO_OUT_RESOLUTION_1080 = 1;
constexpr uint32_t VIDEO_OUT_RESOLUTION_720 = 2;
constexpr uint32_t VIDEO_OUT_RESOLUTION_480 = 4;
constexpr uint32_t VIDEO_OUT_RESOLUTION_576 = 5;
constexpr uint32_t VIDEO_OUT_DISPLAY_CONVERSION_NONE = 0x00;  /* cellVideoOut.h:159 */
constexpr uint32_t VIDEO_OUT_SCAN_MODE_INTERLACE = 0;         /* cellVideoOut.h:55 */
constexpr uint32_t VIDEO_OUT_SCAN_MODE_PROGRESSIVE = 1;
constexpr uint32_t VIDEO_OUT_ASPECT_AUTO = 0;                 /* cellVideoOut.h:90 */
constexpr uint32_t VIDEO_OUT_ASPECT_4_3 = 1;
constexpr uint32_t VIDEO_OUT_ASPECT_16_9 = 2;
constexpr uint32_t VIDEO_OUT_BUFFER_COLOR_FORMAT_X8R8G8B8 = 0;/* cellVideoOut.h:97 */
constexpr uint16_t VIDEO_OUT_REFRESH_RATE_59_94HZ = 0x0001;   /* cellVideoOut.h:69 */
constexpr uint8_t VIDEO_OUT_OUTPUT_STATE_ENABLED = 0;         /* cellVideoOut.h:104 */
constexpr uint8_t VIDEO_OUT_OUTPUT_STATE_DISABLED = 1;
constexpr uint8_t VIDEO_OUT_DEVICE_STATE_AVAILABLE = 1;       /* cellVideoOut.h:112 */
constexpr uint8_t VIDEO_OUT_RGB_OUTPUT_RANGE_FULL = 1;        /* cellVideoOut.h:238 */
constexpr uint32_t VIDEO_OUT_3D_FRAME_PACKING_BASE = 0x81;    /* cellVideoOut.h:36 */

/* ------------------------------------------------------------- sound modes
 *
 * refs/.../cellAudioOut.cpp:35-180 builds the list. With its default
 * audio_format::stereo (Emu/system_config_types.h:79-86) only two entries
 * exist:
 *   - the primary output always gets Linear PCM 2 Ch. 48 kHz
 *     (cellAudioOut.cpp:159-160). The 5.1/7.1/AC3/DTS entries are added only
 *     for the surround modes, and SOUND_FORMAT alone does not add them.
 *   - the secondary output only ever supports the same stereo mode
 *     (cellAudioOut.cpp:162-163).
 * The pre-selected mode is that first supported one (cellAudioOut.cpp:72-80),
 * so the primary starts with encoder LPCM, 2 channels, 48 kHz, layout 2CH.
 *
 * This is also what a console with an AV cable reports, which is the target
 * behaviour here (docs/board.md: no PS3 audio peripheral in this runtime).
 */
struct SoundMode {
    uint8_t type = AUDIO_OUT_CODING_TYPE_LPCM;       /* offset 0 */
    uint8_t channel = AUDIO_OUT_CHNUM_2;             /* offset 1 */
    uint8_t fs = AUDIO_OUT_FS_48KHZ;                 /* offset 2 */
    uint8_t reserved = 0;                            /* offset 3 */
    uint32_t layout = AUDIO_OUT_SPEAKER_LAYOUT_2CH;  /* offset 4 */
};

struct ModeTable {
    SoundMode modes[16];
    uint32_t count = 0;
};

/* cellAudioOut.cpp:159-163. Both outputs carry the same single stereo mode;
 * keeping this a function is what makes "the secondary has no extra formats"
 * a testable statement instead of an assumption. */
inline ModeTable default_modes() {
    ModeTable t;
    t.modes[0] = SoundMode{};
    t.count = 1;
    return t;
}

/* --------------------------------------------------------------- audio out */

struct AudioOut {
    uint8_t state = AUDIO_OUT_OUTPUT_STATE_ENABLED;   /* cellAudioOut.h:209 */
    uint8_t channel = AUDIO_OUT_CHNUM_2;              /* cellAudioOut.h:210 */
    uint8_t encoder = AUDIO_OUT_CODING_TYPE_LPCM;     /* cellAudioOut.h:211 */
    uint32_t downmixer = AUDIO_OUT_DOWNMIXER_NONE;    /* cellAudioOut.h:212 */
    uint32_t copy_control = 0;                        /* cellAudioOut.h:213 */
    SoundMode sound_mode{};
    ModeTable modes = default_modes();
};

struct State {
    AudioOut out[2];

    /* Video out. 'video_set' is RPCS3's rsx::avconf.state: until the title
     * configures a mode, the reported one is this runtime's default.
     * runtime/src/gpu/presenter.cpp presents the guest surface at 1280x720
     * (:609, :649), so that is the mode the title is told about. */
    bool video_set = false;
    uint32_t video_resolution = VIDEO_OUT_RESOLUTION_720;
    uint32_t video_format = VIDEO_OUT_BUFFER_COLOR_FORMAT_X8R8G8B8;
    uint32_t video_aspect = VIDEO_OUT_ASPECT_16_9;
    uint32_t video_pitch = 4 * 1280;

    /* BGM. refs/.../cellSysutil.cpp:751-762: a manager whose status starts
     * STOPPED and DISABLED, and whose enable/disable calls move only
     * enableState (cellSysutil.cpp:764-773, :799-812). */
    uint8_t bgm_enable_state = 1;   /* CELL_SYSUTIL_BGMPLAYBACK_STATUS_DISABLE */
    uint8_t bgm_player_state = 1;   /* CELL_SYSUTIL_BGMPLAYBACK_STATUS_STOP */

    /* Syscache. cellSysCacheMount/Clear keep one mounted id
     * (refs/.../cellSysCache.cpp:36-52, syscache_info::cache_id). */
    bool cache_mounted = false;
    std::string cache_id;
};

inline State& state() {
    static State s;
    return s;
}

/* ------------------------------------------------------ audio-out writes
 *
 * Offsets are the header's field order. The values go out big-endian, which
 * vm::write* already does.
 */
inline void write_sound_mode(uint32_t ea, const SoundMode& m) {
    if (!ea || !vm::range_mapped(ea, AUDIO_OUT_SOUND_MODE_SIZE)) return;
    vm::write8(ea + 0, m.type);
    vm::write8(ea + 1, m.channel);
    vm::write8(ea + 2, m.fs);
    vm::write8(ea + 3, 0);
    vm::write32(ea + 4, m.layout);
}

inline void write_audio_out_configuration(uint32_t ea, const AudioOut& out) {
    if (!ea || !vm::range_mapped(ea, AUDIO_OUT_CONFIGURATION_SIZE)) return;
    std::memset(vm::ptr(ea), 0, AUDIO_OUT_CONFIGURATION_SIZE);
    vm::write8(ea + 0, out.channel);
    vm::write8(ea + 1, out.encoder);
    vm::write32(ea + 12, out.downmixer);
}

inline void write_audio_out_state(uint32_t ea, const AudioOut& out) {
    if (!ea || !vm::range_mapped(ea, AUDIO_OUT_STATE_SIZE)) return;
    std::memset(vm::ptr(ea), 0, AUDIO_OUT_STATE_SIZE);
    vm::write8(ea + 0, out.state);
    vm::write8(ea + 1, out.encoder);
    vm::write32(ea + 8, out.downmixer);
    write_sound_mode(ea + 12, out.sound_mode);
}

/* refs/.../cellAudioOut.cpp:489-536: portType HDMI, the mode count, AVAILABLE,
 * latency 13 (the value RPCS3 reports), then the modes at offset 8. */
inline void write_audio_out_device_info(uint32_t ea, const AudioOut& out) {
    if (!ea || !vm::range_mapped(ea, AUDIO_OUT_DEVICE_INFO_SIZE)) return;
    std::memset(vm::ptr(ea), 0, AUDIO_OUT_DEVICE_INFO_SIZE);
    vm::write8(ea + 0, AUDIO_OUT_PORT_HDMI);
    vm::write8(ea + 1, static_cast<uint8_t>(out.modes.count));
    vm::write8(ea + 2, AUDIO_OUT_DEVICE_STATE_AVAILABLE);
    vm::write16(ea + 6, 13);
    for (uint32_t i = 0; i < out.modes.count && i < 16; i++) {
        write_sound_mode(ea + 8 + i * AUDIO_OUT_SOUND_MODE_SIZE, out.modes.modes[i]);
    }
}

/* The uninitialised-looking answer firmware gives for a device index past the
 * end of the SECONDARY output: refs/.../cellAudioOut.cpp:302-311 returns
 * CELL_OK with state 0x10 and layout 0xD00C1680 rather than an error. It is
 * odd, but it is what the console does and therefore the specification. */
inline void write_audio_out_secondary_past_end(uint32_t ea) {
    if (!ea || !vm::range_mapped(ea, AUDIO_OUT_STATE_SIZE)) return;
    std::memset(vm::ptr(ea), 0, AUDIO_OUT_STATE_SIZE);
    vm::write8(ea + 0, 0x10);
    vm::write32(ea + 16, 0xD00C1680);   /* soundMode.layout */
}

/* ---------------------------------------------- audio-out return-value logic */

/* cellAudioOutGetNumberOfDevice, refs/.../cellAudioOut.cpp:472-487:
 * primary 1, secondary 0, anything else ILLEGAL_PARAMETER. */
inline int32_t audio_out_number_of_device(uint32_t audio_out, uint32_t& count) {
    switch (audio_out) {
    case AUDIO_OUT_PRIMARY: count = 1; return 0;
    case AUDIO_OUT_SECONDARY: count = 0; return 0;
    default: return AUDIO_OUT_ERR_ILLEGAL_PARAMETER;
    }
}

/* cellAudioOutGetSoundAvailability, refs/.../cellAudioOut.cpp:226-253: a
 * non-primary output answers 0 (a success carrying zero channels), otherwise
 * the largest channel count among the modes matching (type, fs).
 *
 * The previous body answered 2 for every type == 0 and ignored fs, so a title
 * asking for 192 kHz or for AC3 was told two channels were available and then
 * opened a port its own mixer would not produce. */
inline uint32_t audio_out_sound_availability(uint32_t audio_out, uint32_t type, uint32_t fs) {
    if (audio_out != AUDIO_OUT_PRIMARY) return 0;
    const AudioOut& out = state().out[AUDIO_OUT_PRIMARY];
    uint32_t available = 0;
    for (uint32_t i = 0; i < out.modes.count; i++) {
        const SoundMode& m = out.modes.modes[i];
        if (m.type == type && static_cast<uint32_t>(m.fs) == fs && m.channel > available) {
            available = m.channel;
        }
    }
    return available;
}

/* cellAudioOutConfigure, refs/.../cellAudioOut.cpp:339-435.
 *
 * The set configuration does not have to exist in the mode list (:366-368); a
 * downMixer above TYPE_B is ignored and the previous value kept (:373-381); when
 * a mode matches, the selected sound mode follows it, otherwise the previous
 * one is kept (:384-397). On a change the output is cycled DISABLED -> ENABLED
 * (:403-430); that cycle is observable through cellAudioOutGetState, which is
 * what the reference's reset_audio does as well.
 *
 * There is no device to reopen here: cellAudio's mixer reads whatever channel
 * count the title opened on its own ports (runtime/src/hle/cellAudio.cpp,
 * audio_thread -> mix_block, which handles 1, 2, 6 and 8 channels), so no
 * sample path depends on this value. The stored configuration IS the console's
 * answer.
 */
inline int32_t audio_out_configure(uint32_t audio_out, const AudioOut& requested) {
    if (audio_out == AUDIO_OUT_SECONDARY) return AUDIO_OUT_ERR_UNSUPPORTED_AUDIO_OUT;  /* :352-353 */
    if (audio_out != AUDIO_OUT_PRIMARY) return AUDIO_OUT_ERR_ILLEGAL_PARAMETER;         /* :354-355 */

    AudioOut& out = state().out[AUDIO_OUT_PRIMARY];
    const bool changed = out.channel != requested.channel || out.encoder != requested.encoder ||
                         out.downmixer != requested.downmixer;
    if (changed) {
        out.channel = requested.channel;
        out.encoder = requested.encoder;
        if (requested.downmixer <= AUDIO_OUT_DOWNMIXER_TYPE_B) out.downmixer = requested.downmixer;  /* :373-381 */
        for (uint32_t i = 0; i < out.modes.count; i++) {
            const SoundMode& m = out.modes.modes[i];
            if (m.type == out.encoder && m.channel == out.channel) {
                out.sound_mode = m;
                break;
            }
        }
        out.state = AUDIO_OUT_OUTPUT_STATE_DISABLED;   /* :410 */
        out.state = AUDIO_OUT_OUTPUT_STATE_ENABLED;    /* :418 */
    }
    return 0;                                          /* :434 */
}

/* cellAudioOutGetConfiguration, refs/.../cellAudioOut.cpp:437-470: the active
 * configuration, with the same primary/secondary/other split. */
inline int32_t audio_out_get_configuration(uint32_t audio_out, AudioOut& out) {
    if (audio_out == AUDIO_OUT_SECONDARY) return AUDIO_OUT_ERR_UNSUPPORTED_AUDIO_OUT;  /* :450-451 */
    if (audio_out != AUDIO_OUT_PRIMARY) return AUDIO_OUT_ERR_ILLEGAL_PARAMETER;         /* :452-453 */
    out = state().out[AUDIO_OUT_PRIMARY];
    return 0;
}

/* cellAudioOutGetState, refs/.../cellAudioOut.cpp:282-337. */
inline int32_t audio_out_get_state(uint32_t audio_out, uint32_t device_index, AudioOut& out,
                                   bool& secondary_past_end) {
    secondary_past_end = false;
    uint32_t count = 0;
    const int32_t rc = audio_out_number_of_device(audio_out, count);
    if (rc < 0) return rc;                                  /* :291-296 */
    if (device_index >= count) {
        if (audio_out == AUDIO_OUT_SECONDARY) {              /* :300-311 */
            secondary_past_end = true;
            return 0;
        }
        return AUDIO_OUT_ERR_PARAMETER_OUT_OF_RANGE;         /* :313 */
    }
    out = state().out[audio_out];
    return 0;
}

/* cellAudioOutGetDeviceInfo, refs/.../cellAudioOut.cpp:489-536. Past the end of
 * the secondary output the console zeroes the structure and returns CELL_OK
 * (:505-513), unlike GetState. */
inline int32_t audio_out_get_device_info(uint32_t audio_out, uint32_t device_index, AudioOut& out,
                                         bool& zeroed_past_end) {
    zeroed_past_end = false;
    uint32_t count = 0;
    const int32_t rc = audio_out_number_of_device(audio_out, count);  /* :498-503 */
    if (rc < 0) return rc;
    if (device_index >= count) {
        if (audio_out == AUDIO_OUT_SECONDARY) {              /* :505-513 */
            zeroed_past_end = true;
            return 0;
        }
        return AUDIO_OUT_ERR_PARAMETER_OUT_OF_RANGE;         /* :514 */
    }
    out = state().out[audio_out];
    return 0;
}

/* cellAudioOutSetCopyControl, refs/.../cellAudioOut.cpp:538-563: the control
 * range is checked BEFORE audioOut, then primary-only, then the value is
 * stored. */
inline int32_t audio_out_set_copy_control(uint32_t audio_out, uint32_t control) {
    if (control > AUDIO_OUT_COPY_CONTROL_COPY_NEVER) return AUDIO_OUT_ERR_ILLEGAL_PARAMETER;  /* :542-545 */
    if (audio_out == AUDIO_OUT_SECONDARY) return AUDIO_OUT_ERR_UNSUPPORTED_AUDIO_OUT;         /* :551-552 */
    if (audio_out != AUDIO_OUT_PRIMARY) return AUDIO_OUT_ERR_ILLEGAL_PARAMETER;               /* :553-554 */
    state().out[AUDIO_OUT_PRIMARY].copy_control = control;                                    /* :560 */
    return 0;
}

/* --------------------------------------------------------------- video out */

struct DisplayMode {
    uint8_t resolution_id = 0;   /* offset 0, cellVideoOut.h:170 */
    uint8_t scan_mode = 0;       /* offset 1 */
    uint8_t conversion = 0;      /* offset 2 */
    uint8_t aspect = 0;          /* offset 3 */
    uint16_t refresh_rates = 0;  /* offset 6, be u16 */
};

/* cellVideoOutGetNumberOfDevice, refs/.../cellVideoOut.cpp:425-436. */
inline int32_t video_out_number_of_device(uint32_t video_out, uint32_t& count) {
    switch (video_out) {
    case VIDEO_OUT_PRIMARY: count = 1; return 0;
    case VIDEO_OUT_SECONDARY: count = 0; return 0;
    default: return VIDEO_OUT_ERR_UNSUPPORTED_VIDEO_OUT;
    }
}

/* _IntGetResolutionInfo, refs/.../cellVideoOut.cpp:91-123, entry for entry --
 * including the ids whose enumerants are unnamed there (0x64, 0x82, 0x83...).
 * A missing id used to fall through to DEVICE_NOT_FOUND, which is not a code
 * cellVideoOutGetResolution returns. */
inline bool resolution_info(uint32_t id, uint16_t& w, uint16_t& h) {
    switch (id) {
    case 1:    w = 0x780; h = 0x438; return true;   /* 1920x1080 */
    case 2:    w = 0x500; h = 0x2D0; return true;   /* 1280x720  */
    case 4:    w = 0x2D0; h = 0x1E0; return true;   /* 720x480   */
    case 5:    w = 0x2D0; h = 0x240; return true;   /* 720x576   */
    case 0x0A: w = 0x640; h = 0x438; return true;   /* 1600x1080 */
    case 0x0B: w = 0x5A0; h = 0x438; return true;   /* 1440x1080 */
    case 0x0C: w = 0x500; h = 0x438; return true;   /* 1280x1080 */
    case 0x0D: w = 0x3C0; h = 0x438; return true;   /* 960x1080  */
    case 0x64: w = 0x550; h = 0x300; return true;
    case 0x81: w = 0x500; h = 0x5BE; return true;   /* 720 3D frame packing */
    case 0x82: w = 0x780; h = 0x438; return true;
    case 0x83: w = 0x780; h = 0x89D; return true;
    case 0x8B: w = 0x280; h = 0x5BE; return true;   /* 640x720 3D */
    case 0x8A: w = 0x320; h = 0x5BE; return true;   /* 800x720 3D */
    case 0x89: w = 0x3C0; h = 0x5BE; return true;   /* 960x720 3D */
    case 0x88: w = 0x400; h = 0x5BE; return true;   /* 1024x720 3D */
    case 0x91: w = 0x500; h = 0x5BE; return true;   /* 720 dualview */
    case 0x92: w = 0x780; h = 0x438; return true;
    case 0x9B: w = 0x280; h = 0x5BE; return true;   /* 640x720 dualview */
    case 0x9A: w = 0x320; h = 0x5BE; return true;   /* 800x720 dualview */
    case 0x99: w = 0x3C0; h = 0x5BE; return true;   /* 960x720 dualview */
    case 0x98: w = 0x400; h = 0x5BE; return true;   /* 1024x720 dualview */
    case 0xA1: w = 0x780; h = 0x438; return true;
    default: return false;
    }
}

inline void write_video_out_resolution(uint32_t ea, uint16_t w, uint16_t h) {
    if (!ea || !vm::range_mapped(ea, VIDEO_OUT_RESOLUTION_SIZE)) return;
    vm::write16(ea + 0, w);
    vm::write16(ea + 2, h);
}

inline void write_display_mode(uint32_t ea, const DisplayMode& m) {
    if (!ea || !vm::range_mapped(ea, VIDEO_OUT_DISPLAY_MODE_SIZE)) return;
    vm::write8(ea + 0, m.resolution_id);
    vm::write8(ea + 1, m.scan_mode);
    vm::write8(ea + 2, m.conversion);
    vm::write8(ea + 3, m.aspect);
    vm::write8(ea + 4, 0);      /* reserved[2] */
    vm::write8(ea + 5, 0);
    vm::write16(ea + 6, m.refresh_rates);
}

inline void write_video_out_state(uint32_t ea, const DisplayMode& mode, int32_t color_space,
                                  int32_t output_state) {
    if (!ea || !vm::range_mapped(ea, VIDEO_OUT_STATE_SIZE)) return;
    std::memset(vm::ptr(ea), 0, VIDEO_OUT_STATE_SIZE);
    vm::write8(ea + 0, static_cast<uint8_t>(output_state));
    vm::write8(ea + 1, static_cast<uint8_t>(color_space));
    write_display_mode(ea + 8, mode);
}

/* cellVideoOutGetState, refs/.../cellVideoOut.cpp:125-163: an index past the
 * device count is DEVICE_NOT_FOUND, the primary reports the active mode, the
 * secondary reports DISABLED with a zeroed mode (:155-157), anything else is
 * UNSUPPORTED_VIDEO_OUT (:160). The previous body ignored both videoOut and
 * deviceIndex, so a secondary query was answered as if it were the primary. */
inline int32_t video_out_get_state(uint32_t video_out, uint32_t device_index, DisplayMode& mode,
                                   int32_t& color_space, int32_t& output_state) {
    uint32_t count = 0;
    const int32_t rc = video_out_number_of_device(video_out, count);
    if (rc < 0) return rc;
    if (device_index >= count) return VIDEO_OUT_ERR_DEVICE_NOT_FOUND;   /* :133-136 */
    if (video_out == VIDEO_OUT_SECONDARY) {
        mode = DisplayMode{};
        color_space = 0;
        output_state = VIDEO_OUT_OUTPUT_STATE_DISABLED;
        return 0;
    }
    const State& s = state();
    output_state = VIDEO_OUT_OUTPUT_STATE_ENABLED;
    color_space = VIDEO_OUT_COLOR_SPACE_RGB;
    mode.resolution_id = static_cast<uint8_t>(s.video_resolution);      /* :145-149 */
    mode.scan_mode = static_cast<uint8_t>(VIDEO_OUT_SCAN_MODE_PROGRESSIVE);
    mode.conversion = static_cast<uint8_t>(VIDEO_OUT_DISPLAY_CONVERSION_NONE);
    mode.aspect = static_cast<uint8_t>(s.video_aspect);
    mode.refresh_rates = VIDEO_OUT_REFRESH_RATE_59_94HZ;               /* :149 */
    return 0;
}

/* cellVideoOutConfigure, refs/.../cellVideoOut.cpp:185-236.
 *
 * The validation order is the reference's: a zero pitch, then the two ids the
 * console refuses as ILLEGAL_CONFIGURATION (0x92, 0xa1), then the resolution
 * table, which also rejects the 3D modes when stereo output is off -- this
 * runtime has no stereo output. An AUTO or unknown aspect is resolved to the
 * configured one (:229-235); with no display to ask, this runtime's aspect is
 * 16:9, the same default its video-out state reports.
 *
 * There is no mode-set to perform: the presenter scales whatever surface the
 * guest produces to the swapchain (runtime/src/gpu/presenter.cpp:609, :649,
 * 1280x720), so the stored configuration is the whole of what this call can
 * truthfully do, and it is exactly what the caller reads back through
 * cellVideoOutGetConfiguration.
 */
inline int32_t video_out_configure(const uint32_t* words) {
    const uint32_t resolution_id = words[0];
    const uint32_t format = words[1];
    const uint32_t aspect = words[2];
    const uint32_t pitch = words[3];
    if (pitch == 0) return VIDEO_OUT_ERR_PARAMETER_OUT_OF_RANGE;                                    /* :196-199 */
    if (resolution_id == 0x92 || resolution_id == 0xA1) return VIDEO_OUT_ERR_ILLEGAL_CONFIGURATION;  /* :201-204 */
    uint16_t w = 0, h = 0;
    if (!resolution_info(resolution_id, w, h)) return VIDEO_OUT_ERR_UNSUPPORTED_DISPLAY_MODE;        /* :207-214 */
    if (resolution_id >= VIDEO_OUT_3D_FRAME_PACKING_BASE) return VIDEO_OUT_ERR_UNSUPPORTED_DISPLAY_MODE;

    State& s = state();
    s.video_set = true;
    s.video_resolution = resolution_id;
    s.video_format = format;
    s.video_pitch = pitch;
    s.video_aspect = (aspect == VIDEO_OUT_ASPECT_4_3 || aspect == VIDEO_OUT_ASPECT_16_9)
                         ? aspect
                         : static_cast<uint32_t>(VIDEO_OUT_ASPECT_16_9);
    return 0;                                                                                       /* :235 */
}

inline void write_video_out_configuration(uint32_t ea, const State& s) {
    if (!ea || !vm::range_mapped(ea, VIDEO_OUT_CONFIGURATION_SIZE)) return;
    std::memset(vm::ptr(ea), 0, VIDEO_OUT_CONFIGURATION_SIZE);
    vm::write8(ea + 0, static_cast<uint8_t>(s.video_resolution));
    vm::write8(ea + 1, static_cast<uint8_t>(s.video_format));
    vm::write8(ea + 2, static_cast<uint8_t>(s.video_aspect));
    vm::write32(ea + 12, s.video_pitch);      /* pitch at offset 12, cellVideoOut.h:212 */
}

/* cellVideoOutGetConfiguration, refs/.../cellVideoOut.cpp:242-274: the primary
 * reports the active or default configuration; the secondary returns CELL_OK
 * with a zeroed one (:268-270); anything else is UNSUPPORTED_VIDEO_OUT (:272).
 * 'zeroed' tells the caller to write an all-zero structure. */
inline int32_t video_out_get_configuration(uint32_t video_out, State& out_state, bool& zeroed) {
    zeroed = false;
    switch (video_out) {
    case VIDEO_OUT_PRIMARY:
        out_state = state();
        return 0;
    case VIDEO_OUT_SECONDARY:
        zeroed = true;
        return 0;
    default:
        return VIDEO_OUT_ERR_UNSUPPORTED_VIDEO_OUT;
    }
}

/* cellVideoOutGetResolutionAvailability, refs/.../cellVideoOut.cpp:438-490.
 * The result is a boolean, not an error: the primary answers 1 only when the
 * aspect matches (or is AUTO, :447-450) and the id is the active one
 * (:452-455); the secondary always answers 0 (:486); anything else is
 * UNSUPPORTED_VIDEO_OUT (:489). The previous body returned 1 for 1080 and 720
 * regardless of aspect and of what was configured. */
inline int32_t video_out_resolution_availability(uint32_t video_out, uint32_t resolution_id, uint32_t aspect) {
    if (video_out == VIDEO_OUT_SECONDARY) return 0;
    if (video_out != VIDEO_OUT_PRIMARY) return VIDEO_OUT_ERR_UNSUPPORTED_VIDEO_OUT;
    if (aspect != VIDEO_OUT_ASPECT_AUTO && aspect != state().video_aspect) return 0;
    return resolution_id == state().video_resolution ? 1 : 0;
}

/* ------------------------------------------------------ system parameters
 *
 * refs/.../cellSysutil.cpp:515-612 (integers) and :614-683 (strings). An
 * unknown id is CELL_SYSUTIL_ERROR_VALUE, not a zero success: the previous body
 * logged "unknown param" and returned 0 with the caller's variable untouched,
 * which told the title its answer had been produced.
 *
 * The values are RPCS3's defaults for an offline console, which are also what a
 * console with no account and no network answers:
 */
constexpr uint32_t PARAM_ID_LANG = 0x0111;
constexpr uint32_t PARAM_ID_ENTER_BUTTON_ASSIGN = 0x0112;
constexpr uint32_t PARAM_ID_DATE_FORMAT = 0x0114;
constexpr uint32_t PARAM_ID_TIME_FORMAT = 0x0115;
constexpr uint32_t PARAM_ID_TIMEZONE = 0x0116;
constexpr uint32_t PARAM_ID_SUMMERTIME = 0x0117;
constexpr uint32_t PARAM_ID_GAME_PARENTAL_LEVEL = 0x0121;
constexpr uint32_t PARAM_ID_LICENSE_AREA = 0x0122;
constexpr uint32_t PARAM_ID_GAME_PARENTAL_LEVEL0_RESTRICT = 0x0123;
constexpr uint32_t PARAM_ID_CURRENT_USER_HAS_NP_ACCOUNT = 0x0141;
constexpr uint32_t PARAM_ID_CAMERA_PLFREQ = 0x0151;
constexpr uint32_t PARAM_ID_PAD_RUMBLE = 0x0152;
constexpr uint32_t PARAM_ID_KEYBOARD_TYPE = 0x0153;
constexpr uint32_t PARAM_ID_JAPANESE_KEYBOARD_ENTRY_METHOD = 0x0154;
constexpr uint32_t PARAM_ID_CHINESE_KEYBOARD_ENTRY_METHOD = 0x0155;
constexpr uint32_t PARAM_ID_PAD_AUTOOFF = 0x0156;
constexpr uint32_t PARAM_ID_MAGNETOMETER = 0x0157;

constexpr uint32_t PARAM_ID_NICKNAME = 0x0113;
constexpr uint32_t PARAM_ID_CURRENT_USERNAME = 0x0131;
constexpr uint32_t PARAM_ID_X1008 = 0x1008;
constexpr uint32_t PARAM_ID_X1011 = 0x1011;
constexpr uint32_t PARAM_ID_X1012 = 0x1012;
constexpr uint32_t PARAM_ID_X1024 = 0x1024;

constexpr uint32_t NICKNAME_SIZE = 0x80;          /* refs/.../cellSysutil.h:245 */
constexpr uint32_t CURRENT_USERNAME_SIZE = 0x40;  /* refs/.../cellSysutil.h:246 */

/* The SDK the title reports decides two parameters: the license area is refused
 * above 0x23FFFF (refs/.../cellSysutil.cpp:556-568) and the prefix rules differ
 * above 0x36FFFF. RDR declares PS3_SYSTEM_VER 03.3000 (docs/progress.md:406),
 * so it is above both. Kept as one named constant so the assumption is visible
 * instead of buried in a comparison. */
constexpr uint32_t SDK_VERSION = 0x330000;

inline int32_t system_param_int(uint32_t id, int32_t& value) {
    switch (id) {
    case PARAM_ID_LANG: value = 1; return 0;                          /* ENGLISH_US, system_config.h:327 */
    case PARAM_ID_ENTER_BUTTON_ASSIGN: value = 1; return 0;           /* CROSS, system_config.h:329 */
    case PARAM_ID_DATE_FORMAT: value = 1; return 0;                   /* DDMMYYYY, system_config.h:330 */
    case PARAM_ID_TIME_FORMAT: value = 1; return 0;                   /* CLOCK24, system_config.h:331 */
    case PARAM_ID_TIMEZONE: value = 180; return 0;                    /* :540 */
    case PARAM_ID_SUMMERTIME: value = 0; return 0;                    /* :545 */
    case PARAM_ID_GAME_PARENTAL_LEVEL: value = 0; return 0;           /* OFF, :550 */
    case PARAM_ID_LICENSE_AREA:
        /* refs/.../cellSysutil.cpp:556-568 refuses for sdk_ver > 0x23FFFF. */
        if (SDK_VERSION > 0x23FFFFu) return SYSUTIL_ERR_VALUE;
        value = 2;                                                    /* SCEE, the BLES area */
        return 0;
    case PARAM_ID_GAME_PARENTAL_LEVEL0_RESTRICT: value = 0; return 0; /* :570-572 */
    case PARAM_ID_CURRENT_USER_HAS_NP_ACCOUNT:
        /* Offline: no PSN account is signed in, which is what a console with no
         * connection reports (:574-577). */
        value = 0;
        return 0;
    case PARAM_ID_CAMERA_PLFREQ: value = 0; return 0;                 /* DISABLED, :579-581 */
    case PARAM_ID_PAD_RUMBLE: value = 1; return 0;                    /* ON, :583-585 */
    case PARAM_ID_KEYBOARD_TYPE: value = 0; return 0;                 /* CELL_KB_MAPPING_101 */
    case PARAM_ID_JAPANESE_KEYBOARD_ENTRY_METHOD: value = 0; return 0;/* ROMAJI, :591 */
    case PARAM_ID_CHINESE_KEYBOARD_ENTRY_METHOD: value = 0; return 0; /* ZHUYIN, :596 */
    case PARAM_ID_PAD_AUTOOFF: value = 0; return 0;                   /* OFF, :603 */
    case PARAM_ID_MAGNETOMETER: value = 0; return 0;                  /* OFF, :607 */
    default: return SYSUTIL_ERR_VALUE;                                /* :609-610 */
    }
}

/* The buffer size each string id requires. refs/.../cellSysutil.cpp:626-654. */
inline uint32_t system_param_string_size(uint32_t id) {
    switch (id) {
    case PARAM_ID_NICKNAME: return NICKNAME_SIZE;
    case PARAM_ID_CURRENT_USERNAME: return CURRENT_USERNAME_SIZE;
    case PARAM_ID_X1011:
    case PARAM_ID_X1012: return 0x400;
    case PARAM_ID_X1024: return 0x100;
    case PARAM_ID_X1008: return 0x4;
    default: return 0;
    }
}

/* refs/.../cellSysutil.cpp:655-682: the caller's size must EQUAL the id's size,
 * not merely be large enough, and an unknown id is ERROR_VALUE. */
inline int32_t system_param_string_check(uint32_t id, uint32_t bufsize) {
    const uint32_t need = system_param_string_size(id);
    if (!need) return SYSUTIL_ERR_VALUE;
    if (bufsize != need) return SYSUTIL_ERR_SIZE;
    return 0;
}

/* The string each id carries. RPCS3 reads the nickname from its config and the
 * current username from /dev_hdd0/home/<id>/localusername, falling back to
 * "Unknown" when that file does not exist (:626-651). */
inline std::string system_param_string(uint32_t id, const std::string& username_file_contents) {
    switch (id) {
    case PARAM_ID_NICKNAME: return std::string("PS3BASE");
    case PARAM_ID_CURRENT_USERNAME:
        return username_file_contents.empty() ? std::string("Unknown") : username_file_contents;
    default: return std::string("Unknown");   /* the x1008/x1011/x1012/x1024 family */
    }
}

/* ---------------------------------------------------------------------- BGM */

/* cellSysutilGetBgmPlaybackStatus, refs/.../cellSysutil.cpp:838-852: a null
 * pointer is BGMPLAYBACK_ERROR_PARAM, otherwise the manager's status is copied.
 * The status starts STOP+DISABLE (:757-761) and enable/disable move only
 * enableState (:764-773, :799-812), so there is a real state to report and it
 * reflects the calls the title actually made rather than a fabricated one. */
inline void write_bgm_status(uint32_t ea) {
    if (!ea || !vm::range_mapped(ea, BGM_STATUS_SIZE)) return;
    std::memset(vm::ptr(ea), 0, BGM_STATUS_SIZE);
    vm::write8(ea + 0, state().bgm_player_state);
    vm::write8(ea + 1, state().bgm_enable_state);
}

inline void bgm_enable() { state().bgm_enable_state = 0; }    /* ENABLE, :770 */
inline void bgm_disable() { state().bgm_enable_state = 1; }   /* DISABLE, :805 */

/* ------------------------------------------------------------------ syscache
 *
 * refs/.../cellSysCache.cpp:178-262. The guest-visible path is "/dev_hdd1"
 * (:210) -- not a path inside the cache -- and the directory name is the title
 * id and the parameter joined by '_' (:204). Mounting the same cache again is
 * RET_OK_RELAYED, a fresh one is RET_OK_CLEARED (:217-228, :262).
 *
 * The console remounts /dev_hdd1 onto that directory. This runtime's VFS has
 * one fixed mapping for /dev_hdd1 (runtime/src/vfs.cpp:101-110) and no mount
 * call to redirect it, so the directory is created with the right name inside
 * the shared /dev_hdd1 tree and the handler logs exactly that. The guest sees a
 * valid, writable path either way, which is what its own code consumes.
 */
inline bool valid_cache_id(const std::string& id) {
    /* sysutil_check_name_string(id, 1, 32), refs/.../cellSysutil.cpp:452-509:
     * length 1..31, A-Z 0-9 '-' '_', and it may not start with '_'. An empty id
     * is explicitly allowed by cellSysCacheMount (:193). */
    if (id.empty()) return true;
    if (id.size() > SYSCACHE_ID_SIZE - 1) return false;
    if (id[0] == '_') return false;
    for (char c : id) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}

inline std::string cache_dir_name(const std::string& title_id, const std::string& cache_id) {
    return title_id + "_" + cache_id;   /* refs/.../cellSysCache.cpp:204 */
}

/* Returns the cellSysCache code and, through 'dir', the directory name to
 * create under /dev_hdd1/caches. */
inline int32_t cache_mount(const std::string& cache_id, const std::string& title_id, std::string& dir) {
    if (!valid_cache_id(cache_id)) return SYSCACHE_ERR_PARAM;   /* :193-198 */
    const std::string name = cache_dir_name(title_id, cache_id);
    State& s = state();
    s.cache_mounted = true;
    if (!cache_id.empty() && s.cache_id == name) {              /* :217-228 */
        dir = name;
        return SYSCACHE_RET_OK_RELAYED;
    }
    s.cache_id = name;
    dir = name;
    return SYSCACHE_RET_OK_CLEARED;                             /* :262 */
}

inline int32_t cache_clear() {
    /* :155-176: a clear before any mount is NOTMOUNTED, not a success. */
    if (!state().cache_mounted) return SYSCACHE_ERR_NOTMOUNTED;
    return SYSCACHE_RET_OK_CLEARED;
}

/* -------------------------------------------------------------- msgdialog
 *
 * cellMsgDialogProgressBar*, refs/.../cellMsgDialog.cpp:599-696 plus the
 * overlay's own checks (refs/.../Overlays/overlay_message_dialog.cpp:551-597):
 * no open dialog is DIALOG_NOT_OPENED, an index past the dialog's bar count is
 * PARAM. progress_bar_count is bits 12-13 of the dialog type
 * (refs/.../cellMsgDialog.h:82). The previous bodies returned CELL_OK for every
 * call, including one made with no dialog open at all.
 */
inline uint32_t msg_progress_bar_count(uint32_t type) { return (type >> 12) & 3u; }

inline int32_t msg_progress_bar_check(bool open, uint32_t dialog_type, uint32_t index) {
    if (!open) return MSG_ERR_DIALOG_NOT_OPENED;              /* :618-623 */
    if (index >= msg_progress_bar_count(dialog_type)) return MSG_ERR_PARAM;  /* :655-658 */
    return 0;
}

inline int32_t msg_progress_bar_set_msg_check(bool open, uint32_t dialog_type, uint32_t index,
                                              bool has_message, size_t message_len) {
    /* :599-607 checks the message BEFORE the dialog state. */
    if (!has_message || message_len >= MSG_PROGRESSBAR_STRING_SIZE) return MSG_ERR_PARAM;
    return msg_progress_bar_check(open, dialog_type, index);
}

/* ------------------------------------------------------------------- OSK
 *
 * refs/.../cellOskDialog.cpp:792-828 (Abort), :830-858 (SetDeviceMask),
 * :950-962 (SetKeyLayoutOption). Abort with no dialog open is
 * CELL_MSGDIALOG_ERROR_DIALOG_NOT_OPENED, not a success; the device mask has
 * one valid value (PAD, 0xff) and anything above it is PARAM; the key-layout
 * option is 1..3 and 0 or anything larger is PARAM.
 */
constexpr int32_t OSK_ERR_DIALOG_NOT_OPENED = MSG_ERR_DIALOG_NOT_OPENED;

inline int32_t osk_abort_check(bool open) {
    return open ? 0 : OSK_ERR_DIALOG_NOT_OPENED;   /* :796-814 */
}

inline int32_t osk_set_device_mask(uint32_t mask) {
    if (mask > OSK_DEVICE_MASK_PAD) return OSK_ERR_PARAM;   /* :835-838 */
    return 0;
}

inline int32_t osk_set_key_layout_option(uint32_t option) {
    if (option == 0 || option > 3) return OSK_ERR_PARAM;    /* :954-957 */
    return 0;
}

/* ---------------------------------------------------------------- save data
 *
 * CELL_CANCEL is 1 (refs/.../ErrorCodes.h:68), and it is what
 * cellSaveDataDelete2 returns when the save-data selection UI yields no
 * selection: select_and_delete initialises 'selected = -1' and returns
 * CELL_CANCEL from 'if (selected < 0)'
 * (refs/.../cellSaveData.cpp:316-340, :2527-2532). This runtime has no
 * save-data selection UI, so that is the answer -- an explicit "nothing was
 * selected", never a success for a deletion that did not happen.
 */
constexpr int32_t CELL_CANCEL = 1;

}  // namespace sysutil

/* cellOskDialog's own state, exposed so cellSysutil's cellOskDialogAbort can
 * ask whether a dialog is open and close it without keeping a second copy of
 * that state (implemented in runtime/src/hle/cellOskDialog.cpp). */
namespace osk {
bool is_open();
void abort();
}

}  // namespace hle

#endif
