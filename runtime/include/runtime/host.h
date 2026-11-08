#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace host {

/* Event-level keyboard input. The window thread reports every SDL key event
 * here, so poll_pad can still see a press that begins and ends inside one poll
 * interval -- which is exactly what "xdotool key" produces. */
void note_key(int scancode, bool down);

#ifndef _WIN32
/* Virtual gamepad, for a machine with no /dev/input (a container, a headless
 * server, CI): SDL 3.2's SDL_AttachVirtualJoystick registers an SDL_Gamepad
 * that every consumer of the normal gamepad path sees, so poll_pad reads it
 * through the same SDL_GetGamepadButton calls a physical pad goes through.
 * attach_virtual_pad is called once from the window thread; the state is then
 * driven with virtual_pad_button / virtual_pad_axis. */
void attach_virtual_pad();
void virtual_pad_button(int sdl_button, bool down);
void virtual_pad_axis(int sdl_axis, int value);
#endif

uint64_t qpc();
uint64_t qpf();
uint64_t thread_cycles();
uint64_t thread_cycles(void* native);

void yield();
void sleep_ms(uint32_t ms);
uint32_t tid();
void set_thread_name(const std::string& name);
std::string thread_name();
void* current_native();
bool native_is_current(void* native);
bool set_affinity(const std::vector<int>& lps);
bool set_priority(int prio);
int last_error();

void wait_on(const void* addr, const void* cmp, size_t size, uint32_t ms);
void wake_all(const void* addr);
void wake_one(const void* addr);

enum class Prot { None, Read, ReadWrite };
void* vm_reserve(size_t size);
bool vm_commit(void* p, size_t size);
bool vm_decommit(void* p, size_t size);
bool vm_protect(void* p, size_t size, Prot prot);

struct Core {
    int efficiency = 0;
    std::vector<int> lps;
};
std::vector<Core> cpu_cores();
int logical_cpus();
void init_timers();

}
