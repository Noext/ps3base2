#pragma once

/* The audio ring the AudioWorklet drains.
 *
 * Milestone 4 asks for an AudioWorklet fed by shared memory. The pattern the
 * browser requires: C++ writes into a SharedArrayBuffer, the worklet's
 * process() reads from it on the audio thread and returns silence when the ring
 * starves. Nothing here blocks -- the audio callback must never wait, so a full
 * ring DROPS the newest samples and counts them rather than blocking the
 * emulation thread.
 *
 * Layout is plain f32 interleaved stereo, because that is what the HLE already
 * produces and what AudioWorklet's channel arrays take without conversion.
 *
 * Dropping rather than blocking is a deliberate choice and it is measurable:
 * drop() counts. A ring that drops every block for a second is a starved
 * emulator; one that never drops is a worklet that is not consuming. Both are
 * visible in the counters, which is the only way to tell them apart from
 * outside.
 */
#include <atomic>
#include <cstdint>

namespace gpu {

class AudioRing {
public:
    /* capacity_frames is the number of stereo frames; the byte size is twice
     * that many floats. Must be a power of two. */
    AudioRing(float* memory, uint32_t capacity_frames);

    /* Producer (emulation thread). Writes as many frames as fit and returns how
     * many were written; the remainder are counted as dropped. Never blocks. */
    uint32_t write(const float* stereo, uint32_t frames);

    /* Consumer (AudioWorklet). Copies up to frames into out; returns how many
     * were available. Missing frames are left untouched so the caller can zero
     * them, which is what a worklet does when it returns silence. */
    uint32_t read(float* out, uint32_t frames);

    uint32_t available() const;
    uint32_t free_frames() const;
    uint64_t dropped_frames() const { return dropped_.load(std::memory_order_relaxed); }
    uint64_t written_frames() const { return written_.load(std::memory_order_relaxed); }
    uint64_t read_frames() const { return read_.load(std::memory_order_relaxed); }

private:
    float* mem_;
    uint32_t cap_;          /* frames */
    alignas(64) std::atomic<uint32_t> head_{0};   /* frame index, producer only */
    alignas(64) std::atomic<uint32_t> tail_{0};   /* frame index, consumer only */
    std::atomic<uint64_t> dropped_{0};
    std::atomic<uint64_t> written_{0};
    std::atomic<uint64_t> read_{0};
};

}  // namespace gpu
