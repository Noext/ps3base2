#pragma once

/* Direct RSX -> WGSL emission.
 *
 * The decompilers (vp_decompiler.cpp, fp_decompiler.cpp) build a GLSL program
 * text and shader_compiler.cpp hands it to glslang for SPIR-V. Both links are
 * wrong for the wasm target: glslang is a host library with no wasm build here,
 * and WebGPU consumes WGSL, not SPIR-V. This module walks the same RSX
 * instruction stream and writes WGSL, with no GLSL and no SPIR-V in between.
 *
 * The decode is deliberately a separate copy from the decompilers rather than a
 * shared parameterised backend: their expression builders are GLSL string
 * literals and their assemble() emits GLSL declarations, so a "backend" flag
 * would have touched every one of the ~450 string sites anyway. What is shared
 * is the *contract* -- same opcode table, same masks, same slot discovery, and
 * the same FragmentProgramKey the renderer derives at draw time.
 *
 * The structural changes the GLSL form cannot express, all measured by track H:
 *   - texture and sampler are two bindings, not one combined sampler;
 *   - the gl_PerVertex block, invariant, gl_PointSize and bvec4 have no WGSL
 *     counterpart, so the declarations differ, not only the calls.
 */
#include <cstdint>
#include <functional>
#include <string>

namespace gpu {

struct FragmentProgramKey;

using FpWordReader = std::function<uint32_t(uint32_t word_index)>;

struct WgslShader {
    std::string source;
    uint32_t input_mask = 0;
    uint32_t texture_mask = 0;
    uint64_t hash = 0;
    uint32_t literal_count = 0;
    bool valid = false;
};

/* Slot discovery for a vertex program is the same as the decompiler's: linear
 * from the entry, bounded by the table, stop at the end bit. Returns invalid
 * when the guest data cannot be walked, rather than emitting a partial program. */
WgslShader emit_vertex_wgsl(const uint32_t* ucode, uint32_t start_slot);

/* Fragment: the key carries the control bits, the per-unit texture dimensions,
 * the unnormalized and shadow masks and the MRT count, exactly as the renderer
 * derives them at draw time. */
WgslShader emit_fragment_wgsl(const FpWordReader& read, const FragmentProgramKey& key);

}  // namespace gpu
