#pragma once

/* Vulkan -> WebGPU correspondence for every format and blend factor the renderer
 * actually uses, with the ones that have NO direct equivalent marked as such
 * instead of being papered over.
 *
 * This table is the first brick of milestone 2: it is the part of the port that
 * can be written and audited without a device, and the two unknowns that decide
 * the renderer's shape -- 16-bit packed colour and constant-alpha blending -- are
 * named here so they cannot be discovered late.
 *
 * Reference: WGPUTextureFormat / WGPUBlendFactor in wgpu-native v29
 * (/root/Dev/ps2-web-recomp/third_party/wgpu-native/include/webgpu).
 */
#include <cstddef>
#include <cstdint>

namespace gpu {
namespace wgpu {

enum class Map {
    Direct,        /* same format exists with the same meaning */
    Feature,       /* exists but needs a device feature */
    Expand,        /* no equivalent; must be widened, costs bandwidth */
    Emulate,       /* no equivalent; needs shader or state work */
    Absent,        /* not representable; the path must change */
};

struct FormatMap {
    const char* vk;
    const char* wgpu;
    Map kind;
    const char* note;
};

/* Every VK_FORMAT_* the renderer uses, 28 of them, each with a decision. */
const FormatMap* format_table(size_t& count);

struct BlendMap {
    const char* vk;
    const char* wgpu;
    Map kind;
    const char* note;
};

const BlendMap* blend_table(size_t& count);

/* Summary counts for the report. */
struct Coverage {
    int direct = 0;
    int feature = 0;
    int expand = 0;
    int emulate = 0;
    int absent = 0;
};
Coverage format_coverage();
Coverage blend_coverage();

}  // namespace wgpu
}  // namespace gpu
