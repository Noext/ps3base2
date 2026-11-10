/* The WGSL table must cover the vocabulary the decompilers emit, and must not
 * pretend a name has an equivalent when it does not.
 *
 * The failure this guards against is a "helpful" mapping that turns a GLSL built-
 * in into something that looks like WGSL and silently computes another value --
 * a wrong picture is worse than a build error.
 */
#include "runtime/wgsl_map.h"

#include <cstdio>
#include <cstring>

using namespace gpu::wgsl;

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        failures++;
    }
}

int main() {
    size_t n = 0;
    const Entry* e = entries(&n);
    check(n > 40, "the table covers more than 40 names");
    check(e != nullptr, "entries() returns the table");

    /* Every Rewrite entry must have no target name: claiming one would hide the
     * fact that the emitter has to generate different code. */
    for (size_t i = 0; i < n; i++) {
        if (e[i].kind == Kind::Rewrite) {
            check(e[i].wgsl == nullptr, "a Rewrite entry has no WGSL name");
        } else {
            check(e[i].wgsl != nullptr, "a non-Rewrite entry has a WGSL name");
        }
    }

    /* The names the decompilers actually emit and whose form is not a straight
     * copy. These are the ones a wrong mapping would break. */
    const char* must_rename[] = {
        "inversesqrt", "dFdx", "dFdy", "texture", "textureLod", "textureGrad",
        "textureSize", "floatBitsToUint", "uintBitsToFloat",
    };
    for (const char* name : must_rename) {
        const Entry* f = find(name);
        check(f != nullptr, "a renamed builtin is in the table");
        if (f) {
            check(f->kind == Kind::Rename, "it is classified Rename");
            check(f->wgsl && std::strcmp(f->glsl, f->wgsl) != 0, "its WGSL name differs from the GLSL one");
        }
    }

    /* The two that have no equivalent must say so. */
    {
        const Entry* f = find("lit");
        check(f && f->kind == Kind::Rewrite && f->wgsl == nullptr, "lit has no equivalent");
        const Entry* g = find("isnan");
        check(g && g->kind == Kind::Rewrite && g->wgsl == nullptr, "isnan has no equivalent");
    }

    /* bvec4 exists in GLSL and not in WGSL. */
    {
        const Entry* f = find("bvec4");
        check(f && f->kind == Kind::Type, "bvec4 is a type change");
        check(f && std::strstr(f->wgsl, "bool") != nullptr, "bvec4 maps to a bool vector");
    }

    /* Identity for the ones that do transfer unchanged. */
    {
        const char* same[] = {"abs", "clamp", "mix", "dot", "fma", "fract"};
        for (const char* name : same) {
            const Entry* f = find(name);
            check(f && f->kind == Kind::Same, "a shared builtin is classified Same");
            check(f && std::strcmp(f->glsl, f->wgsl) == 0, "its spelling is unchanged");
        }
    }

    /* Unknown names return null rather than guessing: user helpers such as fogc()
     * are written by the emitter and must be ported by hand. */
    check(find("fogc") == nullptr, "a user helper is not in the table");
    check(find("nrm") == nullptr, "another user helper is not in the table");

    size_t rn = 0;
    const Rewrite* r = rewrites(&rn);
    check(rn == 2, "two names need a recipe");
    check(r && r[0].glsl && r[0].recipe, "each recipe names its GLSL form and its replacement");

    std::printf("wgsl_map: %zu entries, %zu rewrites, %d failures\n", n, rn, failures);
    return failures ? 1 : 0;
}
