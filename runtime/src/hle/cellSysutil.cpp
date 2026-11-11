#include "common.h"
#include "sysutil_hle.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>

#include "avconf_hle.h"
#include "runtime/cbwatch.h"
#include "runtime/msgdialog.h"
#include "runtime/sysutil_events.h"
#include "runtime/vfs.h"

namespace hle {

namespace su = sysutil;

namespace {

struct Callback {
    uint32_t func = 0;
    uint32_t userdata = 0;
};

std::mutex g_lock;
Callback g_callbacks[4];
std::deque<std::function<void(ppu::Context&)>> g_pending;

void post(std::function<void(ppu::Context&)> fn) {
    std::lock_guard lk(g_lock);
    g_pending.push_back(std::move(fn));
}

/* The guest pointer a structure write needs is checked before the write, not
 * inside it. RPCS3 faults on an unmapped pointer; this runtime cannot let a
 * title's bad pointer take the host down, so it reports the same code an
 * illegal argument gets and names the address in the log. */
bool mapped(uint32_t ea, uint32_t size) {
    if (ea && vm::range_mapped(ea, size)) return true;
    LOG_WARN("cellSysutil", "guest pointer 0x%08x is not mapped for %u bytes", ea, size);
    return false;
}

}  // namespace

/* Exposed so other modules can raise sysutil events (cellNetCtl's dialogs do). */
uint32_t callback_function(int slot) {
    std::lock_guard lk(g_lock);
    return (slot >= 0 && slot < 4) ? g_callbacks[slot].func : 0;
}

uint32_t callback_arg(int slot) {
    std::lock_guard lk(g_lock);
    return (slot >= 0 && slot < 4) ? g_callbacks[slot].userdata : 0;
}

void sysutil_post(std::function<void(ppu::Context&)> fn) {
    post(std::move(fn));
}

/* --- cellSysutilRegisterCallback / UnregisterCallback --------------------
 * refs/.../cellSysutil.cpp:719-746: slot >= 4 is CELL_SYSUTIL_ERROR_VALUE
 * (0x8002b102), otherwise the callback pair is stored or cleared and CELL_OK is
 * returned. The previous body returned TYPE (0x8002b101) for a bad slot, which
 * is a code this function does not return.
 */
HLE_FUNC(cellSysutil, cellSysutilRegisterCallback) {
    uint32_t slot = a32(ctx, 0);
    if (slot > 3) return ret(ctx, su::SYSUTIL_ERR_VALUE);   /* :723-726 */
    std::lock_guard lk(g_lock);
    g_callbacks[slot] = {a32(ctx, 1), a32(ctx, 2)};
    cbwatch::note_register("sysutil", a32(ctx, 1), a32(ctx, 2));
    static std::atomic<int> seen{0};
    if (seen.fetch_add(1) < 20) {
        const uint32_t opd = a32(ctx, 1);
        uint32_t code = 0, toc = 0;
        if (opd && vm::range_mapped(opd, 8)) {
            code = vm::read32(opd);
            toc = vm::read32(opd + 4);
        }
        LOG_INFO("sysutil", "RegisterCallback slot %u opd 0x%08x -> code 0x%08x toc 0x%08x arg 0x%08x",
                 slot, opd, code, toc, a32(ctx, 2));
    }
    ret(ctx, 0);                                            /* :732 */
}

HLE_FUNC(cellSysutil, cellSysutilUnregisterCallback) {
    uint32_t slot = a32(ctx, 0);
    if (slot > 3) return ret(ctx, su::SYSUTIL_ERR_VALUE);   /* :739-742 */
    std::lock_guard lk(g_lock);
    g_callbacks[slot] = {};
    ret(ctx, 0);                                            /* :745 */
}

/* cellSysutilCheckCallback: drain the queue the system posted into, calling
 * each guest callback with its own arguments. refs/.../cellSysutil.cpp:685-717.
 * The queue and the delivery are the same ones cellMsgDialog and cellOskDialog
 * use (runtime/src/hle/msgdialog.cpp, cellOskDialog.cpp). */
HLE_FUNC(cellSysutil, cellSysutilCheckCallback) {
    for (;;) {
        std::function<void(ppu::Context&)> fn;
        {
            std::lock_guard lk(g_lock);
            if (g_pending.empty()) break;
            fn = std::move(g_pending.front());
            g_pending.pop_front();
        }
        fn(ctx);
    }
    ret(ctx, 0);
}

/* --- cellSysutilGetSystemParamInt ---------------------------------------
 * refs/.../cellSysutil.cpp:515-612. A null value pointer is ERROR_VALUE, an
 * unknown id is ERROR_VALUE (not a silent zero), and a known id writes its
 * answer. Every value is the one RPCS3 reports for the equivalent setting, and
 * every one is also what a console with no network and no signed-in account
 * answers.
 */
HLE_FUNC(cellSysutil, cellSysutilGetSystemParamInt) {
    const uint32_t id = a32(ctx, 0), out = a32(ctx, 1);
    if (!out) return ret(ctx, su::SYSUTIL_ERR_VALUE);       /* :519-522 */
    int32_t value = 0;
    const int32_t rc = su::system_param_int(id, value);
    if (rc < 0) {
        LOG_WARN("sysutil", "cellSysutilGetSystemParamInt: unknown id 0x%x -> ERROR_VALUE", id);
        return ret(ctx, rc);
    }
    vm::write32(out, static_cast<uint32_t>(value));
    ret(ctx, 0);
}

/* --- cellSysutilGetSystemParamString ------------------------------------
 * refs/.../cellSysutil.cpp:614-683. The caller's bufsize must EQUAL the id's
 * size (not be at least it), an unknown id is ERROR_VALUE, and the string is
 * copied NUL-terminated. The previous body answered "Kratos" for id 0x131 and
 * an empty string for everything else, both regardless of the size argument.
 */
HLE_FUNC(cellSysutil, cellSysutilGetSystemParamString) {
    const uint32_t id = a32(ctx, 0), buf = a32(ctx, 1), size = a32(ctx, 2);
    if (!buf) return ret(ctx, su::SYSUTIL_ERR_VALUE);       /* :618-621 */
    const int32_t rc = su::system_param_string_check(id, size);
    if (rc < 0) {
        LOG_WARN("sysutil", "cellSysutilGetSystemParamString: id 0x%x size %u -> 0x%x", id, size,
                 static_cast<unsigned>(rc));
        return ret(ctx, rc);
    }
    /* The current username lives in the account's file, exactly as RPCS3 reads
     * it (:635-648). An absent file leaves RPCS3's own "Unknown" fallback. */
    std::string username;
    if (id == su::PARAM_ID_CURRENT_USERNAME) {
        const std::string path = vfs::host_path("/dev_hdd0/home/00000001/localusername");
        std::ifstream f(path, std::ios::binary);
        if (f) std::getline(f, username);
    }
    const std::string value = su::system_param_string(id, username);
    put_str(buf, value, size);
    ret(ctx, 0);                                            /* :682 */
}

/* --- cellVideoOutGetState ------------------------------------------------
 * refs/.../cellVideoOut.cpp:125-163. A null state is ILLEGAL_PARAMETER, a
 * deviceIndex past the count is DEVICE_NOT_FOUND, the secondary reports
 * DISABLED with a zeroed display mode, and any other videoOut is
 * UNSUPPORTED_VIDEO_OUT.
 *
 * The previous body ignored videoOut and deviceIndex entirely and returned a
 * hardcoded 720p mode with a fabricated refreshRates; a title asking for the
 * secondary output got the primary's answer.
 */
HLE_FUNC(cellSysutil, cellVideoOutGetState) {
    const uint32_t video_out = a32(ctx, 0), device_index = a32(ctx, 1), out = a32(ctx, 2);
    if (!out) return ret(ctx, su::VIDEO_OUT_ERR_ILLEGAL_PARAMETER);   /* :129-132 */
    if (!mapped(out, su::VIDEO_OUT_STATE_SIZE)) return ret(ctx, su::VIDEO_OUT_ERR_ILLEGAL_PARAMETER);

    su::DisplayMode mode;
    int32_t color_space = 0, output_state = 0;
    const int32_t rc = su::video_out_get_state(video_out, device_index, mode, color_space, output_state);
    if (rc < 0) return ret(ctx, rc);

    su::write_video_out_state(out, mode, color_space, output_state);
    ret(ctx, 0);                                                      /* :162 */
}

/* --- cellVideoOutGetResolution -------------------------------------------
 * refs/.../cellVideoOut.cpp:165-183: a null pointer is ILLEGAL_PARAMETER, an id
 * outside the table is ILLEGAL_PARAMETER, and the entry is the table's.
 *
 * The previous body had four entries and answered UNSUPPORTED_DISPLAY_MODE
 * (0x8002b226) for anything else -- a code this function never returns -- so
 * a title asking for 1600x1080 or 960x1080 was told its display request was
 * unsupported rather than the resolution it asked about.
 */
HLE_FUNC(cellSysutil, cellVideoOutGetResolution) {
    const uint32_t id = a32(ctx, 0), out = a32(ctx, 1);
    if (!out) return ret(ctx, su::VIDEO_OUT_ERR_ILLEGAL_PARAMETER);   /* :170-173 */
    uint16_t w = 0, h = 0;
    if (!su::resolution_info(id, w, h)) {
        LOG_WARN("sysutil", "cellVideoOutGetResolution: unknown resolution id 0x%x", id);
        return ret(ctx, su::VIDEO_OUT_ERR_ILLEGAL_PARAMETER);           /* :178 */
    }
    if (!mapped(out, su::VIDEO_OUT_RESOLUTION_SIZE)) return ret(ctx, su::VIDEO_OUT_ERR_ILLEGAL_PARAMETER);
    su::write_video_out_resolution(out, w, h);
    ret(ctx, 0);                                                      /* :181 */
}

/* --- cellVideoOutGetResolutionAvailability ------------------------------
 * refs/.../cellVideoOut.cpp:438-490. The result is a BOOLEAN (1/0), not an
 * error, and it depends on the aspect and on the active mode; only a videoOut
 * that is neither primary nor secondary is an error. The previous body ignored
 * the aspect and the configured mode.
 */
HLE_FUNC(cellSysutil, cellVideoOutGetResolutionAvailability) {
    const uint32_t video_out = a32(ctx, 0);
    const uint32_t id = a32(ctx, 1);
    const uint32_t aspect = a32(ctx, 2);
    ret(ctx, su::video_out_resolution_availability(video_out, id, aspect));
}

/* --- cellVideoOutConfigure ----------------------------------------------
 * refs/.../cellVideoOut.cpp:185-236. A null config is ILLEGAL_PARAMETER, a zero
 * pitch is PARAMETER_OUT_OF_RANGE, ids 0x92/0xa1 are ILLEGAL_CONFIGURATION, an
 * unknown or 3D-without-stereo id is UNSUPPORTED_DISPLAY_MODE.
 *
 * There is no mode-set to perform here: the presenter blits whatever surface
 * the guest produces onto the swapchain (runtime/src/gpu/presenter.cpp:609,
 * :649), so the stored configuration is the whole of what this call can
 * truthfully do -- and it is what the title reads back through
 * cellVideoOutGetConfiguration, so the two agree.
 */
HLE_FUNC(cellSysutil, cellVideoOutConfigure) {
    const uint32_t cfg = a32(ctx, 1);
    if (!cfg) return ret(ctx, su::VIDEO_OUT_ERR_ILLEGAL_PARAMETER);   /* :189-192 */
    if (!mapped(cfg, su::VIDEO_OUT_CONFIGURATION_SIZE)) return ret(ctx, su::VIDEO_OUT_ERR_ILLEGAL_PARAMETER);
    const uint32_t words[4] = {vm::ptr(cfg)[0], vm::ptr(cfg)[1], vm::ptr(cfg)[2], vm::read32(cfg + 12)};
    const int32_t rc = su::video_out_configure(words);
    if (rc < 0) {
        LOG_WARN("sysutil", "cellVideoOutConfigure: res 0x%x format %u aspect %u pitch %u -> 0x%x",
                 words[0], words[1], words[2], words[3], static_cast<unsigned>(rc));
    } else {
        LOG_INFO("sysutil", "cellVideoOutConfigure: res 0x%x format %u aspect %u pitch %u", words[0],
                 words[1], words[2], words[3]);
    }
    ret(ctx, rc);                                                      /* :235 */
}

/* --- cellVideoOutGetConfiguration ---------------------------------------
 * refs/.../cellVideoOut.cpp:242-274: a null config is ILLEGAL_PARAMETER, the
 * option is zeroed when present, the secondary returns CELL_OK with a zeroed
 * configuration, and any other videoOut is UNSUPPORTED_VIDEO_OUT.
 */
HLE_FUNC(cellSysutil, cellVideoOutGetConfiguration) {
    const uint32_t video_out = a32(ctx, 0), cfg = a32(ctx, 1), option = a32(ctx, 2);
    if (!cfg) return ret(ctx, su::VIDEO_OUT_ERR_ILLEGAL_PARAMETER);   /* :246-249 */
    if (!mapped(cfg, su::VIDEO_OUT_CONFIGURATION_SIZE)) return ret(ctx, su::VIDEO_OUT_ERR_ILLEGAL_PARAMETER);
    if (option && vm::range_mapped(option, su::VIDEO_OUT_OPTION_SIZE)) {
        vm::write32(option, 0);                                       /* :251 */
    }
    su::State view;
    bool zeroed = false;
    const int32_t rc = su::video_out_get_configuration(video_out, view, zeroed);
    if (rc < 0) {
        LOG_WARN("sysutil", "cellVideoOutGetConfiguration: videoOut %u -> 0x%x", video_out,
                 static_cast<unsigned>(rc));
        return ret(ctx, rc);
    }
    if (zeroed) {
        std::memset(vm::ptr(cfg), 0, su::VIDEO_OUT_CONFIGURATION_SIZE);
    } else {
        su::write_video_out_configuration(cfg, view);
    }
    LOG_INFO("sysutil", "cellVideoOutGetConfiguration(%u): res 0x%x format %u aspect %u pitch %u",
             video_out, view.video_resolution, view.video_format, view.video_aspect, view.video_pitch);
    ret(ctx, 0);
}

/* --- cellAudioOutGetSoundAvailability -----------------------------------
 * refs/.../cellAudioOut.cpp:226-253. A non-primary output answers 0 (a success
 * carrying zero channels); otherwise the answer is the largest channel count
 * among the modes matching (type, fs).
 *
 * The previous body returned 2 for every type == 0 and ignored fs, so a title
 * asking for AC3 or for 192 kHz was told two channels were available.
 */
HLE_FUNC(cellSysutil, cellAudioOutGetSoundAvailability) {
    const uint32_t audio_out = a32(ctx, 0), type = a32(ctx, 1), fs = a32(ctx, 2);
    const uint32_t available = su::audio_out_sound_availability(audio_out, type, fs);
    LOG_INFO("sysutil", "cellAudioOutGetSoundAvailability(%u, type %u, fs 0x%x) -> %u", audio_out, type, fs,
             available);
    ret(ctx, static_cast<int64_t>(available));               /* not_an_error(available), :252 */
}

/* --- cellAudioOutGetNumberOfDevice --------------------------------------
 * refs/.../cellAudioOut.cpp:472-487: primary 1, secondary 0, anything else
 * ILLEGAL_PARAMETER. A console with an AV cable has one audio device, so 1 is
 * the console's answer and not a placeholder.
 */
HLE_FUNC(cellSysutil, cellAudioOutGetNumberOfDevice) {
    const uint32_t audio_out = a32(ctx, 0);
    uint32_t count = 0;
    const int32_t rc = su::audio_out_number_of_device(audio_out, count);
    if (rc < 0) return ret(ctx, rc);
    ret(ctx, static_cast<int64_t>(count));
}

/* --- cellAudioOutGetState ------------------------------------------------
 * refs/.../cellAudioOut.cpp:282-337. A null state is ILLEGAL_PARAMETER; an
 * unsupported audioOut is ILLEGAL_PARAMETER; a deviceIndex past the end is
 * PARAMETER_OUT_OF_RANGE for the primary, and for the SECONDARY it is CELL_OK
 * with the odd constant state 0x10 and layout 0xD00C1680 the console actually
 * returns (:302-311).
 *
 * There is no audio device behind this runtime, but that does not make the call
 * an error: a console with an AV cable reports a stereo LPCM output ENABLED,
 * and this writes exactly that from the stored configuration, so a
 * cellAudioOutConfigure the title made is reflected here.
 */
HLE_FUNC(cellSysutil, cellAudioOutGetState) {
    const uint32_t audio_out = a32(ctx, 0), device_index = a32(ctx, 1), out = a32(ctx, 2);
    if (!out) return ret(ctx, su::AUDIO_OUT_ERR_ILLEGAL_PARAMETER);    /* :286-289 */
    if (!mapped(out, su::AUDIO_OUT_STATE_SIZE)) return ret(ctx, su::AUDIO_OUT_ERR_ILLEGAL_PARAMETER);

    su::AudioOut view;
    bool secondary_past_end = false;
    const int32_t rc = su::audio_out_get_state(audio_out, device_index, view, secondary_past_end);
    if (rc < 0) return ret(ctx, rc);
    if (secondary_past_end) {
        su::write_audio_out_secondary_past_end(out);
        return ret(ctx, 0);
    }
    su::write_audio_out_state(out, view);
    LOG_INFO("sysutil", "cellAudioOutGetState(%u, %u): state %u encoder %u downMixer %u ch %u",
             audio_out, device_index, view.state, view.encoder, view.downmixer, view.sound_mode.channel);
    ret(ctx, 0);
}

/* --- cellAudioOutGetDeviceInfo ------------------------------------------
 * refs/.../cellAudioOut.cpp:489-536. The structure is CellAudioOutDeviceInfo:
 * portType, availableModeCount, state, reserved[3], latency, then up to 16
 * CellAudioOutSoundMode entries (cellAudioOut.h:142-150), which is 0x88 bytes
 * -- NOT the 0x1C8 of the video-out structure, which is the size that once
 * overran a caller's frame in this tree.
 *
 * A deviceIndex past the end is PARAMETER_OUT_OF_RANGE for the primary and
 * CELL_OK with a ZEROED structure for the secondary (:505-513).
 */
HLE_FUNC(cellSysutil, cellAudioOutGetDeviceInfo) {
    const uint32_t audio_out = a32(ctx, 0), device_index = a32(ctx, 1), info = a32(ctx, 2);
    if (!info) return ret(ctx, su::AUDIO_OUT_ERR_ILLEGAL_PARAMETER);   /* :493-496 */
    if (!mapped(info, su::AUDIO_OUT_DEVICE_INFO_SIZE)) return ret(ctx, su::AUDIO_OUT_ERR_ILLEGAL_PARAMETER);

    su::AudioOut view;
    bool zeroed_past_end = false;
    const int32_t rc = su::audio_out_get_device_info(audio_out, device_index, view, zeroed_past_end);
    if (rc < 0) return ret(ctx, rc);
    if (zeroed_past_end) {
        std::memset(vm::ptr(info), 0, su::AUDIO_OUT_DEVICE_INFO_SIZE);
        return ret(ctx, 0);
    }
    su::write_audio_out_device_info(info, view);
    LOG_INFO("sysutil", "cellAudioOutGetDeviceInfo(%u, %u): port %s, %u modes, first type %u ch %u fs 0x%x",
             audio_out, device_index, "HDMI", view.modes.count, view.modes.modes[0].type,
             view.modes.modes[0].channel, view.modes.modes[0].fs);
    ret(ctx, 0);
}

/* --- cellAudioOutGetConfiguration ---------------------------------------
 * refs/.../cellAudioOut.cpp:437-470: a null config is ILLEGAL_PARAMETER, the
 * secondary is UNSUPPORTED_AUDIO_OUT (it has one fixed format and cannot be
 * reconfigured), anything else is ILLEGAL_PARAMETER, and the primary reports
 * the ACTIVE configuration -- which is what a preceding cellAudioOutConfigure
 * stored, so the two agree.
 */
HLE_FUNC(cellSysutil, cellAudioOutGetConfiguration) {
    const uint32_t audio_out = a32(ctx, 0), cfg = a32(ctx, 1);
    if (!cfg) return ret(ctx, su::AUDIO_OUT_ERR_ILLEGAL_PARAMETER);    /* :441-444 */
    if (!mapped(cfg, su::AUDIO_OUT_CONFIGURATION_SIZE)) return ret(ctx, su::AUDIO_OUT_ERR_ILLEGAL_PARAMETER);
    su::AudioOut view;
    const int32_t rc = su::audio_out_get_configuration(audio_out, view);
    if (rc < 0) {
        LOG_WARN("sysutil", "cellAudioOutGetConfiguration(%u) -> 0x%x", audio_out, static_cast<unsigned>(rc));
        return ret(ctx, rc);
    }
    su::write_audio_out_configuration(cfg, view);
    LOG_INFO("sysutil", "cellAudioOutGetConfiguration(%u): channel %u encoder %u downMixer %u", audio_out,
             view.channel, view.encoder, view.downmixer);
    ret(ctx, 0);
}

/* --- cellAudioOutConfigure ----------------------------------------------
 * refs/.../cellAudioOut.cpp:339-435. A null config is ILLEGAL_PARAMETER, the
 * secondary is UNSUPPORTED_AUDIO_OUT, anything else ILLEGAL_PARAMETER. On the
 * primary the configuration is stored, an invalid downMixer keeps the previous
 * value, a matching sound mode follows the new channel/encoder, and the output
 * state is cycled DISABLED -> ENABLED, which cellAudioOutGetState then reports.
 *
 * There is no device to reopen: the mixer in runtime/src/hle/cellAudio.cpp
 * mixes whatever channel count the title opened on its own ports (1, 2, 6 or
 * 8), so nothing in the sample path depends on this value. Storing it is the
 * whole of the console behaviour that is observable to the guest.
 */
HLE_FUNC(cellSysutil, cellAudioOutConfigure) {
    const uint32_t audio_out = a32(ctx, 0), cfg = a32(ctx, 1);
    if (!cfg) return ret(ctx, su::AUDIO_OUT_ERR_ILLEGAL_PARAMETER);    /* :343-346 */
    if (!mapped(cfg, su::AUDIO_OUT_CONFIGURATION_SIZE)) return ret(ctx, su::AUDIO_OUT_ERR_ILLEGAL_PARAMETER);

    /* CellAudioOutConfiguration: u8 channel, u8 encoder, u8 reserved[10],
     * be u32 downMixer (cellAudioOut.h:123-129). */
    su::AudioOut requested;
    requested.channel = vm::ptr(cfg)[0];
    requested.encoder = vm::ptr(cfg)[1];
    requested.downmixer = vm::read32(cfg + 12);

    const int32_t rc = su::audio_out_configure(audio_out, requested);
    if (rc < 0) {
        LOG_WARN("sysutil", "cellAudioOutConfigure(%u): channel %u encoder %u downMixer %u -> 0x%x", audio_out,
                 requested.channel, requested.encoder, requested.downmixer, static_cast<unsigned>(rc));
    } else {
        LOG_INFO("sysutil", "cellAudioOutConfigure(%u): channel %u encoder %u downMixer %u", audio_out,
                 requested.channel, requested.encoder, requested.downmixer);
    }
    ret(ctx, rc);
}

/* --- cellAudioOutSetCopyControl -----------------------------------------
 * refs/.../cellAudioOut.cpp:538-563: the control range is checked BEFORE the
 * output, then the secondary is refused and any other value is
 * ILLEGAL_PARAMETER. The copy-protection flag has no effect on this runtime's
 * audio path, but the call is a validation plus a stored value, which is what
 * the reference is, so success is the truthful answer -- and an out-of-range
 * control is refused instead of ignored, which the previous body did not do.
 */
HLE_FUNC(cellSysutil, cellAudioOutSetCopyControl) {
    const uint32_t audio_out = a32(ctx, 0), control = a32(ctx, 1);
    const int32_t rc = su::audio_out_set_copy_control(audio_out, control);
    if (rc < 0) {
        LOG_WARN("sysutil", "cellAudioOutSetCopyControl(%u, %u) -> 0x%x", audio_out, control,
                 static_cast<unsigned>(rc));
    }
    ret(ctx, rc);
}

/* --- cellSysutilGetBgmPlaybackStatus ------------------------------------
 * refs/.../cellSysutil.cpp:838-852: a null pointer is
 * CELL_SYSUTIL_BGMPLAYBACK_ERROR_PARAM (0x8002b101); otherwise the manager's
 * status is copied into the caller's CellSysutilBgmPlaybackStatus -- 32 bytes:
 * playerState, enableState, contentId[16], currentFadeRatio, reserved[13]
 * (refs/.../cellSysutil.h:289-296).
 *
 * The status is not invented: enable/disable only move enableState
 * (:764-773, :799-812) and the manager starts STOPPED+DISABLED (:757-761), so
 * what a title reads back here is the state its own calls produced.
 */
HLE_FUNC(cellSysutil, cellSysutilGetBgmPlaybackStatus) {
    const uint32_t out = a32(ctx, 0);
    if (!out) return ret(ctx, su::BGM_ERR_PARAM);                    /* :842-845 */
    if (!mapped(out, su::BGM_STATUS_SIZE)) return ret(ctx, su::BGM_ERR_PARAM);
    su::write_bgm_status(out);
    ret(ctx, 0);                                                     /* :851 */
}

/* cellSysutilEnableBgmPlayback / Disable: refs/.../cellSysutil.cpp:764-773 and
 * :799-812 move the manager's enableState and return CELL_OK. Nothing plays
 * background music here, but the call has a real, observable effect --
 * cellSysutilGetBgmPlaybackStatus reports it -- so success is truthful rather
 * than fabricated. */
HLE_FUNC(cellSysutil, cellSysutilEnableBgmPlayback) {
    su::bgm_enable();
    LOG_INFO("sysutil", "cellSysutilEnableBgmPlayback -> ENABLE");
    ret(ctx, 0);
}

HLE_FUNC(cellSysutil, cellSysutilDisableBgmPlayback) {
    su::bgm_disable();
    LOG_INFO("sysutil", "cellSysutilDisableBgmPlayback -> DISABLE");
    ret(ctx, 0);
}

/* --- cellSysCacheMount --------------------------------------------------
 * refs/.../cellSysCache.cpp:178-262. A null param is CELL_SYSCACHE_ERROR_PARAM
 * (0x8002bc03); an invalid cache id is the same; the id and the title id joined
 * by '_' name the directory (:204); the guest-visible path is "/dev_hdd1"
 * (:210) and not a path inside the cache; mounting the same id again is
 * RET_OK_RELAYED (1), a new one is RET_OK_CLEARED (0).
 *
 * Two defects in the previous body are fixed here: the path it wrote into
 * getCachePath was "/dev_hdd1/caches/<id>" instead of "/dev_hdd1", and it
 * returned 0 for every id without ever reporting RELAYED, so a title could not
 * tell a fresh cache from a reused one.
 *
 * What this runtime cannot do: RPCS3 mounts /dev_hdd1 onto that directory. The
 * VFS here has one fixed mapping for /dev_hdd1 (runtime/src/vfs.cpp:101-110)
 * and no mount call, so the directory is created inside the shared /dev_hdd1
 * tree and the log says so. The guest still gets a valid, writable path.
 */
HLE_FUNC(cellSysutil, cellSysCacheMount) {
    const uint32_t param = a32(ctx, 0);
    if (!param) return ret(ctx, su::SYSCACHE_ERR_PARAM);             /* :187-190 */
    if (!mapped(param, su::SYSCACHE_PARAM_SIZE)) return ret(ctx, su::SYSCACHE_ERR_PARAM);
    const std::string id = gstr(param, su::SYSCACHE_ID_SIZE);
    std::string dir;
    /* The title id is the disc's, read from PARAM.SFO by the VFS -- never a
     * value baked into this source, which would make one disc's cache every
     * disc's cache. */
    const std::string title_id = vfs::disc_sfo().str("TITLE_ID");
    const int32_t rc = su::cache_mount(id, title_id, dir);
    if (rc < 0) {
        LOG_WARN("sysutil", "cellSysCacheMount('%s') -> 0x%x", id.c_str(), static_cast<unsigned>(rc));
        return ret(ctx, rc);
    }
    const std::string host = "/dev_hdd1/caches";
    std::error_code ec;
    std::filesystem::create_directories(vfs::host_path(host + "/" + dir), ec);
    put_str(param + su::SYSCACHE_PARAM_PATH_OFFSET, "/dev_hdd1", su::SYSCACHE_PATH_MAX);
    LOG_INFO("sysutil",
             "cellSysCacheMount('%s'): cache id '%s' created under /dev_hdd1/caches; "
             "the VFS mapping of /dev_hdd1 is fixed, so the guest path stays /dev_hdd1",
             id.c_str(), dir.c_str());
    ret(ctx, rc);
}

/* cellSysCacheClear: refs/.../cellSysCache.cpp:155-176. Before any mount this
 * is CELL_SYSCACHE_ERROR_NOTMOUNTED (0x8002bc04), not a success -- the previous
 * body returned CELL_OK for a cache that was never mounted. */
HLE_FUNC(cellSysutil, cellSysCacheClear) {
    const int32_t rc = su::cache_clear();
    if (rc < 0) LOG_WARN("sysutil", "cellSysCacheClear before any mount -> NOTMOUNTED");
    else LOG_INFO("sysutil", "cellSysCacheClear -> %d", rc);
    ret(ctx, rc);
}

/* --- cellMsgDialogProgressBar* -----------------------------------------
 * refs/.../cellMsgDialog.cpp:599-696 and the overlay's own range checks
 * (refs/.../RSX/Overlays/overlay_message_dialog.cpp:551-597). The dialog type's
 * bits 12-13 hold the number of progress bars (refs/.../cellMsgDialog.h:82), so
 * an index past that count is CELL_MSGDIALOG_ERROR_PARAM, and any of these
 * calls with no dialog open is CELL_MSGDIALOG_ERROR_DIALOG_NOT_OPENED. The
 * previous bodies returned CELL_OK unconditionally, including for a call made
 * with no dialog at all -- the exact shape of a fake success: the title's own
 * progress bookkeeping then believed a bar existed.
 *
 * A bar's VALUE only affects the overlay's drawing, and this runtime draws no
 * overlay for the message dialog (msgdialog.cpp models open/close and pad
 * input, not pixels), so the visible bar is unchanged either way; what a caller
 * can act on -- open or not, valid index or not -- is answered exactly.
 */
HLE_FUNC(cellSysutil, cellMsgDialogProgressBarInc) {
    const uint32_t index = a32(ctx, 0);
    const int32_t rc = su::msg_progress_bar_check(msgdialog::is_open(), msgdialog::dialog_type(), index);
    if (rc < 0) return ret(ctx, rc);
    /* delta is a u32; RPCS3 casts it to f32 for the overlay's bar
     * (refs/.../cellMsgDialog.cpp:668-677). */
    msgdialog::progress_bar_inc(index, a32(ctx, 1));
    ret(ctx, 0);
}

HLE_FUNC(cellSysutil, cellMsgDialogProgressBarSetMsg) {
    const uint32_t index = a32(ctx, 0);
    const uint32_t msg = a32(ctx, 1);
    const std::string text = msg ? gstr(msg, su::MSG_PROGRESSBAR_STRING_SIZE + 1) : std::string();
    const int32_t rc = su::msg_progress_bar_set_msg_check(msgdialog::is_open(), msgdialog::dialog_type(),
                                                          index, msg != 0, text.size());
    if (rc < 0) return ret(ctx, rc);
    ret(ctx, 0);
}

HLE_FUNC(cellSysutil, cellMsgDialogProgressBarReset) {
    const uint32_t index = a32(ctx, 0);
    const int32_t rc = su::msg_progress_bar_check(msgdialog::is_open(), msgdialog::dialog_type(), index);
    if (rc < 0) return ret(ctx, rc);
    msgdialog::progress_bar_reset(index);
    ret(ctx, 0);
}

/* cellSaveDataListAutoLoad lives in runtime/src/hle/cellSaveData.cpp, where the
 * save enumeration and funcFixed delivery are. It is registered once, there. */


HLE_FUNC(cellSysutil, cellSaveDataListLoad2) {
    ret(ctx, static_cast<int32_t>(0x8002b40b));
}

/* cellSaveDataDelete2(container): refs/.../cellSaveData.cpp:2527-2532 hands the
 * call to select_and_delete (:295-340), which shows the save-data list, and
 * returns CELL_CANCEL (1, refs/.../ErrorCodes.h:68) when nothing is selected.
 * This runtime has no save-data selection UI, so nothing is ever selected and
 * that is the answer. The previous body returned CELL_OK, telling the title a
 * deletion had happened that never did -- and the duplicate registration in
 * cellSaveData.cpp is removed so the two cannot disagree.
 */
HLE_FUNC(cellSysutil, cellSaveDataDelete2) {
    LOG_INFO("sysutil", "cellSaveDataDelete2: no save-data selection UI -> CELL_CANCEL, nothing deleted");
    ret(ctx, su::CELL_CANCEL);
}

/* cellSaveDataFixedDelete: the same select_and_delete path
 * (refs/.../cellSaveData.cpp:2534-2540), so it also answers CELL_CANCEL. */
HLE_FUNC(cellSysutil, cellSaveDataFixedDelete) {
    LOG_INFO("sysutil", "cellSaveDataFixedDelete: no save-data selection UI -> CELL_CANCEL, nothing deleted");
    ret(ctx, su::CELL_CANCEL);
}

/* --- cellOskDialogAbort -------------------------------------------------
 * refs/.../cellOskDialog.cpp:792-828: with no dialog open it returns
 * CELL_MSGDIALOG_ERROR_DIALOG_NOT_OPENED (0x8002b302); otherwise it closes with
 * the ABORT input result and raises CELL_SYSUTIL_OSKDIALOG_FINISHED (:818-825).
 *
 * The OSK state lives in cellOskDialog.cpp; this asks it, so "is a dialog open"
 * has one owner instead of a second copy here.
 */
HLE_FUNC(cellSysutil, cellOskDialogAbort) {
    const int32_t rc = su::osk_abort_check(osk::is_open());
    if (rc < 0) {
        LOG_WARN("sysutil", "cellOskDialogAbort with no dialog open -> DIALOG_NOT_OPENED");
        return ret(ctx, rc);
    }
    osk::abort();
    ret(ctx, 0);
}

/* --- cellOskDialogSetDeviceMask ----------------------------------------
 * refs/.../cellOskDialog.cpp:830-858: the only accepted mask is
 * CELL_OSKDIALOG_DEVICE_MASK_PAD (0xff); anything above it is
 * CELL_OSKDIALOG_ERROR_PARAM (0x8002b504). The previous registration of this
 * import did not exist at all.
 */
HLE_FUNC(cellSysutil, cellOskDialogSetDeviceMask) {
    const uint32_t mask = a32(ctx, 0);
    const int32_t rc = su::osk_set_device_mask(mask);
    if (rc < 0) LOG_WARN("sysutil", "cellOskDialogSetDeviceMask(0x%x) -> PARAM", mask);
    ret(ctx, rc);
}

namespace {
/* Message-dialog type bits (refs/rpcs3/rpcs3/Emu/Cell/Modules/cellMsgDialog.h:26-52). */
constexpr uint32_t CELL_MSGDIALOG_TYPE_SE_ERROR = 0u << 0;
constexpr uint32_t CELL_MSGDIALOG_TYPE_BUTTON_OK = 2u << 4;
}

/* --- cellMsgDialog*: a real system dialog, not an auto-answer -------------
 *
 * See runtime/include/runtime/msgdialog.h for the state machine and which RPCS3
 * lines the button values come from. The dialog opens, the SYSTEM reads the
 * pad, the callback fires when -- and only when -- a button that the dialog type
 * accepts is pressed, and while a dialog is open a second open2 returns
 * CELL_SYSUTIL_ERROR_BUSY exactly as firmware does.
 *
 * Measurement that motivated it: the "level autosave feature" dialog this title
 * shows is NOT one of ours. No cellMsgDialog* import is ever called in a run
 * that displays it, and no cellSaveData error dialog is raised either. The
 * title draws that box itself. This is still the right behaviour for every
 * OTHER title dialog. */
HLE_FUNC(cellSysutil, cellMsgDialogOpen2) {
    uint32_t type = a32(ctx, 0);
    std::string msg = gstr(a32(ctx, 1));
    uint32_t cb = a32(ctx, 2), user = a32(ctx, 3);
    ret(ctx, msgdialog::open(type, msg.c_str(), cb, user));
}

/* cellMsgDialogOpenErrorCode(errorCode, callback, userData, extParam):
 * refs/rpcs3/.../cellMsgDialog.cpp:432-505 turns the code into localized text
 * and opens an error-type OK dialog. We keep the code in the message so a log
 * identifies which error the title reported, and use the same OK-button type
 * so the same pad rule applies. */
HLE_FUNC(cellSysutil, cellMsgDialogOpenErrorCode) {
    uint32_t code = a32(ctx, 0);
    uint32_t cb = a32(ctx, 1), user = a32(ctx, 2);
    char buf[64];
    std::snprintf(buf, sizeof buf, "An error has occurred. (0x%08x)", code);
    LOG_WARN("cellSysutil", "cellMsgDialogOpenErrorCode(0x%08x)", code);
    ret(ctx, msgdialog::open(CELL_MSGDIALOG_TYPE_SE_ERROR | CELL_MSGDIALOG_TYPE_BUTTON_OK,
                             buf, cb, user));
}

HLE_FUNC(cellSysutil, cellMsgDialogClose) {
    /* The argument is a float delay in milliseconds. */
    const float delay = static_cast<float>(ctx.fpr[1]);
    ret(ctx, msgdialog::close_dialog(delay > 0.0f ? static_cast<uint32_t>(delay) : 0u));
}

HLE_FUNC(cellSysutil, cellMsgDialogAbort) {
    ret(ctx, msgdialog::abort());
}
/* cellVideoOutGetScreenSize(videoOut, screenSize)
 * refs/rpcs3/rpcs3/Emu/Cell/Modules/cellAvconfExt.cpp:553-577.
 *
 * The previous body wrote 0 into the caller's float and returned
 * 0x8002b226, which is CELL_VIDEO_OUT_ERROR_UNSUPPORTED_DISPLAY_MODE -- a code
 * this function never returns. RPCS3 returns VALUE_IS_NOT_SET (0x8002b228) and
 * writes NOTHING, because "not set" means the caller's variable keeps its own
 * value. Writing 0 first and then reporting "not set" is a value the caller
 * cannot distinguish from a reported one. */
HLE_FUNC(cellSysutilAvconfExt, cellVideoOutGetScreenSize) {
    const uint32_t video_out = a32(ctx, 0);
    const uint32_t screen_size = a32(ctx, 1);
    if (!screen_size) return ret(ctx, static_cast<int32_t>(0x8002b222));  /* ILLEGAL_PARAMETER */
    if (video_out != 0) return ret(ctx, static_cast<int32_t>(0x8002b225));  /* UNSUPPORTED_VIDEO_OUT */
    /* No 3D display is modelled, so the size is genuinely not set. */
    LOG_INFO("cellSysutil", "cellVideoOutGetScreenSize -> not set (no 3D display modelled)");
    ret(ctx, static_cast<int32_t>(0x8002b228));
}

/* cellVideoOutSetGamma(videoOut, gamma)
 * refs/rpcs3/rpcs3/Emu/Cell/Modules/cellAvconfExt.cpp:448-467.
 *
 * The argument order and every return code are RPCS3's: gamma outside
 * [0.8, 1.2] is PARAMETER_OUT_OF_RANGE, a videoOut other than PRIMARY is
 * UNSUPPORTED_VIDEO_OUT, and only then is the value accepted. RPCS3 stores it
 * in avconf and its present path builds an sRGB conversion from it
 * (GL/GLPresent.cpp:438-445, VK/VKPresent.cpp:850-886). This runtime has no
 * gamma stage -- presenter.cpp blits straight to the swapchain -- so storing a
 * number nothing reads and answering CELL_OK would tell the title the picture
 * changed when it did not. See runtime/src/hle/avconf_hle.h. */
HLE_FUNC(cellSysutilAvconfExt, cellVideoOutSetGamma) {
    const uint32_t video_out = a32(ctx, 0);
    /* A float argument travels in f1: f0 is scratch (see cellMsgDialogClose,
     * which reads ctx.fpr[1] for the same reason). */
    const float gamma = static_cast<float>(ctx.fpr[1]);
    const int32_t rc = avconf::set_gamma(video_out, gamma);
    if (rc == 0 && !avconf::is_identity(gamma)) {
        /* Unreachable by construction, kept so the two cannot drift apart. */
        LOG_WARN("cellSysutil", "cellVideoOutSetGamma(%u, %f) returned OK for a non-identity gamma", video_out, gamma);
    } else if (rc == avconf::ERR_NOT_IMPLEMENTED) {
        LOG_WARN("cellSysutil",
                 "cellVideoOutSetGamma(%u, %f) -> NOT_IMPLEMENTED: this runtime has no gamma stage, "
                 "the picture is unchanged",
                 video_out, gamma);
    } else {
        LOG_INFO("cellSysutil", "cellVideoOutSetGamma(%u, %f) -> 0x%x", video_out, gamma, static_cast<unsigned>(rc));
    }
    ret(ctx, rc);
}

/* --- cellVideoOutGetDeviceInfo ------------------------------------------
 *
 * A title calls this to learn what its display can do, then picks a mode from
 * the answer. Returning ENOSYS left it with an uninitialised structure, so it
 * could not choose a resolution and submitted nothing -- which is exactly the
 * shape of the empty frames this runtime was producing.
 *
 * The structure is CellVideoOutDeviceInfo: port type, colour space, latency,
 * a mode count, the state, and an array of up to 32 display modes. Only the
 * modes matter for choosing, so the array is filled with what this runtime
 * actually supports and the count says how many.
 */
namespace {

struct VideoMode {
    uint8_t resolution_id;
    uint8_t aspect;
    uint16_t refresh_bits;   /* bit0 60 Hz, bit1 50 Hz, bit2 59.94 */
};

/* 1920x1080 and 1280x720 at 60 Hz, 720x480 and 720x576 for completeness.
 * The ids are the console's, not an enum of ours: a title compares them
 * against constants compiled into it. */
constexpr VideoMode kModes[] = {
    {1, 1, 0x0001},   /* 1920x1080, 16:9 */
    {2, 1, 0x0001},   /* 1280x720,  16:9 */
    {4, 2, 0x0001},   /* 720x480,   4:3  */
    {5, 3, 0x0001},   /* 720x576,   4:3  */
};

/* The structure is exactly 0x1C8 bytes:
 *   0x000 u8 portType, colorSpace; u16 latency; u8 availCount, state, rgbRange,
 *         reserved[5]                                     (12)
 *   0x00C CellVideoOutColorInfo: 8 x u16 + u32 gamma        (20)
 *   0x020 CellVideoOutDisplayMode availableModes[32]        (256)
 *   0x120 CellVideoOutKSVList: 160 + 4 + 4                  (168)
 * An earlier value of 0x3A0 zeroed 0x1D8 bytes PAST the caller's structure and
 * destroyed its stack frame -- the crash then appeared at a guest address of
 * 0x6904, nowhere near this function. */
constexpr uint32_t kDeviceInfoSize = 0x1C8;

}  // namespace

HLE_FUNC(cellSysutil, cellVideoOutGetDeviceInfo) {
    const uint32_t video_out = a32(ctx, 0);
    const uint32_t index = a32(ctx, 1);
    const uint32_t out = a32(ctx, 2);

    if (out == 0) return ret(ctx, static_cast<int32_t>(0x8002b222));  /* CELL_EINVAL */
    if (video_out != 0) return ret(ctx, static_cast<int32_t>(0x8002b224));  /* CELL_VIDEO_OUT_ERROR_ILLEGAL_PARAMETER */
    if (index != 0) return ret(ctx, static_cast<int32_t>(0x8002b225));  /* no such device */

    /* Zero first: the caller may read fields this runtime has nothing to say
     * about, and a stale stack value would be read as a capability. */
    std::memset(vm::ptr(out), 0, kDeviceInfoSize);

    uint8_t* p = vm::ptr(out);
    p[0] = 1;    /* portType: HDMI */
    p[1] = 1;    /* colorSpace: RGB */
    vm::write16(out + 2, 0);            /* latency */
    p[4] = static_cast<uint8_t>(sizeof(kModes) / sizeof(kModes[0]));
    p[5] = 0;    /* state: 0 means "this device is available" */
    p[6] = 1;    /* rgbOutputRange: full */

    /* colorInfo at +0x0C: the sRGB primaries, scaled by 100000. A title that
     * reads them only uses them for a colour conversion; leaving them zero
     * makes it conclude the display has no gamut. */
    const uint16_t primaries[8] = {64000, 33000, 30000, 60000, 15000, 6000, 31270, 32900};
    for (int i = 0; i < 8; i++) vm::write16(out + 0x0C + i * 2, primaries[i]);
    vm::write32(out + 0x1C, 2200);      /* gamma, x100 */

    /* availableModes at +0x20, 8 bytes each. */
    uint32_t mode_ea = out + 0x20;
    for (const VideoMode& m : kModes) {
        vm::ptr(mode_ea)[0] = m.resolution_id;
        vm::ptr(mode_ea)[1] = 0;        /* scanMode: progressive */
        vm::ptr(mode_ea)[2] = 0;        /* conversion: none */
        vm::ptr(mode_ea)[3] = m.aspect;
        vm::write16(mode_ea + 6, m.refresh_bits);
        mode_ea += 8;
    }

    LOG_INFO("cellSysutil",
             "cellVideoOutGetDeviceInfo -> %u modes, first %ux%u",
             (unsigned)p[4], 1920u, 1080u);
    ret(ctx, 0);
}

}  // namespace hle
