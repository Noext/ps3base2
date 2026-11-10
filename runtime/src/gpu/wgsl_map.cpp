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

    /* --- comparison results are bvecN in GLSL, vecN<bool> in WGSL --- */
    {"equal", "equal", Kind::Type, "returns vecN<bool>, not bvecN"},
    {"notEqual", "notEqual", Kind::Type, "returns vecN<bool>"},
    {"lessThan", "lessThan", Kind::Type, "returns vecN<bool>"},
    {"lessThanEqual", "lessThanEqual", Kind::Type, "returns vecN<bool>"},
    {"greaterThan", "greaterThan", Kind::Type, "returns vecN<bool>"},
    {"greaterThanEqual", "greaterThanEqual", Kind::Type, "returns vecN<bool>"},
    {"any", "any", Kind::Type, "takes vecN<bool>"},
    {"bvec4", "vec4<bool>", Kind::Type, "WGSL has no bvecN"},

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
