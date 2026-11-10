#pragma once

/* GLSL -> WGSL for the vocabulary the two RSX shader decompilers actually emit.
 *
 * The table was built from the decompilers, not from the GLSL spec: every name
 * below was found by scanning the string literals vp_decompiler.cpp and
 * fp_decompiler.cpp build their shader from. Sixty-four identifiers appear; this
 * header classifies the ones whose WGSL form is not a straight copy.
 *
 * Four kinds, because the port must handle them differently:
 *   Same        identical spelling, copy the call
 *   Rename      same semantics, different name
 *   Rewrite     no equivalent call; the emitter must generate different code
 *   Type        a type or qualifier, not a call: the declaration itself changes
 */
#include <cstddef>

namespace gpu {
namespace wgsl {

enum class Kind { Same, Rename, Rewrite, Type };

struct Entry {
    const char* glsl;
    const char* wgsl;   /* nullptr for Rewrite: there is no target name */
    Kind kind;
    const char* note;
};

/* The full table. Sorted by GLSL name so a reader can find one. */
const Entry* entries(size_t* count);

/* Look one up; nullptr when the name is not in the table (a user-defined helper
 * such as fogc() or divsq(), which the emitter also writes and must port by hand). */
const Entry* find(const char* glsl);

/* Names GLSL has and WGSL does not, where the emitter must produce inline code.
 * Kept separate from the table because the replacement depends on the operands,
 * not on a name. */
struct Rewrite {
    const char* glsl;
    const char* recipe;
};

const Rewrite* rewrites(size_t* count);

}  // namespace wgsl
}  // namespace gpu
