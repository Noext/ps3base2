#pragma once

/* The pipeline cache WebGPU needs because it has no dynamic state.
 *
 * RSX changes blend/depth/stencil/topology per draw. On Vulkan that was a
 * vkCmdSet*; here every unseen combination must become a pipeline at creation
 * time, which is the most expensive operation WebGPU offers. So the cache is not
 * an optimisation -- it is what keeps the frame loop from creating pipelines
 * forever.
 *
 * The cache is deliberately device-free. It maps a PipelineKey to an opaque
 * handle and records how often each lookup missed; the caller supplies creation.
 * That keeps the eviction policy and the hit rates testable on a machine with no
 * GPU, which is the only way to measure the cost the prompt asks for before the
 * port exists.
 *
 * Eviction: a fixed capacity with least-recently-used replacement. A frame that
 * exceeds the capacity thrashes, and thrashing is exactly what the miss counter
 * will show -- better to measure it than to hide it behind an unbounded map.
 */
#include "runtime/pipeline_key.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gpu {
namespace wgpu {

struct PipelineCacheStats {
    uint64_t lookups = 0;
    uint64_t hits = 0;
    uint64_t misses = 0;       /* pipeline creations the caller had to perform */
    uint64_t evictions = 0;
    uint64_t thrash_frames = 0; /* frames whose misses exceeded the capacity */

    double hit_rate() const {
        return lookups ? static_cast<double>(hits) / static_cast<double>(lookups) : 0.0;
    }
};

class PipelineCache {
public:
    explicit PipelineCache(size_t capacity = 256);

    /* Returns the handle for this state, or 0 on a miss. On a miss the caller
     * must create the pipeline and call insert() with the handle it got. A 0
     * handle is never a valid pipeline, so 0 is unambiguous. */
    uint64_t find(const PipelineKey& key);

    void insert(const PipelineKey& key, uint64_t handle);

    /* Called once per frame so thrash_frames counts frames, not draws. */
    void end_frame();

    const PipelineCacheStats& stats() const { return stats_; }
    size_t size() const { return entries_used_; }
    size_t capacity() const { return capacity_; }

private:
    struct Entry {
        PipelineKey key{};
        uint64_t handle = 0;
        uint64_t last_used = 0;
        bool live = false;
    };

    size_t slot_for(const PipelineKey& key) const;
    void evict_one();

    std::vector<Entry> slots_;
    size_t capacity_;
    size_t entries_used_ = 0;
    uint64_t clock_ = 0;
    uint64_t frame_misses_ = 0;
    PipelineCacheStats stats_{};
};

}  // namespace wgpu
}  // namespace gpu
