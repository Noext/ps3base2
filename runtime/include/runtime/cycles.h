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

#if defined(__wasm__) || defined(__EMSCRIPTEN__)
/* __rdtsc appears 81 times across the runtime and headers. Rewriting every site
 * would be churn with no benefit, and would make the native path differ from the
 * ported one. Instead: under wasm, __rdtsc becomes this function, so the same
 * sources build both ways and every call site keeps its meaning (they all
 * subtract two readings). */
uint64_t wasm_rdtsc();
#define __rdtsc wasm_rdtsc
#endif

namespace host {

/* Monotonic, non-decreasing. Units are arbitrary but consistent within a run;
 * callers only ever subtract two values. */
uint64_t cycles();

}  // namespace host
