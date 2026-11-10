#include "runtime/backtrace_port.h"

#if defined(__wasm__) || defined(__EMSCRIPTEN__)
namespace backtrace_port {
int capture(void** pcs, int depth) {
    /* No stack walking in wasm. Zero frames is a truthful answer: callers log
     * that they have no trace rather than a fabricated one. */
    (void)pcs;
    (void)depth;
    return 0;
}
}  // namespace backtrace_port
#else
#include <execinfo.h>

namespace backtrace_port {
int capture(void** pcs, int depth) { return ::backtrace(pcs, depth); }
}  // namespace backtrace_port
#endif
