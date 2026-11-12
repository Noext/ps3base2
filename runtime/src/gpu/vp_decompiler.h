#pragma once

#include <cstdint>
#include <string>

namespace gpu {

struct VertexProgramInfo {
    std::string glsl;
    uint32_t input_mask = 0;
    uint32_t texture_mask = 0;
    bool valid = false;
};

VertexProgramInfo decompile_vertex_program(const uint32_t* ucode, uint32_t start_slot);
/* The same program written for the WebGPU backend: samplers are emitted as
 * separate texture + sampler bindings. A combined sampler compiles to an
 * OpTypeSampledImage variable and naga -- wgpu-native SPIR-V translator --
 * rejects it with "invalid id". The Vulkan entry point is unchanged.
 */
VertexProgramInfo decompile_vertex_program_wgpu(const uint32_t* ucode, uint32_t start_slot);

}
