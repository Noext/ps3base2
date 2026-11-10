#pragma once

/* Stack capture, portably.
 *
 * backtrace(3) comes from <execinfo.h>, which does not exist in WebAssembly: a
 * wasm stack is not walkable the way an x86 one is, and the browser exposes no
 * equivalent. The crash reporter and the codewatch both use it to name the caller
 * of a write, which is diagnostic value, not correctness.
 *
 * Native: real backtrace.
 * Wasm:   reports zero frames. Every caller already handles n == 0, and the crash
 *         reporter is not installed under wasm anyway (no signals).
 */
#include <cstddef>

namespace backtrace_port {

/* Fills up to `depth` program counters, returns how many were written. */
int capture(void** pcs, int depth);

}  // namespace backtrace_port
