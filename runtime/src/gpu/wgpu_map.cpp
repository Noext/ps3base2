#include "runtime/wgpu_map.h"

namespace gpu {
namespace wgpu {

namespace {

/* Format table. Each entry is a decision, not a wish: where WebGPU has no
 * equivalent the column says what must happen instead. */
const FormatMap kFormats[] = {
    {"VK_FORMAT_UNDEFINED", "WGPUTextureFormat_Undefined", Map::Direct, "same meaning"},
    {"VK_FORMAT_R8_UNORM", "WGPUTextureFormat_R8Unorm", Map::Direct, ""},
    {"VK_FORMAT_R8G8_UNORM", "WGPUTextureFormat_RG8Unorm", Map::Direct, ""},
    {"VK_FORMAT_R8G8_SNORM", "WGPUTextureFormat_RG8Snorm", Map::Direct, ""},
    {"VK_FORMAT_R8G8B8A8_UNORM", "WGPUTextureFormat_RGBA8Unorm", Map::Direct, ""},
    {"VK_FORMAT_R8G8B8A8_SRGB", "WGPUTextureFormat_RGBA8UnormSrgb", Map::Direct, ""},
    {"VK_FORMAT_B8G8R8A8_UNORM", "WGPUTextureFormat_BGRA8Unorm", Map::Direct, ""},
    {"VK_FORMAT_B8G8R8A8_SRGB", "WGPUTextureFormat_BGRA8UnormSrgb", Map::Direct, ""},
    {"VK_FORMAT_R16_UNORM", "WGPUTextureFormat_R16Unorm", Map::Direct, ""},
    {"VK_FORMAT_R16G16_UNORM", "WGPUTextureFormat_RG16Unorm", Map::Direct, ""},
    {"VK_FORMAT_R16G16_SFLOAT", "WGPUTextureFormat_RG16Float", Map::Direct, ""},
    {"VK_FORMAT_R16G16B16A16_SFLOAT", "WGPUTextureFormat_RGBA16Float", Map::Direct, ""},
    {"VK_FORMAT_R32_SFLOAT", "WGPUTextureFormat_R32Float", Map::Direct, ""},
    {"VK_FORMAT_R32G32B32A32_SFLOAT", "WGPUTextureFormat_RGBA32Float", Map::Direct, ""},
    {"VK_FORMAT_D16_UNORM", "WGPUTextureFormat_Depth16Unorm", Map::Direct, ""},
    {"VK_FORMAT_D32_SFLOAT", "WGPUTextureFormat_Depth32Float", Map::Direct, ""},
    {"VK_FORMAT_D24_UNORM_S8_UINT", "WGPUTextureFormat_Depth24PlusStencil8", Map::Direct, "stencil aspect, not a UNORM sample"},
    {"VK_FORMAT_D32_SFLOAT_S8_UINT", "WGPUTextureFormat_Depth32FloatStencil8", Map::Feature, "needs depth32float-stencil8"},

    /* No 16-bit packed RGBA colour format exists in WebGPU. Widening to RGBA8
     * doubles the bytes per texel on every such texture; the renderer's texture
     * cache must budget for it. */
    {"VK_FORMAT_A1R5G5B5_UNORM_PACK16", "WGPUTextureFormat_RGBA8Unorm", Map::Expand, "16-bit packed -> 8 bit/chan, 2x bytes"},
    {"VK_FORMAT_R5G5B5A1_UNORM_PACK16", "WGPUTextureFormat_RGBA8Unorm", Map::Expand, "16-bit packed -> 8 bit/chan, 2x bytes"},
    {"VK_FORMAT_R4G4B4A4_UNORM_PACK16", "WGPUTextureFormat_RGBA8Unorm", Map::Expand, "16-bit packed -> 8 bit/chan, 2x bytes"},
    {"VK_FORMAT_R5G6B5_UNORM_PACK16", "WGPUTextureFormat_RGBA8Unorm", Map::Expand, "16-bit packed -> 8 bit/chan, 2x bytes"},

    /* Block-compressed formats exist in WebGPU but are feature-gated. */
    {"VK_FORMAT_BC1_RGBA_UNORM_BLOCK", "WGPUTextureFormat_BC1RGBAUnorm", Map::Feature, "texture-compression-bc"},
    {"VK_FORMAT_BC1_RGBA_SRGB_BLOCK", "WGPUTextureFormat_BC1RGBAUnormSrgb", Map::Feature, "texture-compression-bc"},
    {"VK_FORMAT_BC2_UNORM_BLOCK", "WGPUTextureFormat_BC2RGBAUnorm", Map::Feature, "texture-compression-bc"},
    {"VK_FORMAT_BC2_SRGB_BLOCK", "WGPUTextureFormat_BC2RGBAUnormSrgb", Map::Feature, "texture-compression-bc"},
    {"VK_FORMAT_BC3_UNORM_BLOCK", "WGPUTextureFormat_BC3RGBAUnorm", Map::Feature, "texture-compression-bc"},
    {"VK_FORMAT_BC3_SRGB_BLOCK", "WGPUTextureFormat_BC3RGBAUnormSrgb", Map::Feature, "texture-compression-bc"},
};

/* Blend factors. WebGPU has no constant-alpha factor and no blend-constant
 * colour in blend state, so those four need emulation. */
const BlendMap kBlend[] = {
    {"VK_BLEND_FACTOR_ZERO", "WGPUBlendFactor_Zero", Map::Direct, ""},
    {"VK_BLEND_FACTOR_ONE", "WGPUBlendFactor_One", Map::Direct, ""},
    {"VK_BLEND_FACTOR_SRC_COLOR", "WGPUBlendFactor_Src", Map::Direct, ""},
    {"VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR", "WGPUBlendFactor_OneMinusSrc", Map::Direct, ""},
    {"VK_BLEND_FACTOR_DST_COLOR", "WGPUBlendFactor_Dst", Map::Direct, ""},
    {"VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR", "WGPUBlendFactor_OneMinusDst", Map::Direct, ""},
    {"VK_BLEND_FACTOR_SRC_ALPHA", "WGPUBlendFactor_SrcAlpha", Map::Direct, ""},
    {"VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA", "WGPUBlendFactor_OneMinusSrcAlpha", Map::Direct, ""},
    {"VK_BLEND_FACTOR_DST_ALPHA", "WGPUBlendFactor_DstAlpha", Map::Direct, ""},
    {"VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA", "WGPUBlendFactor_OneMinusDstAlpha", Map::Direct, ""},
    {"VK_BLEND_FACTOR_SRC_ALPHA_SATURATE", "WGPUBlendFactor_SrcAlphaSaturated", Map::Direct, ""},
    {"VK_BLEND_FACTOR_CONSTANT_COLOR", "WGPUBlendFactor_Constant", Map::Direct, "blend constant via SetBlendConstant"},
    {"VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR", "WGPUBlendFactor_OneMinusConstant", Map::Direct, ""},
    /* These two have no WebGPU factor at all: there is no constant-alpha. */
    {"VK_BLEND_FACTOR_CONSTANT_ALPHA", "WGPUBlendFactor_Constant", Map::Emulate, "no constant-alpha; use the constant's alpha channel through a shader uniform"},
    {"VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA", "WGPUBlendFactor_OneMinusConstant", Map::Emulate, "same"},
};

/* Pipeline state beyond formats and blend factors. The interesting entries are
 * the ones WebGPU does not have: triangle fans, separate stencil ops per face in
 * some combinations, and logic ops (absent from WebGPU entirely). */
const StateMap kState[] = {
    /* topology -- measured: 6 real values plus the sentinel */
    {"VK_PRIMITIVE_TOPOLOGY_POINT_LIST", "WGPUPrimitiveTopology_PointList", Map::Direct, ""},
    {"VK_PRIMITIVE_TOPOLOGY_LINE_LIST", "WGPUPrimitiveTopology_LineList", Map::Direct, ""},
    {"VK_PRIMITIVE_TOPOLOGY_LINE_STRIP", "WGPUPrimitiveTopology_LineStrip", Map::Direct, ""},
    {"VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST", "WGPUPrimitiveTopology_TriangleList", Map::Direct, ""},
    {"VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP", "WGPUPrimitiveTopology_TriangleStrip", Map::Direct, ""},
    {"VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN", "WGPUPrimitiveTopology_TriangleList", Map::Emulate, "WebGPU has no fan; reindex to a list"},

    /* compare -- all seven exist */
    {"VK_COMPARE_OP_NEVER", "WGPUCompareFunction_Never", Map::Direct, ""},
    {"VK_COMPARE_OP_LESS", "WGPUCompareFunction_Less", Map::Direct, ""},
    {"VK_COMPARE_OP_EQUAL", "WGPUCompareFunction_Equal", Map::Direct, ""},
    {"VK_COMPARE_OP_LESS_OR_EQUAL", "WGPUCompareFunction_LessEqual", Map::Direct, ""},
    {"VK_COMPARE_OP_GREATER", "WGPUCompareFunction_Greater", Map::Direct, ""},
    {"VK_COMPARE_OP_GREATER_OR_EQUAL", "WGPUCompareFunction_GreaterEqual", Map::Direct, ""},
    {"VK_COMPARE_OP_ALWAYS", "WGPUCompareFunction_Always", Map::Direct, ""},

    /* cull -- WebGPU has none/front/back. FRONT_AND_BACK is expressed by
     * discarding every triangle, which the shader can do, so it is Emulate and
     * not Absent. */
    {"VK_CULL_MODE_NONE", "WGPUCullMode_None", Map::Direct, ""},
    {"VK_CULL_MODE_FRONT_BIT", "WGPUCullMode_Front", Map::Direct, ""},
    {"VK_CULL_MODE_BACK_BIT", "WGPUCullMode_Back", Map::Direct, ""},
    {"VK_CULL_MODE_FRONT_AND_BACK", "WGPUCullMode_None", Map::Emulate, "cull everything: discard in the vertex stage"},

    /* blend ops -- all five exist */
    {"VK_BLEND_OP_ADD", "WGPUBlendOperation_Add", Map::Direct, ""},
    {"VK_BLEND_OP_SUBTRACT", "WGPUBlendOperation_Subtract", Map::Direct, ""},
    {"VK_BLEND_OP_REVERSE_SUBTRACT", "WGPUBlendOperation_ReverseSubtract", Map::Direct, ""},
    {"VK_BLEND_OP_MIN", "WGPUBlendOperation_Min", Map::Direct, ""},
    {"VK_BLEND_OP_MAX", "WGPUBlendOperation_Max", Map::Direct, ""},

    /* stencil ops -- all eight exist */
    {"VK_STENCIL_OP_KEEP", "WGPUStencilOperation_Keep", Map::Direct, ""},
    {"VK_STENCIL_OP_ZERO", "WGPUStencilOperation_Zero", Map::Direct, ""},
    {"VK_STENCIL_OP_REPLACE", "WGPUStencilOperation_Replace", Map::Direct, ""},
    {"VK_STENCIL_OP_INCREMENT_AND_CLAMP", "WGPUStencilOperation_IncrementClamp", Map::Direct, ""},
    {"VK_STENCIL_OP_DECREMENT_AND_CLAMP", "WGPUStencilOperation_DecrementClamp", Map::Direct, ""},
    {"VK_STENCIL_OP_INVERT", "WGPUStencilOperation_Invert", Map::Direct, ""},
    {"VK_STENCIL_OP_INCREMENT_AND_WRAP", "WGPUStencilOperation_IncrementWrap", Map::Direct, ""},
    {"VK_STENCIL_OP_DECREMENT_AND_WRAP", "WGPUStencilOperation_DecrementWrap", Map::Direct, ""},
};

constexpr size_t kStateCount = sizeof(kState) / sizeof(kState[0]);
constexpr size_t kFormatCount = sizeof(kFormats) / sizeof(kFormats[0]);
constexpr size_t kBlendCount = sizeof(kBlend) / sizeof(kBlend[0]);

}  // namespace

const FormatMap* format_table(size_t& count) {
    count = kFormatCount;
    return kFormats;
}

const BlendMap* blend_table(size_t& count) {
    count = kBlendCount;
    return kBlend;
}

Coverage format_coverage() {
    Coverage c;
    for (size_t i = 0; i < kFormatCount; i++) {
        switch (kFormats[i].kind) {
        case Map::Direct: c.direct++; break;
        case Map::Feature: c.feature++; break;
        case Map::Expand: c.expand++; break;
        case Map::Emulate: c.emulate++; break;
        case Map::Absent: c.absent++; break;
        }
    }
    return c;
}

const StateMap* state_table(size_t& count) {
    count = kStateCount;
    return kState;
}

Coverage state_coverage() {
    Coverage c;
    for (size_t i = 0; i < kStateCount; i++) {
        switch (kState[i].kind) {
        case Map::Direct: c.direct++; break;
        case Map::Feature: c.feature++; break;
        case Map::Expand: c.expand++; break;
        case Map::Emulate: c.emulate++; break;
        case Map::Absent: c.absent++; break;
        }
    }
    return c;
}

Coverage blend_coverage() {
    Coverage c;
    for (size_t i = 0; i < kBlendCount; i++) {
        switch (kBlend[i].kind) {
        case Map::Direct: c.direct++; break;
        case Map::Feature: c.feature++; break;
        case Map::Expand: c.expand++; break;
        case Map::Emulate: c.emulate++; break;
        case Map::Absent: c.absent++; break;
        }
    }
    return c;
}

}  // namespace wgpu
}  // namespace gpu
