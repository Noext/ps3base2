#pragma once

/* T1 -- THE REPLAY HALF of the WebGPU backend, on the page's main thread.
 *
 * The backend is cut in two, and this file is the seam:
 *
 *   RECORDING  the RSX thread (a pthread, because the build uses
 *              -sPROXY_TO_PTHREAD) classifies every method the command
 *              processor executes and pushes it into the command ring
 *              (runtime/src/gpu/rsx_ring.cpp, producer of S2). It never
 *              touches WebGPU: measured by S1, a pthread's realm has an empty
 *              WebGPU object table, so every device call from there returns
 *              undefined.
 *
 *   REPLAY     the page's MAIN thread pops the records and turns them into
 *              WebGPU calls. This is C++ (runtime/src/gpu/rsx_replay.cpp for
 *              the record consumer, runtime/src/gpu/t1_replay_main.cpp for the
 *              emscripten_set_main_loop that drives it), not JavaScript: the
 *              only JavaScript on this path is the device the page creates and
 *              hands over through Module.preinitializedWebGPUDevice.
 *
 * Why a replay of the RECORD STREAM rather than S1's in-renderer mirror: S1
 * called wgpu::draw() from inside the renderer's draw(), which runs on the
 * pthread. Its own results say what that costs (S1 RESULTS sec. 2.3, and the
 * proxy annotations in web/s1-bridge.js that were needed to make a pthread
 * paint at all). Here nothing on the WebGPU path runs off the main thread, so
 * no proxy is needed and the pthread can never block on the browser.
 *
 * What the replay reproduces, and what it does not. The renderer's decode --
 * vertex expansion, index expansion, texture decode, shader decompile, uniform
 * assembly -- is what turns RSX registers into the inputs a draw needs, and it
 * lives in rsx_renderer.cpp on the pthread. The replay therefore does the part
 * that needs the device (pipelines, buffers, textures, bind groups, passes)
 * from the state the ring carries, and it says so in its counters instead of
 * pretending: draws_ready counts the draws whose inputs it could assemble.
 */
#include <cstdint>

namespace gpu {
namespace replay {

/* Per-thread facts, written by the C++ main loop and read by the page. */
struct Stats {
    /* Records popped by this consumer, all kinds. The criterion's "popped". */
    uint64_t popped;
    /* Popped by kind, index = gpu::rsxring::Kind (1..8). */
    uint64_t by_kind[9];
    /* Bytes of record payload consumed (header excluded). */
    uint64_t bytes;
    /* The consumer's own view of what it assembled. */
    uint64_t state_words;     /* StateWord pairs applied to the register file */
    uint64_t draws_seen;      /* kDraw records */
    uint64_t draws_ready;     /* draws whose vertex data was in the store */
    uint64_t clears;
    uint64_t flips;
    uint64_t refs;
    uint64_t textures_complete;  /* texture objects reassembled from chunks */
    uint64_t shaders;            /* ucode images received */
    /* Peak ring occupancy as this consumer saw it, in bytes and as a percent of
     * capacity. Read from the ring's own used word, so it is the producer's
     * number, not an estimate. */
    uint64_t peak_used;
    uint32_t capacity;
    /* The device the replay drives: nonzero once the main thread adopted it. */
    uint32_t device_ready;
    /* Frames the C++ main loop ran (one per emscripten_set_main_loop tick). */
    uint64_t frames;
    /* WebGPU objects the replay created, so "it ran" is distinguishable from
     * "it ran and built nothing". */
    uint64_t pipelines;
    uint64_t buffers;
    uint64_t texture_uploads;
    uint64_t bind_groups;
    uint64_t passes;
    uint64_t submits;
    /* The first WebGPU validation error seen, empty when none. */
    const char* first_error;
};

/* Install the device the page created (WGPUDevice as an opaque handle), plus the
 * canvas configuration the page already did. Called from the main thread only;
 * returns false when the handle is null. */
bool adopt_device(void* wgpu_device, int color_format, uint32_t width, uint32_t height);

/* One replay step: pop everything the producer has published and apply it. This
 * is the function emscripten_set_main_loop calls, through t1_replay_frame(). */
void pump();

/* The counters. */
const Stats& stats();
void reset_stats();

}  // namespace replay
}  // namespace gpu
