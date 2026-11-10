#pragma once

/* Host input, portably.
 *
 * The native path reads SDL3: window focus, keyboard state, gamepads, a virtual
 * joystick. Under wasm, input arrives from the browser event loop instead, so the
 * SDL dependency is replaced by a stub that reports "no keys down". The page
 * layer (milestone 4) feeds the guest pad from DOM events directly.
 */
#include <cstdint>

namespace host {

/* True when the emulator window has focus and may take keyboard input. */
bool input_focused();
/* Fills the 4 pad bytes. Matches host_input.cpp's signature exactly -- a
 * mismatched return type is a wasm signature_mismatch trap at the call site, which
 * is how this was found (host::poll_pad, called from cellPadGetData). */
void poll_pad(uint16_t* buttons);

}  // namespace host
