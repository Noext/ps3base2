#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gpu {

struct FragmentProgramKey {
    uint32_t control = 0;
    uint32_t texture_dims = 0;
    uint32_t unnormalized = 0;
    uint32_t shadow = 0;
    uint32_t mrt_count = 1;
    uint64_t hash = 0;   /* shader hash, so a debug override can be gated */
};

struct FragmentProgramInfo {
    std::string glsl;
    std::vector<uint32_t> literal_slots;
    uint32_t texture_mask = 0;
    uint32_t length_slots = 0;
    uint32_t start_slot = 0;
    uint64_t hash = 0;
    bool valid = false;
};

using FpWordReader = std::function<uint32_t(uint32_t word_index)>;

FragmentProgramInfo analyse_fragment_program(const FpWordReader& read);
FragmentProgramInfo decompile_fragment_program(const FpWordReader& read, const FragmentProgramKey& key);

/* The same program, but the GLSL is written for the WebGPU backend: samplers are
 * emitted as separate texture + sampler bindings instead of combined ones.
 *
 * The reason is measured, not stylistic. A combined sampler compiles to an
 * OpTypeSampledImage variable, and naga -- which wgpu-native uses to translate
 * SPIR-V -- rejects it with "invalid id". A separate texture2D plus a sampler
 * call produces the OpImage/OpTypeSampler shape naga does implement. The Vulkan
 * path is untouched: decompile_fragment_program still emits the combined form.
 */
FragmentProgramInfo decompile_fragment_program_wgpu(const FpWordReader& read, const FragmentProgramKey& key);

}
