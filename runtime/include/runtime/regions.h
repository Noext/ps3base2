#pragma once

/* Guest region bases, in one place.
 *
 * The map was flat and sparse: video at 0xC0000000, stack at 0xD0000000, spu at
 * 0xE0000000, with 1792 MB of nothing between the user regions and video. That
 * made the guest span exactly 4 GiB (docs/webassembly/MEMORY-4GB.md), which is
 * impossible for a wasm32 build: linear memory would need max(ea) bytes and there
 * would be nothing left for code, stack or tables.
 *
 * PS3BASE_REGIONS=compact moves the three high regions down to close the gap,
 * giving 2.25 GiB and leaving 1.75 GiB for the runtime. The guest never observes
 * the move: grep over all 98 lifted units finds no reference to any of the three
 * old bases, and sys_rsx_context_allocate takes its area from the region
 * allocator rather than naming an address.
 *
 * Default is the sparse map, so the native path is unchanged until the compact
 * mode is proven.
 */
#include <cstdint>

namespace vm {

struct RegionBases {
    uint32_t video;
    uint32_t stack;
    uint32_t spu;
    uint32_t spu_size;
    uint32_t user_bound;   /* upper bound for user-region slot placement */
    bool compact;
};

/* Chosen once from PS3BASE_REGIONS; every consumer reads it here. */
const RegionBases& region_bases();

/* Highest address the map can reach, which is what a wasm32 build must fit. */
uint64_t map_span();

}  // namespace vm
