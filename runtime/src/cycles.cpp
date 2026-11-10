#include "runtime/cycles.h"

#include <chrono>

#if defined(__wasm__) || defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>

uint64_t wasm_rdtsc() {
    /* No cycle counter in the browser. `emscripten_get_now()` is milliseconds as
     * a double and needs no libc++ chrono instantiation, which keeps this file
     * linkable against Emscripten's prebuilt libraries. Callers only subtract two
     * readings, so the unit does not matter -- only monotonicity does. */
    return static_cast<uint64_t>(emscripten_get_now() * 1e6);
}

namespace host {
uint64_t cycles() { return wasm_rdtsc(); }
}  // namespace host
#elif defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
#include <x86intrin.h>
namespace host {
uint64_t cycles() { return __rdtsc(); }
}  // namespace host
#else
namespace host {
uint64_t cycles() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
}  // namespace host
#endif
