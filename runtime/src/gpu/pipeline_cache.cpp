#include "runtime/pipeline_cache.h"

#include <algorithm>

namespace gpu {
namespace wgpu {

PipelineCache::PipelineCache(size_t capacity)
    : slots_(capacity ? capacity : 1), capacity_(capacity ? capacity : 1) {}

size_t PipelineCache::slot_for(const PipelineKey& key) const {
    /* Linear probing. A pipeline cache is small (hundreds of entries) and the
     * probe sequence stays short at that load factor, so a chained map would add
     * allocation without buying anything measurable. */
    const size_t start = static_cast<size_t>(hash_key(key) % capacity_);
    for (size_t i = 0; i < capacity_; i++) {
        const size_t s = (start + i) % capacity_;
        if (!slots_[s].live) return s;
        if (slots_[s].key == key) return s;
    }
    return start;
}

uint64_t PipelineCache::find(const PipelineKey& key) {
    stats_.lookups++;
    clock_++;
    const size_t s = slot_for(key);
    if (slots_[s].live && slots_[s].key == key) {
        slots_[s].last_used = clock_;
        stats_.hits++;
        return slots_[s].handle;
    }
    stats_.misses++;
    frame_misses_++;
    return 0;
}

void PipelineCache::insert(const PipelineKey& key, uint64_t handle) {
    if (!handle) return;  /* a caller that failed to create must not poison the cache */
    size_t s = slot_for(key);
    /* slot_for() returns the matching slot if the key is present, otherwise the
     * first free one -- which may be past the end of a full table. Only an absent
     * key at capacity needs an eviction, and the eviction frees a slot that the
     * second probe can then find. */
    if (!(slots_[s].live && slots_[s].key == key) && entries_used_ >= capacity_) {
        evict_one();
        s = slot_for(key);
    }
    if (!slots_[s].live) entries_used_++;
    slots_[s].key = key;
    slots_[s].handle = handle;
    slots_[s].last_used = ++clock_;
    slots_[s].live = true;
}

void PipelineCache::evict_one() {
    /* Least recently used. Scanning is O(capacity) and only happens on a miss at
     * capacity, which is the pathological case we want to count anyway. */
    size_t oldest = 0;
    for (size_t i = 1; i < capacity_; i++) {
        if (!slots_[i].live) { oldest = i; break; }
        if (slots_[i].last_used < slots_[oldest].last_used) oldest = i;
    }
    if (slots_[oldest].live) {
        slots_[oldest].live = false;
        entries_used_--;
        stats_.evictions++;
    }
}

void PipelineCache::end_frame() {
    if (frame_misses_ > capacity_) stats_.thrash_frames++;
    frame_misses_ = 0;
}

}  // namespace wgpu
}  // namespace gpu
