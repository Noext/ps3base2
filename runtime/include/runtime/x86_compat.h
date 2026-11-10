#pragma once

/* x86-only facilities used throughout the SPU and jobsys paths, in one place.
 *
 * Each of these is real on x86 and meaningless in WebAssembly:
 *   - __rdtsc: no cycle counter (a monotonic clock substitutes);
 *   - _mm_getcsr/_mm_setcsr: no FCSR, rounding is fixed, no FP exception flags;
 *   - __builtin_ia32_readeflags_u32: no EFLAGS register at all.
 *
 * Include this BEFORE <immintrin.h>/<x86intrin.h> and use the gowa_* names, so a
 * file needs one include instead of an #ifdef around every call.
 *
 * Included by: runtime/cycles.h users, spu/*, jobsys/*.
 */
#include <cstdint>

#if defined(__wasm__) || defined(__EMSCRIPTEN__)
#include <bit>

/* No cycle counter: emscripten_get_now is milliseconds as a double and links
 * against the prebuilt libraries, unlike std::chrono. */
#include <emscripten/emscripten.h>
inline uint64_t wasm_rdtsc() { return static_cast<uint64_t>(emscripten_get_now() * 1e6); }
#ifndef __rdtsc
#define __rdtsc wasm_rdtsc
#endif

/* No FCSR. */
inline unsigned ps3b_csr_read() { return 0u; }
inline void ps3b_csr_write(unsigned) {}
#ifndef GOWA_NO_CSR_SHIM
#define _mm_getcsr ps3b_csr_read
#define _mm_setcsr ps3b_csr_write
#endif

/* No EFLAGS. Callers only test the result, so 0 is a consistent answer. */
inline unsigned ps3b_eflags_read() { return 0u; }
#define __builtin_ia32_readeflags_u32 ps3b_eflags_read
#define __builtin_ia32_writeeflags_u32(v) ((void)(v))
#endif
