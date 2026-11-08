#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xinput.h>
#else
#include <SDL3/SDL.h>
#endif

#include "runtime/host.h"
#include "runtime/log.h"

namespace host {

namespace {

void press(uint16_t* b, uint32_t offset, uint16_t mask, uint32_t pressure) {
    b[offset] |= mask;
    if (pressure) b[pressure] = 0xFF;
}

void stick(int sx, int sy, int deadzone, uint16_t& ox, uint16_t& oy) {
    float x = static_cast<float>(sx), y = static_cast<float>(sy);
    float mag = std::sqrt(x * x + y * y);
    if (mag <= deadzone) {
        ox = oy = 128;
        return;
    }
    float scale = (std::min(mag, 32767.0f) - deadzone) / (32767.0f - deadzone) / mag;
    x *= scale;
    y *= scale;
    ox = static_cast<uint16_t>(std::clamp(static_cast<int>(std::lround(128.0f + x * 127.0f)), 0, 255));
    oy = static_cast<uint16_t>(std::clamp(static_cast<int>(std::lround(128.0f - y * 127.0f)), 0, 255));
}

#ifdef _WIN32

using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

XInputGetStateFn g_xinput = nullptr;
std::once_flag g_once;

void load() {
    const wchar_t* names[] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
    for (const wchar_t* n : names) {
        if (HMODULE m = LoadLibraryW(n)) {
            g_xinput = reinterpret_cast<XInputGetStateFn>(GetProcAddress(m, "XInputGetState"));
            if (g_xinput) return;
        }
    }
}

bool focused() {
    HWND w = GetForegroundWindow();
    if (!w) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

bool key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

int g_slot = -1;
ULONGLONG g_next_scan = 0;
#endif

}

#ifdef _WIN32

void poll_pad(uint16_t* b) {
    std::call_once(g_once, load);
    if (g_xinput) {
        XINPUT_STATE st{};
        bool have = g_slot >= 0 && g_xinput(static_cast<DWORD>(g_slot), &st) == ERROR_SUCCESS;
        if (!have && GetTickCount64() >= g_next_scan) {
            g_slot = -1;
            for (DWORD i = 0; i < 4 && !have; i++) {
                if (g_xinput(i, &st) == ERROR_SUCCESS) {
                    g_slot = static_cast<int>(i);
                    have = true;
                }
            }
            if (!have) g_next_scan = GetTickCount64() + 1000;
        }
        if (have) {
            WORD w = st.Gamepad.wButtons;
            if (w & XINPUT_GAMEPAD_BACK) press(b, 2, 0x01, 0);
            if (w & XINPUT_GAMEPAD_LEFT_THUMB) press(b, 2, 0x02, 0);
            if (w & XINPUT_GAMEPAD_RIGHT_THUMB) press(b, 2, 0x04, 0);
            if (w & XINPUT_GAMEPAD_START) press(b, 2, 0x08, 0);
            if (w & XINPUT_GAMEPAD_DPAD_UP) press(b, 2, 0x10, 10);
            if (w & XINPUT_GAMEPAD_DPAD_RIGHT) press(b, 2, 0x20, 8);
            if (w & XINPUT_GAMEPAD_DPAD_DOWN) press(b, 2, 0x40, 11);
            if (w & XINPUT_GAMEPAD_DPAD_LEFT) press(b, 2, 0x80, 9);
            if (st.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
                b[3] |= 0x01;
                b[18] = st.Gamepad.bLeftTrigger;
            }
            if (st.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
                b[3] |= 0x02;
                b[19] = st.Gamepad.bRightTrigger;
            }
            if (w & XINPUT_GAMEPAD_LEFT_SHOULDER) press(b, 3, 0x04, 16);
            if (w & XINPUT_GAMEPAD_RIGHT_SHOULDER) press(b, 3, 0x08, 17);
            if (w & XINPUT_GAMEPAD_Y) press(b, 3, 0x10, 12);
            if (w & XINPUT_GAMEPAD_B) press(b, 3, 0x20, 13);
            if (w & XINPUT_GAMEPAD_A) press(b, 3, 0x40, 14);
            if (w & XINPUT_GAMEPAD_X) press(b, 3, 0x80, 15);
            stick(st.Gamepad.sThumbRX, st.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE, b[4], b[5]);
            stick(st.Gamepad.sThumbLX, st.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, b[6], b[7]);
        }
    }
    if (!focused()) return;
    if (key(VK_BACK)) press(b, 2, 0x01, 0);
    if (key(VK_RETURN)) press(b, 2, 0x08, 0);
    if (key(VK_UP)) press(b, 2, 0x10, 10);
    if (key(VK_RIGHT)) press(b, 2, 0x20, 8);
    if (key(VK_DOWN)) press(b, 2, 0x40, 11);
    if (key(VK_LEFT)) press(b, 2, 0x80, 9);
    if (key('1')) press(b, 3, 0x01, 18);
    if (key('3')) press(b, 3, 0x02, 19);
    if (key('Q')) press(b, 3, 0x04, 16);
    if (key('E')) press(b, 3, 0x08, 17);
    if (key('S')) press(b, 3, 0x10, 12);
    if (key('X')) press(b, 3, 0x20, 13);
    if (key('Z') || key(VK_SPACE)) press(b, 3, 0x40, 14);
    if (key('A')) press(b, 3, 0x80, 15);
    if (key('J')) b[6] = 0;
    if (key('L')) b[6] = 255;
    if (key('I')) b[7] = 0;
    if (key('K')) b[7] = 255;
}

#else

namespace {

/* Event-edge latch. "xdotool key" emits press and release in the same
 * millisecond, so a 60 Hz poll of SDL_GetKeyboardState never observes it and
 * the press is silently dropped -- which is why the menus appeared to eat
 * random presses. Every key event the window thread sees is latched here for a
 * minimum number of poll frames, so an instantaneous tap still reads as held. */
constexpr int kLatchFrames = 5;
std::atomic<int> g_latch[SDL_SCANCODE_COUNT];

const bool* keys() {
    int n = 0;
    return SDL_GetKeyboardState(&n);
}

bool key(SDL_Scancode sc) {
    const bool* k = keys();
    if (k && k[sc]) return true;
    const int latched = g_latch[sc].load(std::memory_order_relaxed);
    if (latched <= 0) return false;
    g_latch[sc].store(latched - 1, std::memory_order_relaxed);
    return true;
}

SDL_Gamepad* pad() {
    int n = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&n);
    SDL_Gamepad* p = nullptr;
    for (int i = 0; i < n && !p; i++) p = SDL_GetGamepadFromID(ids[i]);
    SDL_free(ids);
    return p;
}

}

}

namespace gpu {
void* native_window();
}

namespace host {

namespace {

/* Virtual gamepad. A container has no /dev/input and no /dev/uinput, so a real
 * pad can never appear; before this, the gamepad path in poll_pad was dead code
 * there and the pad bytes only ever came from the keyboard. SDL 3.2's
 * SDL_AttachVirtualJoystick registers a device with the joystick subsystem, so
 * SDL_GetGamepads() returns it, SDL_OpenGamepad() opens it, and poll_pad reads
 * it through exactly the same calls a physical pad goes through. The state is
 * written with SDL_SetJoystickVirtualButton/Axis. */
SDL_Joystick* g_vpad = nullptr;

bool input_log() {
    static const bool on = std::getenv("PS3BASE_INPUT_LOG") != nullptr;
    return on;
}

double now_s() { return double(SDL_GetTicks()) / 1000.0; }

}  // namespace

void attach_virtual_pad() {
    if (g_vpad) return;
    if (std::getenv("PS3BASE_NO_VIRTUAL_PAD")) {
        LOG_INFO("input", "virtual gamepad disabled by PS3BASE_NO_VIRTUAL_PAD");
        return;
    }
    /* Only when no physical pad is present: a real controller must win. */
    int have = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&have);
    bool physical = false;
    for (int i = 0; i < have; i++) {
        if (SDL_IsJoystickVirtual(ids[i])) continue;
        if (SDL_GetGamepadFromID(ids[i]) || SDL_OpenGamepad(ids[i])) physical = true;
    }
    SDL_free(ids);
    if (physical) {
        LOG_INFO("input", "physical gamepad present, not attaching a virtual one");
        return;
    }
    SDL_VirtualJoystickDesc d;
    SDL_INIT_INTERFACE(&d);
    d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    d.naxes = SDL_GAMEPAD_AXIS_COUNT;
    d.vendor_id = 0x054C;   /* Sony, so a title's own pad table recognises it */
    d.product_id = 0x0268;
    d.name = "ps3base virtual pad";
    d.button_mask = 0xFFFFFFFFu;
    d.axis_mask = 0xFFFFFFFFu;
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&d);
    if (!id) {
        LOG_WARN("input", "SDL_AttachVirtualJoystick failed: %s", SDL_GetError());
        return;
    }
    g_vpad = SDL_OpenJoystick(id);
    if (!g_vpad) LOG_WARN("input", "virtual pad attached but SDL_OpenJoystick failed: %s", SDL_GetError());
    else LOG_INFO("input", "virtual gamepad attached: %s (id %u)", SDL_GetJoystickName(g_vpad), unsigned(id));
    if (!g_vpad) return;
    /* Centre the sticks and release the triggers, so the first poll is neutral
     * instead of whatever a zeroed descriptor reports. Goes through
     * virtual_pad_axis because a raw trigger axis is not 0 at rest. */
    for (int a : {SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_RIGHTX,
                  SDL_GAMEPAD_AXIS_RIGHTY, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER})
        virtual_pad_axis(a, 0);
    SDL_UpdateJoysticks();
}

void virtual_pad_button(int sdl_button, bool down) {
    if (!g_vpad || sdl_button < 0 || sdl_button >= SDL_GAMEPAD_BUTTON_COUNT) return;
    SDL_SetJoystickVirtualButton(g_vpad, sdl_button, down);
    SDL_UpdateJoysticks();
    if (input_log()) LOG_INFO("input", "vpad button %d %s t=%.3f", sdl_button, down ? "down" : "up", now_s());
}

/* Scripted pad input, through the virtual pad rather than through fill().
 * "name@start:hold,..." -- same shape as GOWA_AUTOPAD, but every press is an
 * SDL button/axis on a real SDL gamepad, so this exercises the same path a
 * human's controller takes: SDL state -> SDL_GetGamepadButton -> pad bytes.
 * It is the only way to drive the pad on a machine with no /dev/input, and it
 * is also the regression test for the virtual pad itself. */
struct VPress {
    int button = -1;      /* SDL_GAMEPAD_BUTTON_* */
    int axis = -1;        /* SDL_GAMEPAD_AXIS_* */
    int value = 0;
    double at = 0, hold = 0;
};

std::vector<VPress> g_vscript;
bool g_vscript_loaded = false;
bool g_vscript_started = false;
double g_vscript_t0 = 0;

void load_vscript() {
    g_vscript_loaded = true;
    const char* env = std::getenv("PS3BASE_VPAD_SCRIPT");
    if (!env) return;
    struct B { const char* n; int b; };
    static const B btns[] = {
        {"cross", SDL_GAMEPAD_BUTTON_SOUTH}, {"circle", SDL_GAMEPAD_BUTTON_EAST},
        {"square", SDL_GAMEPAD_BUTTON_WEST}, {"triangle", SDL_GAMEPAD_BUTTON_NORTH},
        {"up", SDL_GAMEPAD_BUTTON_DPAD_UP}, {"down", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
        {"left", SDL_GAMEPAD_BUTTON_DPAD_LEFT}, {"right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
        {"l1", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}, {"r1", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
        {"select", SDL_GAMEPAD_BUTTON_BACK}, {"start", SDL_GAMEPAD_BUTTON_START},
        {"l3", SDL_GAMEPAD_BUTTON_LEFT_STICK}, {"r3", SDL_GAMEPAD_BUTTON_RIGHT_STICK},
    };
    struct A { const char* n; int a; int v; };
    static const A axes[] = {
        {"lx-", SDL_GAMEPAD_AXIS_LEFTX, -32767}, {"lx+", SDL_GAMEPAD_AXIS_LEFTX, 32767},
        {"ly-", SDL_GAMEPAD_AXIS_LEFTY, -32767}, {"ly+", SDL_GAMEPAD_AXIS_LEFTY, 32767},
        {"rx-", SDL_GAMEPAD_AXIS_RIGHTX, -32767}, {"rx+", SDL_GAMEPAD_AXIS_RIGHTX, 32767},
        {"ry-", SDL_GAMEPAD_AXIS_RIGHTY, -32767}, {"ry+", SDL_GAMEPAD_AXIS_RIGHTY, 32767},
        /* L2/R2 are analogue triggers, not buttons: SDL exposes them only as
         * axes, and poll_pad reads them as axes too (press bit at >30/255). */
        {"l2", SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767}, {"r2", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767},
    };
    std::string s = env;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t end = s.find(',', pos);
        if (end == std::string::npos) end = s.size();
        std::string item = s.substr(pos, end - pos);
        pos = end + 1;
        const size_t at = item.find('@');
        if (at == std::string::npos) continue;
        const std::string name = item.substr(0, at);
        const std::string when = item.substr(at + 1);
        const size_t colon = when.find(':');
        VPress p;
        p.at = std::atof(when.substr(0, colon).c_str());
        p.hold = colon == std::string::npos ? 0.25 : std::atof(when.substr(colon + 1).c_str());
        bool ok = false;
        for (const B& b : btns) if (name == b.n) { p.button = b.b; ok = true; }
        for (const A& a : axes) if (name == a.n) { p.axis = a.a; p.value = a.v; ok = true; }
        if (!ok) { LOG_WARN("input", "vpad script: unknown control '%s'", name.c_str()); continue; }
        g_vscript.push_back(p);
        LOG_INFO("input", "vpad script: %s at %.2fs for %.2fs", name.c_str(), p.at, p.hold);
    }
}

void virtual_pad_tick() {
    if (!g_vpad) return;
    if (!g_vscript_loaded) load_vscript();
    if (g_vscript.empty()) return;
    /* The clock starts at the first tick, so a script's times are relative to
     * the moment the pad became usable rather than to process start. */
    if (!g_vscript_started) { g_vscript_started = true; g_vscript_t0 = now_s(); }
    const double t = now_s() - g_vscript_t0;
    static std::vector<bool> held(g_vscript.size(), false);
    if (held.size() != g_vscript.size()) held.assign(g_vscript.size(), false);
    for (size_t i = 0; i < g_vscript.size(); i++) {
        const VPress& p = g_vscript[i];
        const bool want = t >= p.at && t < p.at + p.hold;
        if (want == held[i]) continue;
        held[i] = want;
        if (p.button >= 0) virtual_pad_button(p.button, want);
        if (p.axis >= 0) virtual_pad_axis(p.axis, want ? p.value : 0);
    }
}

void virtual_pad_axis(int sdl_axis, int value) {
    if (!g_vpad || sdl_axis < 0 || sdl_axis >= SDL_GAMEPAD_AXIS_COUNT) return;
    /* SDL_SetJoystickVirtualAxis writes RAW joystick axes, not gamepad axes.
     * The two agree for the sticks (both -32767..32767) but not for the
     * triggers: a gamepad trigger is 0..32767 with 0 at rest, while a raw
     * trigger axis is -32768..32767 with SDL_JOYSTICK_AXIS_MIN at rest
     * (SDL_joystick.h: "a trigger at rest would have the value of
     * SDL_JOYSTICK_AXIS_MIN"). Writing 0 for "released" therefore lands the
     * trigger halfway down, SDL_GetGamepadAxis returns ~16384, the >30/255
     * threshold in poll_pad fires, and L2+R2 read as permanently held
     * (measured: b[3]=0x03 with no key and no button pressed). Convert. */
    const bool trigger = sdl_axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || sdl_axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
    int raw;
    if (value < -32767) value = -32767;
    if (value > 32767) value = 32767;
    if (trigger) raw = value <= 0 ? SDL_JOYSTICK_AXIS_MIN : (value * 2 - 32768);
    else raw = value;
    if (raw < -32768) raw = -32768;
    SDL_SetJoystickVirtualAxis(g_vpad, sdl_axis, static_cast<Sint16>(raw));
    SDL_UpdateJoysticks();
    if (input_log()) LOG_INFO("input", "vpad axis %d = %d (raw %d) t=%.3f", sdl_axis, value, raw, now_s());
}

void note_key(int scancode, bool down) {
    if (scancode < 0 || scancode >= SDL_SCANCODE_COUNT) return;
    if (input_log() && down) LOG_INFO("input", "key down scancode %d (%s) t=%.3f", scancode, SDL_GetScancodeName(static_cast<SDL_Scancode>(scancode)), now_s());
    if (!down) return;   /* a release never extends the latch */
    g_latch[scancode].store(kLatchFrames, std::memory_order_relaxed);
}

bool allow_unfocused() {
    static const bool on = [] {
        const char* e = std::getenv("PS3BASE_ALLOW_UNFOCUSED_INPUT");
        return e && e[0] == '1';
    }();
    return on;
}

void poll_pad(uint16_t* b) {
    void* w = gpu::native_window();
    if (!w) return;
    /* Drive the scripted virtual pad before reading it: SDL applies virtual
     * button/axis writes on SDL_UpdateJoysticks, so the state has to be pushed
     * before SDL_GetGamepadButton in the same frame sees it. */
    virtual_pad_tick();
    if (SDL_Gamepad* p = pad()) {
        auto btn = [&](SDL_GamepadButton x) { return SDL_GetGamepadButton(p, x); };
        if (btn(SDL_GAMEPAD_BUTTON_BACK)) press(b, 2, 0x01, 0);
        if (btn(SDL_GAMEPAD_BUTTON_LEFT_STICK)) press(b, 2, 0x02, 0);
        if (btn(SDL_GAMEPAD_BUTTON_RIGHT_STICK)) press(b, 2, 0x04, 0);
        if (btn(SDL_GAMEPAD_BUTTON_START)) press(b, 2, 0x08, 0);
        if (btn(SDL_GAMEPAD_BUTTON_DPAD_UP)) press(b, 2, 0x10, 10);
        if (btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) press(b, 2, 0x20, 8);
        if (btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) press(b, 2, 0x40, 11);
        if (btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) press(b, 2, 0x80, 9);
        int lt = SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) >> 7, rt = SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) >> 7;
        if (lt > 30) {
            b[3] |= 0x01;
            b[18] = static_cast<uint16_t>(std::min(lt, 255));
        }
        if (rt > 30) {
            b[3] |= 0x02;
            b[19] = static_cast<uint16_t>(std::min(rt, 255));
        }
        if (btn(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) press(b, 3, 0x04, 16);
        if (btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) press(b, 3, 0x08, 17);
        if (btn(SDL_GAMEPAD_BUTTON_NORTH)) press(b, 3, 0x10, 12);
        if (btn(SDL_GAMEPAD_BUTTON_EAST)) press(b, 3, 0x20, 13);
        if (btn(SDL_GAMEPAD_BUTTON_SOUTH)) press(b, 3, 0x40, 14);
        if (btn(SDL_GAMEPAD_BUTTON_WEST)) press(b, 3, 0x80, 15);
        auto axis = [&](SDL_GamepadAxis a) { return std::clamp(static_cast<int>(SDL_GetGamepadAxis(p, a)), -32767, 32767); };
        stick(axis(SDL_GAMEPAD_AXIS_RIGHTX), -axis(SDL_GAMEPAD_AXIS_RIGHTY), 8689, b[4], b[5]);
        stick(axis(SDL_GAMEPAD_AXIS_LEFTX), -axis(SDL_GAMEPAD_AXIS_LEFTY), 7849, b[6], b[7]);
    }
    /* Without a window manager the X input focus is PointerRoot, so it moves with
     * the pointer and any second instance on the same display steals it. The
     * escape hatch makes the keyboard path usable on a bare server; on a real
     * desktop it stays off so background windows do not drive the game. */
    if (!allow_unfocused() && SDL_GetKeyboardFocus() != static_cast<SDL_Window*>(w)) return;
    if (key(SDL_SCANCODE_BACKSPACE)) press(b, 2, 0x01, 0);
    if (key(SDL_SCANCODE_RETURN)) press(b, 2, 0x08, 0);
    if (key(SDL_SCANCODE_UP)) press(b, 2, 0x10, 10);
    if (key(SDL_SCANCODE_RIGHT)) press(b, 2, 0x20, 8);
    if (key(SDL_SCANCODE_DOWN)) press(b, 2, 0x40, 11);
    if (key(SDL_SCANCODE_LEFT)) press(b, 2, 0x80, 9);
    if (key(SDL_SCANCODE_1)) press(b, 3, 0x01, 18);
    if (key(SDL_SCANCODE_3)) press(b, 3, 0x02, 19);
    if (key(SDL_SCANCODE_Q)) press(b, 3, 0x04, 16);
    if (key(SDL_SCANCODE_E)) press(b, 3, 0x08, 17);
    if (key(SDL_SCANCODE_S)) press(b, 3, 0x10, 12);
    if (key(SDL_SCANCODE_X)) press(b, 3, 0x20, 13);
    if (key(SDL_SCANCODE_Z) || key(SDL_SCANCODE_SPACE)) press(b, 3, 0x40, 14);
    if (key(SDL_SCANCODE_A)) press(b, 3, 0x80, 15);
    if (key(SDL_SCANCODE_J)) b[6] = 0;
    if (key(SDL_SCANCODE_L)) b[6] = 255;
    if (key(SDL_SCANCODE_I)) b[7] = 0;
    if (key(SDL_SCANCODE_K)) b[7] = 255;
    /* Right stick: the in-game look control had no keyboard binding at all, so
     * the camera could not be aimed and break/place had no target. */
    if (key(SDL_SCANCODE_R)) b[4] = 255;
    if (key(SDL_SCANCODE_F)) b[4] = 0;
    if (key(SDL_SCANCODE_T)) b[5] = 0;
    if (key(SDL_SCANCODE_G)) b[5] = 255;
}

#endif

}
