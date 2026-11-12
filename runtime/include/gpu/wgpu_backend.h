/* A WebGPU path that runs ALONGSIDE the Vulkan renderer, not instead of it.
 *
 * Why alongside: the Vulkan path is the reference the WebGPU path has to be
 * judged against. Replacing it would leave nothing to compare with, and every
 * measurement below needs the two images of the SAME frame.
 *
 * How the mirror works. The renderer already has, at every draw, everything a
 * WebGPU backend needs and in CPU-visible form:
 *   - the vertex data, decoded into a host ring (rsx_renderer.cpp vdata[]);
 *   - the index list, decoded into a host ring (idx_out);
 *   - the four uniform blocks, built on the host (vs, lits, fs, vp constants);
 *   - the decoded texture pixels, in a host staging ring before they are copied
 *     into a VkImage.
 * So this backend does not re-run any RSX decoding: it consumes the values the
 * Vulkan path has already produced and replays them to wgpu. That is what makes
 * the comparison meaningful -- any pixel difference comes from the two GPU APIs,
 * not from two different front ends.
 *
 * Shaders are the renderer's own SPIR-V, passed through unchanged. wgpu-native
 * compiles GLSL/SPIR-V with naga at shader-module creation, so there is no WGSL
 * generator here and no second shader pipeline to keep in sync.
 *
 * Two constraints found by measurement, both of which the shader side must meet
 * before a module is accepted (see docs/explore/H2/RESULTS.md):
 *   - SPIR-V must target 1.5, not 1.6: 1.6 encodes `discard` as
 *     OpTerminateInvocation, which naga rejects ("unsupported instruction").
 *     1.5 emits OpKill, which it accepts.
 *   - samplers must be separate texture + sampler bindings: a combined
 *     sampler2D becomes one OpTypeSampledImage variable, which naga rejects
 *     ("invalid id"). The decompiler is changed to emit the split form.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gpu {
namespace wgpu {

/* Enabled by PS3BASE_WGPU=1. Off by default so the Vulkan path is unaffected:
 * a build with this module linked but the variable unset behaves exactly as
 * before. */
bool enabled();

/* Bring the device up. Returns false and logs why on failure; a failure is not
 * fatal to the Vulkan path. */
bool init(uint32_t width, uint32_t height);

/* The per-draw inputs, all of them already decoded by the Vulkan path. A null
 * span means the draw does not carry that input. */
struct DrawInputs {
    /* Vertex streams, one per attribute slot: float4 per vertex. */
    const float* vertex[16] = {};
    /* Which slots the shader declares. A declared slot with no data is a real
     * state (the edge-compute path hands its stream to the GPU), and WebGPU
     * rejects a draw that does not bind it, so the mask is needed. */
    uint32_t vertex_inputs = 0;
    uint32_t vertex_count = 0;
    /* Index list, already expanded and biased to vmin. */
    const uint32_t* indices = nullptr;
    uint32_t index_count = 0;
    int32_t base_vertex = 0;

    /* Uniform blocks, matching the Vulkan descriptor layout at bindings 0..3. */
    const void* uniform[4] = {};
    uint32_t uniform_size[4] = {};

    /* Textures, one per sampler unit. Only the units set in `texture_mask` are
     * read. `pixels` is the decoded RGBA8 image, `width` x `height`. */
    struct Texture {
        const uint8_t* pixels = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t mip_levels = 1;
    } texture[16];
    uint32_t texture_mask = 0;
    /* Values the RSX shader multiplies its texture coordinates by; part of the
     * fragment uniform block, kept here so the sampler state can use them. */
    float texture_scale[16] = {};

    /* Pipeline state, already reduced by the Vulkan path's PipelineKey. */
    uint32_t color_format = 0;   /* WGPUTextureFormat */
    uint32_t depth_format = 0;   /* WGPUTextureFormat, 0 when the pass has none */
    uint32_t topology = 0;       /* 0 point, 1 line, 2 triangle list */
    bool depth_test = false;
    bool depth_write = false;
    uint32_t depth_compare = 0;  /* WGPUCompareFunction */
    uint32_t blend_enable = 0;
    uint32_t sfactor = 0, dfactor = 0, equation = 0;  /* WGPUBlendFactor/Op */
    uint32_t write_mask = 0xF;
    uint32_t cull = 0;           /* WGPUCullMode */
    bool front_face_cw = false;
    uint32_t stencil_mask = 0;
    bool stencil_test = false;

    /* Shader identity: the SPIR-V the Vulkan path compiled for this draw. */
    const uint32_t* vs_spirv = nullptr;
    size_t vs_words = 0;
    const uint32_t* fs_spirv = nullptr;
    size_t fs_words = 0;
    uint64_t vp_hash = 0, fp_hash = 0;

    /* The render target the pass draws into, then the viewport and scissor
     * WITHIN it. The target is a property of the frame, not of the draw, but it
     * is passed here because the renderer only knows its size at draw time. */
    uint32_t target_width = 0;
    uint32_t target_height = 0;
    float viewport[4] = {0, 0, 0, 0};
    int32_t scissor[4] = {0, 0, 0, 0};
};

/* Replay one draw. No-op when not enabled. */
void draw(const DrawInputs& in);

/* Clear the current target. `color` is RGBA in 0..1; pass clear_depth = false
 * to leave depth untouched. */
void clear(uint32_t mask, const float color[4], float depth, uint32_t stencil);

/* Read the current colour target back as RGBA8 and write it as a PPM. Returns
 * false on failure. This is what the comparison is computed from. */
bool capture_ppm(const std::string& path, uint32_t width, uint32_t height);

void shutdown();

/* Vulkan -> WebGPU value conversion for the handful of values the mirror
 * passes across. Each takes the Vulkan enum the renderer already computed, so the
 * mirror never re-derives state from RSX registers: if it did, a difference would
 * be a front-end bug rather than an API difference, which is the opposite of what
 * this backend is for. A value with no WebGPU counterpart returns the Undefined
 * member of its enum; the backend treats that as "keep current state". */
uint32_t format_for(int vk_format);
uint32_t compare_for(int vk_compare_op);
uint32_t blend_for(int vk_blend_factor);
uint32_t equation_for(int vk_blend_op);
uint32_t cull_for(int vk_cull_flags);
/* Diagnostics, printed by the process at exit: pipelines built, draws replayed,
 * textures uploaded, and the first WebGPU error seen (empty when none). */
struct Stats {
    uint64_t draws = 0;
    uint64_t pipelines = 0;
    uint64_t textures = 0;
    uint64_t upload_bytes = 0;
    uint64_t skipped = 0;
    std::string first_error;
};
Stats stats();

}
}
