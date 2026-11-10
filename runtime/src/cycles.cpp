#include "runtime/cycles.h"

#include <chrono>

#if defined(__wasm__) || defined(__EMSCRIPTEN__)
namespace host {
uint64_t cycles() {
    /* No cycle counter in the browser. A steady clock keeps every existing use
     * (differences, windows, per-call cost) meaningful. */
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
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
