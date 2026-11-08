#include "common.h"

#include <atomic>
#include "runtime/cbwatch.h"
#include "runtime/sysutil_events.h"
#include "runtime/vfs.h"

#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>

namespace hle {

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

}

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

HLE_FUNC(cellSysutil, cellSysutilRegisterCallback) {
    uint32_t slot = a32(ctx, 0);
    if (slot > 3) return ret(ctx, static_cast<int32_t>(0x8002b101));
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
    ret(ctx, 0);
}

HLE_FUNC(cellSysutil, cellSysutilUnregisterCallback) {
    uint32_t slot = a32(ctx, 0);
    if (slot > 3) return ret(ctx, static_cast<int32_t>(0x8002b101));
    std::lock_guard lk(g_lock);
    g_callbacks[slot] = {};
    ret(ctx, 0);
}

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

HLE_FUNC(cellSysutil, cellSysutilGetSystemParamInt) {
    uint32_t id = a32(ctx, 0), out = a32(ctx, 1);
    uint32_t v = 0;
    switch (id) {
    case 0x111: v = 1; break;
    case 0x112: v = 1; break;
    case 0x114: v = 1; break;
    case 0x115: v = 1; break;
    case 0x116: v = 0; break;
    case 0x117: v = 0; break;
    case 0x121: v = 9; break;
    case 0x123: v = 0; break;
    case 0x141: v = 0; break;
    case 0x151: v = 1; break;
    case 0x152: v = 1; break;
    case 0x156: v = 0; break;
    default: LOG_WARN("cellSysutil", "unknown system param int 0x%x", id); break;
    }
    if (out) vm::write32(out, v);
    ret(ctx, 0);
}

HLE_FUNC(cellSysutil, cellSysutilGetSystemParamString) {
    uint32_t id = a32(ctx, 0), buf = a32(ctx, 1), size = a32(ctx, 2);
    put_str(buf, id == 0x131 ? "Kratos" : "", size);
    ret(ctx, 0);
}

HLE_FUNC(cellSysutil, cellVideoOutGetState) {
    uint32_t out = a32(ctx, 2);
    if (!out) return ret(ctx, static_cast<int32_t>(0x8002b222));
    std::memset(vm::ptr(out), 0, 16);
    if (a32(ctx, 0) != 0) {
        vm::ptr(out)[0] = 2;
        return ret(ctx, 0);
    }
    uint8_t* p = vm::ptr(out);
    p[0] = 0;
    p[1] = 1;
    p[8] = 2;
    p[9] = 1;
    p[10] = 0;
    p[11] = 2;
    vm::write16(out + 14, 1);
    ret(ctx, 0);
}

HLE_FUNC(cellSysutil, cellVideoOutGetResolution) {
    uint32_t id = a32(ctx, 0), out = a32(ctx, 1);
    uint16_t w = 0, h = 0;
    switch (id) {
    case 1: w = 1920; h = 1080; break;
    case 2: w = 1280; h = 720; break;
    case 4: w = 720; h = 480; break;
    case 5: w = 720; h = 576; break;
    default: return ret(ctx, static_cast<int32_t>(0x8002b224));
    }
    vm::write16(out, w);
    vm::write16(out + 2, h);
    ret(ctx, 0);
}

HLE_FUNC(cellSysutil, cellVideoOutGetResolutionAvailability) {
    uint32_t id = a32(ctx, 1);
    ret(ctx, (id == 1 || id == 2) ? 1 : 0);
}

HLE_FUNC(cellSysutil, cellVideoOutConfigure) {
    uint32_t cfg = a32(ctx, 1);
    if (cfg) LOG_INFO("cellSysutil", "cellVideoOutConfigure res %u format %u aspect %u pitch %u", vm::ptr(cfg)[0], vm::ptr(cfg)[1], vm::ptr(cfg)[2], vm::read32(cfg + 12));
    ret(ctx, 0);
}

HLE_FUNC(cellSysutil, cellAudioOutGetSoundAvailability) {
    uint32_t type = a32(ctx, 1);
    ret(ctx, type == 0 ? 2 : 0);
}

/* cellAudioOutSetCopyControl(mode): the copy-protection flag on the audio
 * output. Nothing we can act on, but the title calls it during startup and an
 * unimplemented import returns 0 without setting its out-parameter. */
HLE_FUNC(cellSysutil, cellAudioOutSetCopyControl) {
    ret(ctx, 0);
}

HLE_FUNC(cellSysutil, cellAudioOutConfigure) {
    ret(ctx, 0);
}

HLE_FUNC(cellSysutil, cellSysCacheMount) {
    uint32_t param = a32(ctx, 0);
    std::string id = gstr(param, 32);
    /* A cache path labelled with one specific title id makes every other disc
     * share that title's cache -- and silently, because the path is internal. */
    std::string guest = "/dev_hdd1/caches/" + (id.empty() ? std::string("ps3base") : id);
    std::error_code ec;
    std::filesystem::create_directories(vfs::host_path(guest), ec);
    put_str(param + 32, guest, 1024);
    LOG_INFO("cellSysutil", "cellSysCacheMount('%s') -> %s", id.c_str(), guest.c_str());
    ret(ctx, 0);
}

HLE_FUNC(cellSysutil, cellSysCacheClear) {
    ret(ctx, 0);
}

HLE_FUNC(cellSysutil, cellMsgDialogOpen2) {
    uint32_t type = a32(ctx, 0);
    std::string msg = gstr(a32(ctx, 1));
    uint32_t cb = a32(ctx, 2), user = a32(ctx, 3);
    LOG_WARN("cellSysutil", "cellMsgDialogOpen2(0x%x) '%s'", type, msg.c_str());
    if (cb) {
        uint32_t button = ((type >> 4) & 7) == 1 ? 1 : 3;
        post([cb, user, button](ppu::Context& c) { call_guest(c, cb, button, user); });
    }
    ret(ctx, 0);
}

HLE_FUNC(cellSysutil, cellSaveDataListAutoLoad) {
    ret(ctx, static_cast<int32_t>(0x8002b40b));
}

HLE_FUNC(cellSysutil, cellSaveDataListLoad2) {
    ret(ctx, static_cast<int32_t>(0x8002b40b));
}

HLE_FUNC(cellSysutil, cellSaveDataDelete2) {
    ret(ctx, 0);
}

HLE_FUNC(cellSysutil, cellSaveDataFixedDelete) {
    ret(ctx, 0);
}

HLE_FUNC(cellSysutilAvconfExt, cellVideoOutGetScreenSize) {
    if (uint32_t p = a32(ctx, 1)) vm::write32(p, 0);
    ret(ctx, static_cast<int32_t>(0x8002b226));
}

HLE_FUNC(cellSysutilAvconfExt, cellVideoOutSetGamma) {
    ret(ctx, 0);
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

/* cellSysutilEnableBgmPlayback / Disable: the title tells the OS that
 * background music is starting. Nothing here plays BGM, and refusing the call
 * makes a title treat the feature as unavailable and take a slower path. There
 * is no BGM to enable, so success is the truthful answer -- and unlike a
 * fabricated CELL_OK for an object, there is no state the caller can later
 * depend on being wrong. */
HLE_FUNC(cellSysutil, cellSysutilEnableBgmPlayback) {
    ret(ctx, 0);
}

HLE_FUNC(cellSysutil, cellSysutilDisableBgmPlayback) {
    ret(ctx, 0);
}

}  // namespace hle
