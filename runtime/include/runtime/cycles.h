#pragma once

/* Cycle counter, portably.
 *
 * __rdtsc appears 81 times in this runtime, all for timing: critpath windows,
 * job scheduling measurements, the SPU AOT timing, the codewatch.
 *
 * It is x86-only. WebAssembly has no cycle counter, so a browser build must fall
 * back to a monotonic clock -- the numbers keep their meaning as *relative*
 * timing, which is all any of these call sites use them for. Nothing here
 * compares a cycle count against an absolute constant.
 *
 * Found by trying to build reservation.cpp for wasm, which fails on _tzcnt_u64
 * and would fail on __rdtsc next. Better to have one place that knows.
 */
#include <cstdint>

namespace host {

/* Monotonic, non-decreasing. Units are arbitrary but consistent within a run;
 * callers only ever subtract two values. */
uint64_t cycles();

}  // namespace host
