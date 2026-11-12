#pragma once

#include <cstdint>
#include <thread>

/* The console's start rule: a thread created with a HIGHER priority than its
 * creator runs first, until its first blocking call.
 *
 * The specification is RPCS3's, because that is what the console does:
 *
 *   refs/rpcs3/rpcs3/Emu/Cell/PPUThread.h:287
 *     bool is_lower_priority_than(ppu_thread& other) const
 *     { return prio.load().prio < other.prio.load().prio; }
 *   -- a SMALLER number is a HIGHER priority (0 highest, 3071 lowest, which
 *      refs/jdc-ps3recomp/docs/SYSCALLS.md:112 states independently).
 *
 *   refs/rpcs3/rpcs3/Emu/Cell/lv2/sys_ppu_thread.cpp:578-622
 *     bool is_lower_prio = thread.is_lower_priority_than(ppu);
 *     ...
 *     if (is_lower_prio) { wait until the new thread has started and stopped
 *                          running (blocked or preempted), bounded by 5000 us }
 *
 * The bound is RPCS3's: `get_system_time() - start < 5000`, and timers.hpp:11
 * documents get_system_time as microseconds. Without a bound, a child that
 * never blocks and never exits would park its creator forever; the console
 * hands the CPU back after that window, so this does too.
 *
 * Behind an option ("advanced.ppu_start_gate" / PS3BASE_PPU_START_GATE, on by
 * default) because it changes thread ordering for every title, and a title that
 * relies on the old "child races its creator" order must be able to turn it off
 * without rebuilding.
 */
namespace start_gate {

/* Whether the rule is on. The value is read once, like the other runtime
 * options (GOWA_ASYNC_SHADERS, GOWA_FPS_OSD), because it is consulted on a
 * syscall path. */
bool enabled();

/* The decision itself, as a pure function so a test can pin it without the
 * runtime: nullptr means "the variable is unset" and the default is on; only
 * "0" turns it off, which is the convention the other boolean options use. */
bool enabled_value(const char* value);

/* Does a child of `child_prio` start ahead of a creator of `creator_prio`?
 * Yes exactly when the child is strictly higher priority, i.e. its number is
 * smaller. Equal priority does not (RPCS3: is_lower_priority_than is strict). */
bool takes_precedence(int32_t child_prio, int32_t creator_prio);

/* The bound on the wait, in microseconds. Nonzero in the runtime. */
uint64_t budget_us();

/* Monotonic microseconds, the unit the budget is expressed in. */
uint64_t clock_us();

/* Poll `ready` until it answers, yielding between attempts, bounded by
 * `budget_us` (0 means unbounded and exists for the test only). The clock is a
 * parameter so the bound can be exercised without waiting on wall time.
 *
 * This mirrors `sys_ppu_thread_start`'s loop: check, yield, check again, stop
 * at the deadline. Returns true when `ready` answered. */
template <class Ready, class Clock>
bool wait_until_ready(Ready&& ready, Clock&& clock, uint64_t budget_us) {
    const uint64_t start = clock();
    for (;;) {
        if (ready()) return true;
        if (budget_us && clock() - start >= budget_us) return false;
        std::this_thread::yield();
    }
}

}
