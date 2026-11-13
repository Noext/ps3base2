#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gpu {

enum class ShaderStage { Vertex, Fragment, Compute };

bool compile_glsl(ShaderStage stage, const std::string& source, std::vector<uint32_t>& spirv, std::string& log);
bool load_cached_glsl(ShaderStage stage, const std::string& source, std::vector<uint32_t>& spirv);
bool compile_glsl_uncached(ShaderStage stage, const std::string& source, std::vector<uint32_t>& spirv, std::string& log);

/* A second target, for the WebGPU backend only.
 *
 * wgpu-native translates SPIR-V with naga, and naga rejects two things the
 * Vulkan-targeted build produces. Both are measured, not assumed
 * (docs/explore/H2/RESULTS.md, section 2):
 *
 *   - SPIR-V 1.6 encodes `discard` as OpTerminateInvocation, which naga does not
 *     implement ("unsupported instruction TerminateInvocation"). SPIR-V 1.5
 *     emits OpKill, which it accepts. So this path targets 1.5.
 *   - A combined `sampler2D` becomes one OpTypeSampledImage variable, which
 *     naga rejects ("invalid id"). A separate texture2D + sampler pair compiles
 *     to the OpImage + OpTypeSampler shape it does implement. The caller must
 *     therefore have split the declarations and the texture() calls, which is
 *     what the "wgpu" flag on the decompilers does.
 *
 * The Vulkan path is not touched by any of this: compile_glsl above keeps
 * emitting 1.6 with combined samplers, unchanged.
 */
bool compile_glsl_wgpu(ShaderStage stage, const std::string& source, std::vector<uint32_t>& spirv, std::string& log);

}
