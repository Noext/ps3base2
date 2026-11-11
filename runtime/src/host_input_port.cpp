#include "runtime/host_input_port.h"

#if defined(__wasm__) || defined(__EMSCRIPTEN__)
extern "C" {
extern uint16_t g_web_pad_words[4];
extern uint32_t g_web_pad_connected;
}

namespace host {
bool input_focused() { return true; }
void poll_pad(uint16_t* b) {
    /* No SDL in the browser, but the page has DOM events and the Gamepad API.
     * It publishes the current button words into this block before each poll;
     * the layout is the same four uint16 words cellPadGetData expects, so no
     * translation happens on this side.
     *
     * The block is written by JavaScript through HEAPU16. It is read here without
     * a lock on purpose: a torn read of a button word is at worst a single frame
     * with a wrong button, and taking a lock in the pad poll would let the page
     * stall the emulation thread.
     *
     * Until the page publishes anything the block stays zero, which is a
     * connected pad with nothing pressed -- a state the title boots with. */
    if (!b) return;
    if (!g_web_pad_connected) {
        b[0] = b[1] = b[2] = b[3] = 0;
        return;
    }
    b[0] = g_web_pad_words[0];
    b[1] = g_web_pad_words[1];
    b[2] = g_web_pad_words[2];
    b[3] = g_web_pad_words[3];
}
}  // namespace host
#endif
