#include "runtime/host_input_port.h"

#if defined(__wasm__) || defined(__EMSCRIPTEN__)
namespace host {
bool input_focused() { return true; }
bool poll_pad(uint16_t* b) {
    /* No SDL in the browser. Milestone 4 fills the pad from DOM events; until
     * then the guest sees a connected pad with no buttons pressed, which is a
     * valid state the title boots with. */
    if (b) {
        b[0] = b[1] = b[2] = b[3] = 0;
    }
    return true;
}
}  // namespace host
#endif
