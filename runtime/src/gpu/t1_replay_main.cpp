/* T1 -- the exported entry points the PAGE drives, once per animation frame.
 *
 * WHY THE LOOP IS NOT INSTALLED HERE
 *
 * The obvious implementation is emscripten_set_main_loop, and it is WRONG for this
 * build. With -sPROXY_TO_PTHREAD the game's main() runs in a pthread, so a main
 * loop registered from inside it is registered on that pthread -- the exact realm
 * that must never touch WebGPU. The measured consequence of getting this wrong is
 * already recorded: FIFO frozen at n_method=378, busy 99.8%, methods 0.1%.
 *
 * So the page owns the loop. web/game.html runs
 *
 *     function tick() { Module._t1_replay_frame(); requestAnimationFrame(tick); }
 *
 * and _t1_replay_frame() is this file's replay_frame(). requestAnimationFrame is
 * the browser's own main-thread callback, which is the thread that owns the device.
 *
 * replay_frame() NEVER BLOCKS. It pops what the producer has published and returns.
 * A page that spent its whole frame here would starve the animation callback, so
 * the consumer's own budget bounds the work (see rsx_replay.cpp).
 */
#include "gpu/rsx_replay.h"

#include "runtime/log.h"
#include "gpu/wgpu_backend.h"

#include <cstdint>
#include <string>

extern "C" {

/* One animation frame of replay. Returns the number of records popped this call,
 * so the page can tell "the ring is empty" from "the replay is not running"
 * without reading the struct. */
uint32_t t1_replay_frame(void) {
    const uint64_t before = gpu::replay::stats().popped;
    gpu::replay::pump();
    return uint32_t(gpu::replay::stats().popped - before);
}

/* The counters, as a pointer to the struct the page reads field by field.
 * A pointer rather than 20 accessors: the page already reads HEAPU32, and one
 * address is one place for the layout to be wrong instead of twenty. */
uint32_t t1_replay_stats(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gpu::replay::stats()));
}

/* Offset of one named field, in bytes. The page asks for the ones it needs by
 * name, so a struct change cannot silently shift a reading. */
uint32_t t1_replay_stat_offset(uint32_t which) {
    const gpu::replay::Stats* s = &gpu::replay::stats();
    const uint8_t* base = reinterpret_cast<const uint8_t*>(s);
    switch (which) {
    case 0: return uint32_t(reinterpret_cast<const uint8_t*>(&s->popped) - base);
    case 1: return uint32_t(reinterpret_cast<const uint8_t*>(&s->bytes) - base);
    case 2: return uint32_t(reinterpret_cast<const uint8_t*>(&s->state_words) - base);
    case 3: return uint32_t(reinterpret_cast<const uint8_t*>(&s->draws_seen) - base);
    case 4: return uint32_t(reinterpret_cast<const uint8_t*>(&s->draws_ready) - base);
    case 5: return uint32_t(reinterpret_cast<const uint8_t*>(&s->draws_no_shader) - base);
    case 6: return uint32_t(reinterpret_cast<const uint8_t*>(&s->draws_no_vertex) - base);
    case 7: return uint32_t(reinterpret_cast<const uint8_t*>(&s->clears) - base);
    case 8: return uint32_t(reinterpret_cast<const uint8_t*>(&s->flips) - base);
    case 9: return uint32_t(reinterpret_cast<const uint8_t*>(&s->refs) - base);
    case 10: return uint32_t(reinterpret_cast<const uint8_t*>(&s->buffers) - base);
    case 11: return uint32_t(reinterpret_cast<const uint8_t*>(&s->textures) - base);
    case 12: return uint32_t(reinterpret_cast<const uint8_t*>(&s->shaders) - base);
    case 13: return uint32_t(reinterpret_cast<const uint8_t*>(&s->peak_used) - base);
    case 14: return uint32_t(reinterpret_cast<const uint8_t*>(&s->capacity) - base);
    case 15: return uint32_t(reinterpret_cast<const uint8_t*>(&s->frames) - base);
    case 16: return uint32_t(reinterpret_cast<const uint8_t*>(&s->off_thread_calls) - base);
    case 17: return uint32_t(reinterpret_cast<const uint8_t*>(&s->textures_refused) - base);
    case 18: return uint32_t(reinterpret_cast<const uint8_t*>(&s->texture_bytes) - base);
    default: return 0xFFFFFFFFu;
    }
}

/* The device handle, obtained from the PORT itself.
 *
 * The page cannot hand it over: s1_adopt_device() is a JS library entry the wasm
 * imports, so it is NOT a property of the Module object the page sees. Measured:
 * "mod._s1_adopt_device" is undefined, and the page passed 0 -- the replay then
 * reported "adopt_device: null device" and never started.
 *
 * So the C++ side asks the port directly. s1_adopt_device is declared in
 * wgpu_backend.cpp and imports the page's device into the port's object table,
 * returning the handle the backend itself uses. */
extern uint32_t s1_adopt_device(void);

/* Adopt the device the page created. Called ONCE, from the page's own thread,
 * before the first replay_frame(). The thread this runs on becomes the owner every
 * later entry point is checked against. */
uint32_t t1_replay_adopt(uint32_t width, uint32_t height) {
    const uint32_t handle = s1_adopt_device();
    if (!handle) {
        LOG_ERROR("replay", "t1_replay_adopt: the port returned no device handle");
        return 0u;
    }
    void* dev = reinterpret_cast<void*>(uintptr_t(handle));
    return gpu::replay::adopt_device(dev, 0, width, height) ? 1u : 0u;
}



/* Set the bisection mode (?dbg=N on the page). 0 = the game's own draws. */
void t1_set_debug_mode(uint32_t mode) { gpu::wgpu::set_debug_mode(mode); }

/* The number of validation errors the per-frame scope has seen. "0 errors" is
 * this number, not an absence of instrumentation. */
uint32_t t1_frame_error_count(void) { return gpu::wgpu::frame_error_count(); }

/* Reset the counters, keeping popped so a rate measurement stays monotonic. */
void t1_replay_reset(void) { gpu::replay::reset_stats(); }

/* 1 when the device was adopted and the replay may run. */
uint32_t t1_replay_ready(void) {
    return gpu::replay::stats().capacity != 0 ? 1u : 0u;
}

}  // extern "C"
