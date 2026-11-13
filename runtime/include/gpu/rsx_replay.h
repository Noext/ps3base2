#pragma once

/* T1 -- THE REPLAY HALF of the WebGPU backend, on the page's main thread.
 *
 * The backend is cut in two, and this file is the seam:
 *
 *   RECORDING  the RSX thread (a pthread, because the build uses
 *              -sPROXY_TO_PTHREAD) classifies every method the command
 *              processor executes and pushes it into the command ring
 *              (runtime/src/gpu/rsx_ring.cpp, producer of S2). It never
 *              touches WebGPU: measured, a pthread's realm has an empty WebGPU
 *              object table, and every port entry is __proxy:'sync' so each
 *              call from there is a blocking round trip. With the renderer
 *              issuing draws from the pthread the FIFO froze at n_method=378
 *              (busy 99.8%, methods 0.1%); with those calls silenced the same
 *              build reached 42 379 draws and 503 327 methods (93.4%, 119k/s).
 *
 *   REPLAY     the page's MAIN thread pops the records and turns them into
 *              WebGPU calls. This is C++ (runtime/src/gpu/rsx_replay.cpp for
 *              the record consumer, runtime/src/gpu/t1_replay_main.cpp for the
 *              exported replay_frame() the page drives from requestAnimationFrame),
 *              not JavaScript: the only JavaScript on this path is the device the
 *              page creates and hands over through Module.preinitializedWebGPUDevice.
 *
 * Why the loop is installed by the PAGE and not by emscripten_set_main_loop:
 * with -sPROXY_TO_PTHREAD the game's main() runs in a pthread, so a main loop
 * registered from there would run on that pthread -- the exact realm that must
 * never touch WebGPU. The page therefore owns the loop (requestAnimationFrame)
 * and calls the exported replay_frame() once per animation frame.
 */
#include <cstdint>

namespace gpu {
namespace replay {

/* Per-thread facts, written by the C++ replay and read by the page. */
struct Stats {
    /* Records popped by this consumer, all kinds. The criterion's "popped". */
    uint64_t popped;
    /* Popped by kind, index = gpu::rsxring::Kind (1..8). */
    uint64_t by_kind[9];
    /* Bytes of record payload consumed (header excluded). */
    uint64_t bytes;
    /* The consumer's own view of what it assembled. */
    uint64_t state_words;
    uint64_t draws_seen;      /* kDraw records */
    uint64_t draws_ready;     /* draws whose inputs it could assemble */
    uint64_t draws_no_shader; /* draws whose programs were not in the store */
    uint64_t draws_no_vertex; /* draws whose vertex data was not in the store */
    uint64_t clears;
    uint64_t flips;
    uint64_t refs;
    uint64_t buffers;
    uint64_t textures;
    uint64_t textures_refused;
    uint64_t texture_bytes;
    uint64_t shaders;
    /* Peak ring occupancy as this consumer saw it, in bytes and as a percent of
     * capacity. Read from the ring's own used word, so it is the producer's
     * number, not an estimate. */
    uint64_t peak_used;
    uint32_t capacity;
    /* Frames the replay ran (one per requestAnimationFrame tick). */
    uint64_t frames;
    /* Calls refused because they arrived on a pthread. MUST stay 0. */
    uint64_t off_thread_calls;
    uint64_t captures;          /* captures taken at the page request */
};

/* Install the device the page created (WGPUDevice as an opaque handle), plus the
 * canvas configuration the page already did. Called from the main thread only;
 * returns false when the handle is null. This call also RECORDS the owner thread
 * every later entry point is checked against. */
bool adopt_device(void* wgpu_device, int color_format, uint32_t width, uint32_t height);

/* One replay step: pop everything the producer has published and apply it. This
 * is the function the page's requestAnimationFrame calls, through
 * t1_replay_frame(). It never blocks and never waits. */
void pump();

/* The counters. */
const Stats& stats();
void reset_stats();

/* The target size the replay draws into. */
uint32_t target_width();
uint32_t target_height();
void set_target_size(uint32_t w, uint32_t h);

}  // namespace replay
}  // namespace gpu
