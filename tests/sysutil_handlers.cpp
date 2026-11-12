/* ps3base -- the cellSysutil HANDLERS, not just their logic.
 *
 * tests/sysutil.cpp drives the reference rules through sysutil_hle.h. This file
 * drives the HLE_FUNC bodies themselves, so the two things a header test cannot
 * see are checked:
 *   - the ARGUMENT ORDER: each handler is called with a real ppu::Context and
 *     the values land in r3.. and on the stack where the ABI puts them;
 *   - the RETURN VALUE in r3, sign-extended, which is what the import path
 *     hands the title.
 *
 * The module is included and its external services (the message dialog, the
 * OSK, the VFS, the callback ledger) are stubbed here, so the test is a pure
 * host program: no game, no GPU, no network, no threads started.
 */
#include "ppu/context.h"
#include "runtime/hle.h"
#include "runtime/memory.h"
#include "runtime/vfs.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

/* --- stubs for the services the module calls ---------------------------- */
namespace hle {
namespace msgdialog {
namespace {
bool g_open = false;
uint32_t g_type = 0;
float g_bar[2] = {0, 0};
}  // namespace
int32_t open(uint32_t, const char*, uint32_t, uint32_t) { return 0; }
int32_t close_dialog(uint32_t) { return 0; }
int32_t abort() { return 0; }
bool is_open() { return g_open; }
uint32_t dialog_type() { return g_type; }
void progress_bar_inc(uint32_t index, uint32_t delta) { if (index < 2) g_bar[index] += static_cast<float>(delta); }
void progress_bar_reset(uint32_t index) { if (index < 2) g_bar[index] = 0; }
float progress_bar_value(uint32_t index) { return index < 2 ? g_bar[index] : 0.f; }
uint32_t opens_seen() { return 0; }
uint32_t resolves_seen() { return 0; }
uint32_t last_button_value() { return 0; }
const char* last_button_name() { return "NONE"; }
void on_pad(const uint16_t*) {}
namespace testhooks {
void set(bool open, uint32_t type) { g_open = open; g_type = type; }
float bar(uint32_t i) { return i < 2 ? g_bar[i] : 0.f; }
}
}

namespace osk {
bool is_open() { return false; }
void abort() {}
}
}

namespace cbwatch {
bool enabled() { return false; }
void note_import(const char*, const char*, uint64_t, uint32_t, bool) {}
void note_register(const char*, uint32_t, uint32_t) {}
void note_call(const char*, uint32_t, uint64_t) {}
void dump(const char*) {}
}

namespace vfs {
std::string host_path(const std::string&) { return {}; }
const Psf& disc_sfo() {
    static Psf s;
    s.strings["TITLE_ID"] = "BLES00680";
    return s;
}
/* The real Psf::str only reads the parsed maps (runtime/src/vfs.cpp:140-150);
 * this test never loads a file, so the same lookup is enough and keeps the test
 * free of the filesystem. */
std::string Psf::str(const std::string& key) const {
    const auto it = strings.find(key);
    return it == strings.end() ? std::string() : it->second;
}
}

namespace hle {
void call_guest(ppu::Context&, uint32_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {}

/* hle::Reg normally registers into the import table (runtime/src/ppu_runtime.cpp:81-83).
 * This test calls the handlers directly, so the constructor records the key in a
 * local map instead of pulling in the whole PPU runtime -- the same trick as
 * tests/cellgameexec.cpp. Registration NAMES are checked too: the dispatcher
 * builds "module::name", so a typo there makes the import resolve nowhere.
 *
 * The map is function-local, not a namespace-scope object: the static hle::Reg
 * instances inside the included module run BEFORE any namespace-scope object
 * declared after it. */
std::map<std::string, Fn>& test_registry() {
    static std::map<std::string, Fn> m;
    return m;
}

Reg::Reg(const char* module, const char* name, Fn fn) {
    test_registry()[std::string(module) + "::" + name] = fn;
}
}  // namespace hle

/* Included AFTER the stubs above and OUTSIDE namespace hle: the module's own
 * "#include <atomic>" and friends must land in the global namespace, not inside
 * hle, or every name from the standard library resolves relative to it. */
#include "../runtime/src/hle/cellSysutil.cpp"

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

/* One call, with up to four register arguments. r3 is the return value. */
int64_t call(ppu::Context& ctx, void (*fn)(ppu::Context&, uint8_t*), uint64_t a0 = 0, uint64_t a1 = 0,
             uint64_t a2 = 0, uint64_t a3 = 0) {
    ctx = ppu::Context{};
    ctx.gpr[3] = a0;
    ctx.gpr[4] = a1;
    ctx.gpr[5] = a2;
    ctx.gpr[6] = a3;
    fn(ctx, vm::g_base);
    return static_cast<int64_t>(ctx.gpr[3]);
}

int32_t low32(int64_t v) { return static_cast<int32_t>(v & 0xFFFFFFFFll); }

}  // namespace

int main() {
    if (!vm::init()) {
        std::fprintf(stderr, "vm::init failed\n");
        return 1;
    }
    ppu::Context ctx;
    const uint32_t buf = alloc_area(0x3000);
    if (!buf) return 1;
    hle::sysutil::state() = hle::sysutil::State{};

    /* --- cellAudioOutGetNumberOfDevice: the answer is the RETURN VALUE -- */
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellAudioOutGetNumberOfDevice, 0)) == 1,
          "the primary output reports one device through r3");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellAudioOutGetNumberOfDevice, 1)) == 0,
          "the secondary output reports zero devices through r3");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellAudioOutGetNumberOfDevice, 5)) ==
              static_cast<int32_t>(0x8002b242),
          "an unknown output returns ILLEGAL_PARAMETER through r3");

    /* --- cellAudioOutGetSoundAvailability: (audioOut, type, fs, option) --- */
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellAudioOutGetSoundAvailability, 0, 0, 0x04)) == 2,
          "2 channels for LPCM at 48 kHz, from the third argument (fs)");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellAudioOutGetSoundAvailability, 0, 0, 0x40)) == 0,
          "0 channels for LPCM at 192 kHz: fs is read, not ignored");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellAudioOutGetSoundAvailability, 0, 1, 0x04)) == 0,
          "0 channels for AC3: type is read, not ignored");

    /* --- cellAudioOutGetConfiguration: writes the caller's structure ---- */
    std::memset(vm::ptr(buf), 0xAA, 0x40);
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellAudioOutGetConfiguration, 0, buf)) == 0,
          "the primary configuration returns CELL_OK");
    CHECK(vm::ptr(buf)[0] == 2, "the written configuration has 2 channels at offset 0");
    CHECK(vm::ptr(buf)[1] == 0, "and LPCM at offset 1");
    CHECK(vm::read32(buf + 12) == 0, "and downMixer NONE at offset 12");
    CHECK(static_cast<uint8_t>(vm::ptr(buf)[16]) == 0xAA, "nothing is written past 16 bytes");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellAudioOutGetConfiguration, 0, 0)) ==
              static_cast<int32_t>(0x8002b242),
          "a null config is ILLEGAL_PARAMETER");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellAudioOutGetConfiguration, 1, buf)) ==
              static_cast<int32_t>(0x8002b245),
          "the secondary output is UNSUPPORTED_AUDIO_OUT");

    /* --- cellAudioOutConfigure: (audioOut, config, option, waitForEvent) - */
    std::memset(vm::ptr(buf), 0, 0x40);
    vm::ptr(buf)[0] = 6;               /* channel */
    vm::ptr(buf)[1] = 0;               /* encoder LPCM */
    vm::write32(buf + 12, 1);          /* downMixer TYPE_A */
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellAudioOutConfigure, 0, buf, 0, 1)) == 0,
          "configuring the primary returns CELL_OK");
    CHECK(hle::sysutil::state().out[0].channel == 6, "the handler stored the channel count");
    CHECK(hle::sysutil::state().out[0].downmixer == 1, "and the downMixer");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellAudioOutConfigure, 0, 0, 0, 0)) ==
              static_cast<int32_t>(0x8002b242),
          "a null config is ILLEGAL_PARAMETER");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellAudioOutConfigure, 1, buf, 0, 0)) ==
              static_cast<int32_t>(0x8002b245),
          "configuring the secondary is UNSUPPORTED_AUDIO_OUT");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellAudioOutConfigure, 2, buf, 0, 0)) ==
              static_cast<int32_t>(0x8002b242),
          "configuring an unknown output is ILLEGAL_PARAMETER");

    /* --- cellAudioOutGetState: (audioOut, deviceIndex, state) ----------- */
    hle::sysutil::state() = hle::sysutil::State{};
    std::memset(vm::ptr(buf), 0xAA, 0x40);
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellAudioOutGetState, 0, 0, buf)) == 0,
          "the primary state returns CELL_OK");
    CHECK(vm::ptr(buf)[0] == 0, "the output state byte is at offset 0 and reports ENABLED");
    CHECK(vm::ptr(buf)[1] == 0, "the encoder byte is at offset 1 and reports LPCM");
    CHECK(vm::ptr(buf + 12)[1] == 2, "the sound-mode channel is at offset 13");
    CHECK(vm::read32(buf + 16) == 1, "the sound-mode layout is at offset 16");
    CHECK(static_cast<uint8_t>(vm::ptr(buf)[0x14]) == 0xAA, "nothing is written past 0x14 bytes");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellAudioOutGetState, 0, 1, buf)) ==
              static_cast<int32_t>(0x8002b243),
          "device index 1 of the primary is PARAMETER_OUT_OF_RANGE");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellAudioOutGetState, 0, 0, 0)) ==
              static_cast<int32_t>(0x8002b242),
          "a null state is ILLEGAL_PARAMETER");
    std::memset(vm::ptr(buf), 0xAA, 0x40);
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellAudioOutGetState, 1, 0, buf)) == 0,
          "past the end of the secondary is CELL_OK, not an error");
    CHECK(vm::ptr(buf)[0] == 0x10, "and writes the console's constant state 0x10");
    CHECK(vm::read32(buf + 16) == 0xD00C1680u, "and the constant layout 0xD00C1680");

    /* --- cellAudioOutGetDeviceInfo: the 0x88-byte structure ------------- */
    hle::sysutil::state() = hle::sysutil::State{};
    std::memset(vm::ptr(buf), 0xAA, 0x100);
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellAudioOutGetDeviceInfo, 0, 0, buf)) == 0,
          "the primary device info returns CELL_OK");
    CHECK(vm::ptr(buf)[0] == 0, "portType HDMI is at offset 0");
    CHECK(vm::ptr(buf)[1] == 1, "availableModeCount is at offset 1");
    CHECK(vm::ptr(buf)[2] == 1, "state AVAILABLE is at offset 2");
    CHECK(vm::read16(buf + 6) == 13, "latency is at offset 6");
    CHECK(vm::ptr(buf + 8)[1] == 2, "availableModes[0].channel is at offset 9");
    CHECK(static_cast<uint8_t>(vm::ptr(buf)[0x88]) == 0xAA,
          "nothing is written past the 0x88-byte CellAudioOutDeviceInfo");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellAudioOutGetDeviceInfo, 0, 1, buf)) ==
              static_cast<int32_t>(0x8002b243),
          "device index 1 of the primary is PARAMETER_OUT_OF_RANGE");
    std::memset(vm::ptr(buf), 0xAA, 0x100);
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellAudioOutGetDeviceInfo, 1, 0, buf)) == 0,
          "past the end of the secondary is CELL_OK");
    CHECK(vm::ptr(buf)[0] == 0 && vm::ptr(buf)[1] == 0 && static_cast<uint8_t>(vm::ptr(buf)[0x87]) == 0,
          "with a completely zeroed structure (:505-513)");

    /* --- cellAudioOutSetCopyControl: the range is checked first --------- */
    hle::sysutil::state() = hle::sysutil::State{};
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellAudioOutSetCopyControl, 1, 3)) ==
              static_cast<int32_t>(0x8002b242),
          "control 3 is ILLEGAL_PARAMETER before the audioOut is looked at");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellAudioOutSetCopyControl, 0, 2)) == 0,
          "control 2 on the primary succeeds");
    CHECK(hle::sysutil::state().out[0].copy_control == 2, "and is stored");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellAudioOutSetCopyControl, 1, 0)) ==
              static_cast<int32_t>(0x8002b245),
          "a valid control on the secondary is UNSUPPORTED_AUDIO_OUT");

    /* --- cellVideoOutGetResolution: (resolutionId, resolution) ---------- */
    std::memset(vm::ptr(buf), 0xAA, 0x10);
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellVideoOutGetResolution, 2, buf)) == 0,
          "resolution id 2 returns CELL_OK");
    CHECK(vm::read16(buf) == 1280 && vm::read16(buf + 2) == 720, "and writes 1280x720");
    CHECK(static_cast<uint8_t>(vm::ptr(buf)[4]) == 0xAA, "nothing is written past 4 bytes");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellVideoOutGetResolution, 0x0A, buf)) == 0 &&
              vm::read16(buf) == 1600,
          "id 0x0a is 1600x1080: the previous body refused it");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellVideoOutGetResolution, 3, buf)) ==
              static_cast<int32_t>(0x8002b222),
          "an id outside the table is ILLEGAL_PARAMETER, not UNSUPPORTED_DISPLAY_MODE");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellVideoOutGetResolution, 2, 0)) ==
              static_cast<int32_t>(0x8002b222),
          "a null resolution pointer is ILLEGAL_PARAMETER");

    /* --- cellVideoOutGetState: (videoOut, deviceIndex, state) ----------- */
    hle::sysutil::state() = hle::sysutil::State{};
    std::memset(vm::ptr(buf), 0xAA, 0x20);
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellVideoOutGetState, 0, 0, buf)) == 0,
          "the primary video-out state returns CELL_OK");
    CHECK(vm::ptr(buf)[0] == 0, "state ENABLED is at offset 0");
    CHECK(vm::ptr(buf)[1] == 1, "colour space RGB is at offset 1");
    CHECK(vm::ptr(buf + 8)[0] == 2, "displayMode.resolutionId is at offset 8 (720p by default)");
    CHECK(vm::read16(buf + 8 + 6) == 1, "displayMode.refreshRates is at offset 14 (59.94 Hz)");
    CHECK(static_cast<uint8_t>(vm::ptr(buf)[16]) == 0xAA, "nothing is written past 16 bytes");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellVideoOutGetState, 0, 1, buf)) ==
              static_cast<int32_t>(0x8002b224),
          "device index 1 is DEVICE_NOT_FOUND");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellVideoOutGetState, 0, 0, 0)) ==
              static_cast<int32_t>(0x8002b222),
          "a null state is ILLEGAL_PARAMETER");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellVideoOutGetState, 9, 0, buf)) ==
              static_cast<int32_t>(0x8002b224),
          "an unknown videoOut collapses into DEVICE_NOT_FOUND (:133-136)");

    /* --- cellVideoOutConfigure: (videoOut, config, option, waitForEvent) - */
    hle::sysutil::state() = hle::sysutil::State{};
    std::memset(vm::ptr(buf), 0, 0x10);
    vm::ptr(buf)[0] = 4;               /* resolutionId 720x480 */
    vm::ptr(buf)[1] = 0;               /* format X8R8G8B8 */
    vm::ptr(buf)[2] = 2;               /* aspect 16:9 */
    vm::write32(buf + 12, 4 * 720);    /* pitch */
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellVideoOutConfigure, 0, buf, 0, 1)) == 0,
          "a valid configuration returns CELL_OK");
    CHECK(hle::sysutil::state().video_resolution == 4 && hle::sysutil::state().video_pitch == 2880,
          "and is stored for GetConfiguration to report");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellVideoOutConfigure, 0, 0, 0, 0)) ==
              static_cast<int32_t>(0x8002b222),
          "a null config is ILLEGAL_PARAMETER");
    vm::write32(buf + 12, 0);
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellVideoOutConfigure, 0, buf, 0, 0)) ==
              static_cast<int32_t>(0x8002b223),
          "a zero pitch is PARAMETER_OUT_OF_RANGE");
    vm::ptr(buf)[0] = 0x92;
    vm::write32(buf + 12, 2880);
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellVideoOutConfigure, 0, buf, 0, 0)) ==
              static_cast<int32_t>(0x8002b221),
          "resolution id 0x92 is ILLEGAL_CONFIGURATION");

    /* --- cellVideoOutGetConfiguration: (videoOut, config, option) ------- */
    hle::sysutil::state() = hle::sysutil::State{};
    hle::sysutil::state().video_resolution = 2;
    hle::sysutil::state().video_pitch = 5120;
    std::memset(vm::ptr(buf), 0xAA, 0x20);
    const uint32_t option = buf + 0x40;
    vm::write32(option, 0xDEADBEEF);
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellVideoOutGetConfiguration, 0, buf, option)) == 0,
          "the primary video configuration returns CELL_OK");
    CHECK(vm::ptr(buf)[0] == 2 && vm::read32(buf + 12) == 5120,
          "and writes the stored resolution and pitch");
    CHECK(vm::read32(option) == 0, "the option structure is zeroed (:251)");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellVideoOutGetConfiguration, 0, 0, 0)) ==
              static_cast<int32_t>(0x8002b222),
          "a null config is ILLEGAL_PARAMETER");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellVideoOutGetConfiguration, 7, buf, 0)) ==
              static_cast<int32_t>(0x8002b225),
          "an unknown videoOut is UNSUPPORTED_VIDEO_OUT");
    std::memset(vm::ptr(buf), 0xAA, 0x20);
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellVideoOutGetConfiguration, 1, buf, 0)) == 0,
          "the secondary returns CELL_OK");
    CHECK(vm::ptr(buf)[0] == 0 && vm::read32(buf + 12) == 0, "with a zeroed configuration");

    /* --- cellVideoOutGetResolutionAvailability -------------------------- */
    hle::sysutil::state() = hle::sysutil::State{};
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellVideoOutGetResolutionAvailability, 0, 2, 0)) == 1,
          "the default 720p mode is available (boolean 1)");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellVideoOutGetResolutionAvailability, 0, 2, 1)) == 0,
          "but not for a 4:3 aspect");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellVideoOutGetResolutionAvailability, 1, 2, 0)) == 0,
          "the secondary answers 0");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellVideoOutGetResolutionAvailability, 3, 2, 0)) ==
              static_cast<int32_t>(0x8002b225),
          "an unknown videoOut is UNSUPPORTED_VIDEO_OUT");

    /* --- cellSysutilGetSystemParamInt: (id, value) ---------------------- */
    std::memset(vm::ptr(buf), 0xAA, 8);
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSysutilGetSystemParamInt, 0x0111, buf)) == 0 &&
              vm::read32(buf) == 1,
          "LANG writes ENGLISH_US (1) into the caller's value");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSysutilGetSystemParamInt, 0x0116, buf)) == 0 &&
              vm::read32(buf) == 180,
          "TIMEZONE writes 180");
    vm::write32(buf, 0x11223344);
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSysutilGetSystemParamInt, 0x9999, buf)) ==
              static_cast<int32_t>(0x8002b102),
          "an unknown id is ERROR_VALUE");
    CHECK(vm::read32(buf) == 0x11223344,
          "and the caller's value is left ALONE: no fabricated zero answer");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSysutilGetSystemParamInt, 0x0111, 0)) ==
              static_cast<int32_t>(0x8002b102),
          "a null value pointer is ERROR_VALUE");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSysutilGetSystemParamInt, 0x0122, buf)) ==
              static_cast<int32_t>(0x8002b102),
          "LICENSE_AREA is refused for a 03.3000 title");

    /* --- cellSysutilGetSystemParamString: (id, buf, size) --------------- */
    std::memset(vm::ptr(buf), 0xAA, 0x100);
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSysutilGetSystemParamString, 0x0113, buf, 0x80)) == 0,
          "the nickname with a 0x80-byte buffer returns CELL_OK");
    CHECK(vm::ptr(buf)[0] != 0xAA, "and writes a non-empty string");
    /* put_str NUL-terminates at the end of the string, not at the end of the
     * caller's buffer -- the bytes after it are the caller's own. */
    CHECK(std::strlen(reinterpret_cast<const char*>(vm::ptr(buf))) < 0x80,
          "the written string is NUL-terminated within the 0x80-byte buffer");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSysutilGetSystemParamString, 0x0113, buf, 0x81)) ==
              static_cast<int32_t>(0x8002b103),
          "a 0x81-byte buffer is ERROR_SIZE: the size must be equal");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSysutilGetSystemParamString, 0x0113, 0, 0x80)) ==
              static_cast<int32_t>(0x8002b102),
          "a null buffer is ERROR_VALUE");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSysutilGetSystemParamString, 0x2222, buf, 0x80)) ==
              static_cast<int32_t>(0x8002b102),
          "an unknown string id is ERROR_VALUE");

    /* --- cellSysutilRegisterCallback / UnregisterCallback --------------- */
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSysutilRegisterCallback, 4, buf, 0)) ==
              static_cast<int32_t>(0x8002b102),
          "a slot past 3 is ERROR_VALUE, not ERROR_TYPE");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSysutilRegisterCallback, 0, buf, 0x1234)) == 0,
          "registering slot 0 returns CELL_OK");
    CHECK(hle::callback_function(0) == buf, "the callback is readable through the module's own accessor");
    CHECK(hle::callback_arg(0) == 0x1234, "and so is its argument");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSysutilUnregisterCallback, 4)) ==
              static_cast<int32_t>(0x8002b102),
          "unregistering slot 4 is ERROR_VALUE");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSysutilUnregisterCallback, 0)) == 0,
          "unregistering slot 0 returns CELL_OK");
    CHECK(hle::callback_function(0) == 0, "and clears the callback");

    /* --- BGM ------------------------------------------------------------ */
    hle::sysutil::state() = hle::sysutil::State{};
    CHECK(hle::sysutil::state().bgm_enable_state == 1, "the BGM status starts DISABLED");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSysutilEnableBgmPlayback)) == 0,
          "EnableBgmPlayback returns CELL_OK");
    CHECK(hle::sysutil::state().bgm_enable_state == 0, "and moves the state to ENABLE");
    std::memset(vm::ptr(buf), 0xAA, 0x40);
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSysutilGetBgmPlaybackStatus, buf)) == 0,
          "GetBgmPlaybackStatus returns CELL_OK");
    CHECK(vm::ptr(buf)[0] == 1, "playerState is at offset 0 and reports STOP");
    CHECK(vm::ptr(buf)[1] == 0, "enableState is at offset 1 and reports ENABLE");
    CHECK(static_cast<uint8_t>(vm::ptr(buf)[0x20]) == 0xAA, "nothing is written past 32 bytes");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSysutilGetBgmPlaybackStatus, 0)) ==
              static_cast<int32_t>(0x8002b101),
          "a null status pointer is BGMPLAYBACK_ERROR_PARAM");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSysutilDisableBgmPlayback)) == 0,
          "DisableBgmPlayback returns CELL_OK");
    CHECK(hle::sysutil::state().bgm_enable_state == 1, "and moves the state back to DISABLE");

    /* --- cellSysCacheMount / Clear ------------------------------------- */
    hle::sysutil::state() = hle::sysutil::State{};
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSysCacheClear)) ==
              static_cast<int32_t>(0x8002bc04),
          "a clear before any mount is NOTMOUNTED");
    std::memset(vm::ptr(buf), 0, 0x500);
    std::memcpy(vm::ptr(buf), "PLAYER", 6);
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSysCacheMount, buf)) == 0,
          "a first mount returns RET_OK_CLEARED (0)");
    CHECK(std::strcmp(reinterpret_cast<const char*>(vm::ptr(buf + 32)), "/dev_hdd1") == 0,
          "getCachePath receives /dev/hdd1, not a path inside the cache (:210)");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSysCacheMount, buf)) == 1,
          "mounting the same cache again returns RET_OK_RELAYED (1)");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSysCacheClear)) == 0,
          "a clear after a mount returns RET_OK_CLEARED");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSysCacheMount, 0)) ==
              static_cast<int32_t>(0x8002bc03),
          "a null param is CELL_SYSCACHE_ERROR_PARAM");
    std::memset(vm::ptr(buf), 0, 0x500);
    std::memcpy(vm::ptr(buf), "lowercase", 10);
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSysCacheMount, buf)) ==
              static_cast<int32_t>(0x8002bc03),
          "an invalid cache id is CELL_SYSCACHE_ERROR_PARAM");

    /* --- progress bars: validated against the OPEN dialog -------------- */
    hle::msgdialog::testhooks::set(false, 0);
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellMsgDialogProgressBarInc, 0, 5)) ==
              static_cast<int32_t>(0x8002b302),
          "a progress-bar increment with no dialog open is DIALOG_NOT_OPENED");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellMsgDialogProgressBarReset, 0)) ==
              static_cast<int32_t>(0x8002b302),
          "as is a reset");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellMsgDialogProgressBarSetMsg, 0, 0)) ==
              static_cast<int32_t>(0x8002b301),
          "a null message is PARAM regardless of the dialog state");
    hle::msgdialog::testhooks::set(true, 0x1000);
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellMsgDialogProgressBarInc, 0, 5)) == 0,
          "with a single-bar dialog open, bar 0 can be incremented");
    CHECK(hle::msgdialog::testhooks::bar(0) == 5.0f, "and the value really moved by the delta");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellMsgDialogProgressBarInc, 1, 5)) ==
              static_cast<int32_t>(0x8002b301),
          "bar 1 does not exist in a single-bar dialog, so it is PARAM");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellMsgDialogProgressBarReset, 0)) == 0,
          "resetting bar 0 succeeds");
    CHECK(hle::msgdialog::testhooks::bar(0) == 0.0f, "and zeroes it");
    std::memcpy(vm::ptr(buf), "progress", 9);
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellMsgDialogProgressBarSetMsg, 0, buf)) == 0,
          "a short message on a valid bar succeeds");

    /* --- OSK ------------------------------------------------------------ */
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellOskDialogAbort)) ==
              static_cast<int32_t>(0x8002b302),
          "abort with no OSK open is DIALOG_NOT_OPENED");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellOskDialogSetDeviceMask, 0xff)) == 0,
          "the PAD device mask is accepted");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellOskDialogSetDeviceMask, 0x100)) ==
              static_cast<int32_t>(0x8002b504),
          "a mask above PAD is CELL_OSKDIALOG_ERROR_PARAM");
    /* cellOskDialogSetKeyLayoutOption lives in runtime/src/hle/cellOskDialog.cpp
     * (it belongs to the OSK module), so its handler is not in this translation
     * unit; its validation is covered by tests/sysutil.cpp, which drives the
     * same sysutil::osk_set_key_layout_option the handler calls. */

    /* --- save-data deletes: an explicit CANCEL, never a false OK -------- */
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSaveDataDelete2, 0)) == 1,
          "cellSaveDataDelete2 answers CELL_CANCEL: nothing was selected, so nothing was deleted");
    CHECK(low32(call(ctx, hle::hle_cellSysutil_cellSaveDataFixedDelete, 0)) == 1,
          "cellSaveDataFixedDelete answers CELL_CANCEL too");
    /* cellSaveDataListAutoLoad lives in cellSaveData.cpp (see the note in the
     * registration list above), so its handler is not in this TU. */

    /* --- the registration names the dispatcher will look up -------------
     * runtime/src/ppu_runtime.cpp:85-87 builds "module::name"; an entry that is
     * missing or misspelt resolves nowhere and the title gets whatever the
     * fallback does. Every import this ticket covers is checked by name. */
    const char* kNames[] = {
        "cellSysutil::cellAudioOutGetState",
        "cellSysutil::cellAudioOutGetConfiguration",
        "cellSysutil::cellAudioOutGetDeviceInfo",
        "cellSysutil::cellAudioOutGetNumberOfDevice",
        "cellSysutil::cellAudioOutGetSoundAvailability",
        "cellSysutil::cellAudioOutConfigure",
        "cellSysutil::cellAudioOutSetCopyControl",
        "cellSysutil::cellVideoOutGetState",
        "cellSysutil::cellVideoOutGetConfiguration",
        "cellSysutil::cellVideoOutGetResolution",
        "cellSysutil::cellVideoOutGetResolutionAvailability",
        "cellSysutil::cellVideoOutConfigure",
        "cellSysutil::cellSysutilGetSystemParamInt",
        "cellSysutil::cellSysutilGetSystemParamString",
        "cellSysutil::cellSysutilRegisterCallback",
        "cellSysutil::cellSysutilUnregisterCallback",
        "cellSysutil::cellSysutilCheckCallback",
        "cellSysutil::cellSysutilGetBgmPlaybackStatus",
        "cellSysutil::cellSysutilEnableBgmPlayback",
        "cellSysutil::cellSysutilDisableBgmPlayback",
        "cellSysutil::cellSysCacheMount",
        "cellSysutil::cellSysCacheClear",
        "cellSysutil::cellMsgDialogProgressBarInc",
        "cellSysutil::cellMsgDialogProgressBarSetMsg",
        "cellSysutil::cellMsgDialogProgressBarReset",
        "cellSysutil::cellOskDialogAbort",
        "cellSysutil::cellOskDialogSetDeviceMask",
        "cellSysutil::cellSaveDataDelete2",
        "cellSysutil::cellSaveDataFixedDelete",
        /* NOT cellSaveDataListAutoLoad: it moved to runtime/src/hle/cellSaveData.cpp,
         * where the save enumeration and the funcFixed delivery are, so it is not
         * registered from this translation unit. tests/savedata.cpp covers that
         * module; the import stays covered because the handler still exists. */
    };
    for (const char* name : kNames) {
        const bool found = hle::test_registry().count(name) != 0;
        checks++;
        if (!found) {
            fails++;
            std::fprintf(stderr, "[FAIL] %s is not registered\n", name);
        }
    }

    std::printf("sysutil_handlers: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
