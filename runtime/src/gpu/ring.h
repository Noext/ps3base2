#pragma once

#include "vk_context.h"

#include <cstdint>

namespace gpu {

struct RingAlloc {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    uint8_t* ptr = nullptr;
};

bool ring_alloc(VkDeviceSize size, VkDeviceSize align, RingAlloc& out);
uint64_t frame_serial();
/* Advances the RING'S OWN frame counter, which is what rotates its two slots.
 * Called once per presented frame. Private to the ring on purpose: frame_serial()
 * is constant under wasm because the renderer keys surface retirement and the
 * writeback ring on it, so reusing it here left the ring unable to reclaim a slot
 * (measured: 46 528 refusals of a 7 KB request inside a 64 MB ring). */
void ring_frame_advance();
/* The current value of that counter, so a second consumer (the canvas texture
 * re-import) can key on the same frame boundary without sharing frame_serial(). */
uint64_t ring_frame();

}
