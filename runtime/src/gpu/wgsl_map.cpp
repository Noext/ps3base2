#include "runtime/wgsl_map.h"

#include <cstring>

namespace gpu {
namespace wgsl {

namespace {

const Entry kEntries[] = {
    /* --- identical spelling, copy the call --- */
    {"abs", "abs", Kind::Same, "scalar and vector"},
    {"clamp", "clamp", Kind::Same, "scalar and vector"},
    {"cos", "cos", Kind::Same, nullptr},
    {"dot", "dot", Kind::Same, nullptr},
    {"exp", "exp", Kind::Same, nullptr},
    {"exp2", "exp2", Kind::Same, nullptr},
    {"fma", "fma", Kind::Same, "WGSL has fma since the first spec"},
    {"floor", "floor", Kind::Same, nullptr},
    {"fract", "fract", Kind::Same, nullptr},
    {"length", "length", Kind::Same, nullptr},
    {"log2", "log2", Kind::Same, nullptr},
    {"max", "max", Kind::Same, nullptr},
    {"min", "min", Kind::Same, nullptr},
    {"mix", "mix", Kind::Same, nullptr},
    {"pow", "pow", Kind::Same, nullptr},
    {"reflect", "reflect", Kind::Same, nullptr},
    {"sign", "sign", Kind::Same, nullptr},
    {"sin", "sin", Kind::Same, nullptr},
    {"sqrt", "sqrt", Kind::Same, nullptr},

    /* --- same semantics, different name --- */
    {"inversesqrt", "inverseSqrt", Kind::Rename, "capital S"},
    {"dFdx", "dpdx", Kind::Rename, "WGSL spells it dpdx; fp_decompiler emits dFdx"},
    {"dFdy", "dpdy", Kind::Rename, nullptr},
    {"texture", "textureSample", Kind::Rename, "explicit sampler argument stays"},
    {"textureLod", "textureSampleLevel", Kind::Rename, nullptr},
    {"textureGrad", "textureSampleGrad", Kind::Rename, nullptr},
    {"textureSize", "textureDimensions", Kind::Rename, nullptr},
    {"floatBitsToUint", "bitcast<u32>", Kind::Rename, "WGSL bitcast is a template"},
    {"uintBitsToFloat", "bitcast<f32>", Kind::Rename, nullptr},

    /* --- packed formats: WGSL has them, under different names --- */
    {"packUnorm2x16", "pack2x16unorm", Kind::Rename, nullptr},
    {"unpackUnorm2x16", "unpack2x16unorm", Kind::Rename, nullptr},
    {"packUnorm4x8", "pack4x8unorm", Kind::Rename, nullptr},
    {"unpackUnorm4x8", "unpack4x8unorm", Kind::Rename, nullptr},
    {"packSnorm4x8", "pack4x8snorm", Kind::Rename, nullptr},
    {"unpackSnorm4x8", "unpack4x8snorm", Kind::Rename, nullptr},
    {"packHalf2x16", "pack2x16float", Kind::Rename, nullptr},
    {"unpackHalf2x16", "unpack2x16float", Kind::Rename, nullptr},

    /* --- comparison builtins: WGSL has NO call form, only operators ---
     * The first version of this table said "same name, returns vecN<bool>", which
     * is wrong: Chromium rejects every one of these calls with "unresolved call
     * target 'lessThan'" (track H measured 28 occurrences across 8/8 menu
     * shaders). The emitter has to write the operator, so the kind is Rewrite. */
    {"equal", nullptr, Kind::Rewrite, "operator ==; there is no equal() in WGSL"},
    {"notEqual", nullptr, Kind::Rewrite, "operator !="},
    {"lessThan", nullptr, Kind::Rewrite, "operator <"},
    {"lessThanEqual", nullptr, Kind::Rewrite, "operator <="},
    {"greaterThan", nullptr, Kind::Rewrite, "operator >"},
    {"greaterThanEqual", nullptr, Kind::Rewrite, "operator >="},
    {"any", "any", Kind::Same, "exists in WGSL and takes vecN<bool>"},
    {"bvec4", "vec4<bool>", Kind::Type, "WGSL has no bvecN"},

    /* --- bvecN <-> uvecN: the bitwise trick does not compile ---
     * GLSL masks four lanes with bvec4(uvec4(r) | uvec4(lt)); WGSL has no
     * conversion between vecN<bool> and vecN<u32> and the OR is already defined
     * on vecN<bool> ("cannot assign 'vec4<u32>' to 'vec4<bool>'", measured). */
    {"uvec4(a_bvec4)", nullptr, Kind::Rewrite, "keep vecN<bool> end to end; | is defined on it"},

    /* --- combined samplers: two bindings, not one --- */
    {"sampler2D", nullptr, Kind::Rewrite, "splits into texture_2d<f32> + sampler"},
    {"sampler1D", nullptr, Kind::Rewrite, "texture_1d<f32> + sampler"},
    {"sampler3D", nullptr, Kind::Rewrite, "texture_3d<f32> + sampler"},
    {"samplerCube", nullptr, Kind::Rewrite, "texture_cube<f32> + sampler"},
    {"sampler2DShadow", nullptr, Kind::Rewrite, "texture_depth_2d + sampler_comparison"},

    /* --- built-in interface blocks and qualifiers that do not exist --- */
    {"gl_PerVertex", nullptr, Kind::Rewrite, "the block disappears; members become @builtin"},
    {"invariant", nullptr, Kind::Rewrite, "no counterpart; the qualifier is dropped"},
    {"gl_PointSize", nullptr, Kind::Rewrite, "@builtin(point_size) is absent in this Chromium build"},
    {"gl_Position", "@builtin(position)", Kind::Type, "a struct member, not a global"},
    {"gl_FragCoord", "@builtin(position)", Kind::Type, "an input member"},
    {"gl_FragDepth", "@builtin(frag_depth)", Kind::Type, "an output member; forces early-z off"},
    {"gl_FrontFacing", "@builtin(front_facing)", Kind::Type, "an input member"},
    {"mod", nullptr, Kind::Rewrite, "the % operator; WGSL has no mod()"},

    /* --- vector constructors: same name, but WGSL needs the element type --- */
    {"vec2", "vec2<f32>", Kind::Type, "constructor infers, declarations do not"},
    {"vec3", "vec3<f32>", Kind::Type, nullptr},
    {"vec4", "vec4<f32>", Kind::Type, nullptr},
    {"ivec4", "vec4<i32>", Kind::Type, nullptr},
    {"uvec4", "vec4<u32>", Kind::Type, nullptr},
    {"float", "f32", Kind::Type, "type name only"},
    {"layout", "var<...> @group @binding", Kind::Type, "uniforms become structs with attributes"},

    /* --- no equivalent at all: the emitter must generate different code --- */
    {"lit", nullptr, Kind::Rewrite, "GLSL lit(); expand to the half-lambert formula"},
    {"isnan", nullptr, Kind::Rewrite, "WGSL has no isnan; use (x != x)"},
};

const Rewrite kRewrites[] = {
    {"lit(x)",
     "clamp(x, 0.0, 1.0) for the diffuse term, with pow() for the specular exponent; "
     "GLSL lit() compresses four terms at once and WGSL has no counterpart"},
    {"isnan(x)",
     "(x != x) -- true for NaN in IEEE, and WGSL guarantees the comparison"},
    {"lessThan(a, b) and the five siblings",
     "a < b, a <= b, a > b, a >= b, a == b, a != b -- operators, and the result is "
     "vecN<bool>; where GLSL wrapped the mask in vec4() to get 0.0/1.0 lanes, WGSL "
     "needs select(vec4<f32>(0.0), vec4<f32>(1.0), mask)"},
    {"bvec4(uvec4(r) | uvec4(lt))",
     "r | (v < vec4<f32>(0.0)) with r declared vec4<bool>; there is no boolean/numeric "
     "vector conversion in WGSL, but | and & are defined on vecN<bool>"},
    {"uniform sampler2D tex0",
     "@group(g) @binding(b) var tex0: texture_2d<f32>; plus a second binding "
     "var smp0: sampler; and textureSample(tex0, smp0, uv). The descriptor layout "
     "doubles per texture unit"},
    {"out gl_PerVertex { invariant vec4 gl_Position; float gl_PointSize; }",
     "a plain output struct with @builtin(position) on gl_Position; invariant and the "
     "block are dropped, and gl_PointSize has no builtin to land in"},
    {"gl_PointSize = x",
     "nothing: @builtin(point_size) is absent from this Chromium build's builtin list, "
     "so an RSX point must be expanded to a quad by the vertex stage instead"},
    {"mod(a, b)",
     "(a - b * floor(a / b)) for the GLSL definition, or the % operator where the "
     "operands are known non-negative"},
};

}  // namespace

const Entry* entries(size_t* count) {
    *count = sizeof kEntries / sizeof kEntries[0];
    return kEntries;
}

const Entry* find(const char* glsl) {
    for (const Entry& e : kEntries) {
        if (std::strcmp(e.glsl, glsl) == 0) return &e;
    }
    return nullptr;
}

const Rewrite* rewrites(size_t* count) {
    *count = sizeof kRewrites / sizeof kRewrites[0];
    return kRewrites;
}

}  // namespace wgsl
}  // namespace gpu
