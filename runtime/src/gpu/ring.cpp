#include "ring.h"
#include "runtime/log.h"

namespace gpu {

namespace {

/* T1: the ring size is a MEMORY BUDGET number under wasm, not a taste.
 *
 * The guest flat buffer (mem::reserve, bitset mode) is 2432 MB, and the module is
 * linked with -sMAXIMUM_MEMORY=3072 MB. 256 MB x 2 slots = 512 MB does not fit in
 * the 640 MB that remains once code, stack and the initial heap are counted, so
 * vkMapMemory's calloc failed. The old stub returned VK_SUCCESS anyway with a null
 * pointer, ring_alloc computed out.ptr = null + offset, and decode_texture wrote
 * decoded textures over the module's own low memory: "memory access out of bounds"
 * at the first 1280x720 texture, several functions from the cause.
 *
 * 64 MB x 2 = 128 MB fits with room to spare (measured headroom: 128 MB free after
 * the flat buffer alone). The ring is a staging area for one frame's decoded
 * vertices and textures, so 64 MB per slot is the honest size here; ring_alloc
 * already refuses and logs when a single frame exceeds it, which is a visible
 * refusal rather than a silent overwrite. */
#if defined(__EMSCRIPTEN__)
constexpr VkDeviceSize RING_SIZE = 64ull << 20;
#else
constexpr VkDeviceSize RING_SIZE = 256ull << 20;
#endif
constexpr VkDeviceSize RING_SLOTS = 2;

VkBuffer g_buffer = VK_NULL_HANDLE;
VkDeviceMemory g_memory = VK_NULL_HANDLE;
uint8_t* g_ptr = nullptr;
VkDeviceSize g_head = 0;
VkDeviceSize g_base = 0;
uint64_t g_serial = ~0ull;
/* Advanced once per presented frame; see ring_alloc. */
uint64_t g_ring_frame = 0;

bool create() {
    const VkCtx& c = vk();
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = RING_SIZE * RING_SLOTS;
    bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
               VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (vkCreateBuffer(c.device, &bi, nullptr, &g_buffer) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(c.device, g_buffer, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = find_memory(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(c.device, &ai, nullptr, &g_memory) != VK_SUCCESS) return false;
    vkBindBufferMemory(c.device, g_buffer, g_memory, 0);
    /* T1: the result of this call was DISCARDED, and that is how a null mapping
     * became a base address. vkMapMemory can fail (under wasm it is a calloc, and
     * the ring competes with the 2432 MB guest flat buffer inside a 3072 MB
     * ceiling); when it did, g_ptr stayed null, ring_alloc computed
     * out.ptr = null + offset, and decode_texture wrote decoded textures over the
     * module's own data segment. The trap then surfaced far from here.
     *
     * A null mapping is a REFUSAL: create() returns false, ring_alloc returns
     * false, every caller already handles that. Never a fake success. */
    if (vkMapMemory(c.device, g_memory, 0, RING_SIZE * RING_SLOTS, 0, reinterpret_cast<void**>(&g_ptr)) != VK_SUCCESS || !g_ptr) {
        LOG_ERROR("gpu", "ring: could not map %llu bytes of host-visible memory; refusing to hand out a null base",
                  static_cast<unsigned long long>(RING_SIZE * RING_SLOTS));
        g_ptr = nullptr;
        return false;
    }
    return true;
}

}

void ring_frame_advance() { g_ring_frame++; }
uint64_t ring_frame() { return g_ring_frame; }

bool ring_alloc(VkDeviceSize size, VkDeviceSize align, RingAlloc& out) {
    if (!g_buffer && !create()) return false;
    /* THE RING'S OWN FRAME COUNTER, not frame_serial().
     *
     * frame_serial() is constant 0 under wasm (gpu_wasm_support.cpp explains why it
     * must stay constant: the renderer keys surface retirement, texture last_used
     * and the writeback ring on it). Using it here meant this branch never fired,
     * g_head only ever grew, and a 64 MB ring refused a 7 488-byte request with
     * "ring buffer exhausted in one frame" 46 528 times in one run.
     *
     * ring_frame_advance() is called once per presented frame by the renderer, and
     * this counter is private to the ring, so rotating slots here cannot disturb
     * anything that reads frame_serial(). */
    if (g_ring_frame != g_serial) {
        g_serial = g_ring_frame;
        g_base = (g_ring_frame % RING_SLOTS) * RING_SIZE;
        g_head = 0;
    }
    VkDeviceSize start = (g_head + align - 1) & ~(align - 1);
    if (start + size > RING_SIZE) {
        LOG_WARN("gpu", "ring buffer exhausted in one frame (%llu bytes requested)", static_cast<unsigned long long>(size));
        return false;
    }
    g_head = start + size;
    out.buffer = g_buffer;
    out.offset = g_base + start;
    out.ptr = g_ptr + g_base + start;
    return true;
}

}
