#pragma once

/* The key WebGPU needs in place of Vulkan's dynamic state.
 *
 * WebGPU pipelines bake in blend, depth, stencil, raster and topology at creation
 * time; there is no vkCmdSet*. RSX changes that state PER DRAW, so the port must
 * look up -- and on a miss create -- a pipeline for every combination it sees.
 *
 * The key covers the 21 states the renderer actually declares (measured, not
 * assumed): 4 blend factors plus write mask, the depth quartet, the stencil
 * seven, topology, cull, front face, and the render-target format. Fields whose
 * value Vulkan set through vkCmdSetViewport / SetScissor / SetLineWidth /
 * SetDepthBias / SetBlendConstants stay OUT of the key: WebGPU keeps viewport,
 * scissor and line width as dynamic state, so they do not force a new pipeline.
 *
 * Only the fields that WebGPU bakes in belong here. A key that is too large
 * multiplies pipeline creations; a key that is too small renders the wrong
 * picture. Both are failures, in opposite directions.
 */
#include <cstdint>
#include <cstring>

namespace gpu {
namespace wgpu {

enum class BlendFactor : uint8_t {
    Zero, One, SrcColor, OneMinusSrcColor, SrcAlpha, OneMinusSrcAlpha,
    DstColor, OneMinusDstColor, DstAlpha, OneMinusDstAlpha,
    /* No WebGPU equivalent: constant colour/alpha. Emulated by pre-multiplying
     * the source, which is why they get their own enumerators rather than
     * collapsing into Zero. */
    Constant, OneMinusConstant,
};

enum class BlendOp : uint8_t { Add, Subtract, ReverseSubtract, Min, Max };
enum class CompareOp : uint8_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
enum class StencilOp : uint8_t { Keep, Zero, Replace, Invert, IncrementClamp, DecrementClamp, IncrementWrap, DecrementWrap };
enum class Topology : uint8_t { PointList, LineList, LineStrip, TriangleList, TriangleStrip };
/* PrimitiveTopology::TriangleFan has no WebGPU equivalent and is absent here on
 * purpose: the renderer must convert fan draws to strips before reaching the key. */
enum class CullMode : uint8_t { None, Front, Back };
enum class FrontFace : uint8_t { Ccw, Cw };
enum class Format : uint8_t { RGBA8Unorm, RGBA8UnormSrgb, BGRA8Unorm, BGRA8UnormSrgb, R8Unorm, R16Float,
                              RGBA16Float, R32Float, Depth32Float, Depth24PlusStencil8 };

struct BlendState {
    bool enabled = false;
    BlendFactor src_color = BlendFactor::One;
    BlendFactor dst_color = BlendFactor::Zero;
    BlendOp color_op = BlendOp::Add;
    BlendFactor src_alpha = BlendFactor::One;
    BlendFactor dst_alpha = BlendFactor::Zero;
    BlendOp alpha_op = BlendOp::Add;
    uint8_t write_mask = 0xF;
};

struct DepthStencilState {
    bool depth_test = false;
    bool depth_write = false;
    CompareOp depth_compare = CompareOp::Always;
    bool stencil_test = false;
    uint8_t stencil_read_mask = 0xFF;
    uint8_t stencil_write_mask = 0xFF;
    CompareOp front_compare = CompareOp::Always;
    StencilOp front_fail = StencilOp::Keep;
    StencilOp front_depth_fail = StencilOp::Keep;
    StencilOp front_pass = StencilOp::Keep;
    CompareOp back_compare = CompareOp::Always;
    StencilOp back_fail = StencilOp::Keep;
    StencilOp back_depth_fail = StencilOp::Keep;
    StencilOp back_pass = StencilOp::Keep;
};

/* Everything WebGPU bakes into a pipeline that RSX changes per draw. */
struct PipelineKey {
    BlendState blend{};
    DepthStencilState depth_stencil{};
    Topology topology = Topology::TriangleList;
    CullMode cull = CullMode::None;
    FrontFace front_face = FrontFace::Ccw;
    Format color_format = Format::RGBA8Unorm;

    bool operator==(const PipelineKey& o) const {
        return std::memcmp(this, &o, sizeof *this) == 0;
    }
};

/* FNV-1a over the key's bytes. The struct is all POD with no padding-sensitive
 * members beyond the explicit uint8_t fields, so hashing the bytes is stable
 * within one binary -- which is all a runtime cache needs. */
inline uint64_t hash_key(const PipelineKey& k) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&k);
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < sizeof k; i++) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

}  // namespace wgpu
}  // namespace gpu
