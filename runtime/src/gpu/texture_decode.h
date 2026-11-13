#pragma once

#include "vk_context.h"

#include <cstdint>
#include <vector>

namespace gpu {

struct TextureRegs {
    uint32_t offset = 0;
    uint32_t format = 0;
    uint32_t address = 0;
    uint32_t control0 = 0;
    uint32_t control1 = 0;
    uint32_t filter = 0;
    uint32_t rect = 0;
    uint32_t control3 = 0;
    uint32_t border = 0;
};

struct TextureRegion {
    uint64_t offset = 0;
    uint32_t level = 0;
    uint32_t layer = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
};

struct TextureLayout {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkComponentMapping mapping{};
    VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_2D;
    uint32_t base = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
    uint32_t levels = 1;
    uint32_t layers = 1;
    uint32_t source_size = 0;
    uint64_t decoded_size = 0;
    /* Bytes the DECODER writes per texel. It is the format's real width (1 for
     * R8, 2 for the packed 16-bit ones, 4 for the 32-bit ones, 8/16 for the
     * block formats), NOT always 4. The WebGPU mirror has to know it: it was
     * reading width*height*4 bytes out of a buffer that can be four times
     * smaller, which is a read past the end -- measured as a SIGSEGV in
     * wgpu::upload_texture with the fault address one past the vector. */
    uint32_t out_bytes = 4;
    bool is_depth = false;
    std::vector<TextureRegion> regions;
};

bool describe_texture(const TextureRegs& regs, TextureLayout& out);
/* Dump the per-format decode histogram (PS3BASE_TEX_LOG). */
void tex_stats_dump();

void decode_texture(const uint8_t* src, const TextureRegs& regs, const TextureLayout& layout, uint8_t* dst);
VkComponentMapping texture_mapping(uint32_t base_format, uint32_t control1);
VkComponentMapping remap_mapping(const VkComponentSwizzle base[4], bool remap, uint32_t control1);

}
