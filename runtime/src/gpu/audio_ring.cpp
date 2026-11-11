#include "runtime/audio_ring.h"

#include <algorithm>
#include <cstring>

namespace gpu {

AudioRing::AudioRing(float* memory, uint32_t capacity_frames)
    : mem_(memory), cap_(capacity_frames ? capacity_frames : 1) {
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
}

uint32_t AudioRing::available() const {
    const uint32_t h = head_.load(std::memory_order_acquire);
    const uint32_t t = tail_.load(std::memory_order_acquire);
    return (h >= t) ? (h - t) : (cap_ - t + h);
}

uint32_t AudioRing::free_frames() const { return cap_ - 1 - available(); }

uint32_t AudioRing::write(const float* stereo, uint32_t frames) {
    if (!stereo || !frames) return 0;
    const uint32_t room = free_frames();
    const uint32_t take = (std::min)(frames, room);
    if (take < frames) dropped_.fetch_add(frames - take, std::memory_order_relaxed);
    if (!take) return 0;

    uint32_t h = head_.load(std::memory_order_relaxed);
    const uint32_t first = (std::min)(take, cap_ - h);
    std::memcpy(mem_ + 2u * h, stereo, 2u * first * sizeof(float));
    if (take > first) {
        std::memcpy(mem_, stereo + 2u * first, 2u * (take - first) * sizeof(float));
    }
    h += take;
    if (h >= cap_) h -= cap_;
    head_.store(h, std::memory_order_release);
    written_.fetch_add(take, std::memory_order_relaxed);
    return take;
}

uint32_t AudioRing::read(float* out, uint32_t frames) {
    if (!out || !frames) return 0;
    const uint32_t have = available();
    const uint32_t take = (std::min)(frames, have);
    if (!take) return 0;

    const uint32_t t = tail_.load(std::memory_order_relaxed);
    const uint32_t first = (std::min)(take, cap_ - t);
    std::memcpy(out, mem_ + 2u * t, 2u * first * sizeof(float));
    if (take > first) {
        std::memcpy(out + 2u * first, mem_, 2u * (take - first) * sizeof(float));
    }
    uint32_t nt = t + take;
    if (nt >= cap_) nt -= cap_;
    tail_.store(nt, std::memory_order_release);
    read_.fetch_add(take, std::memory_order_relaxed);
    return take;
}

}  // namespace gpu
