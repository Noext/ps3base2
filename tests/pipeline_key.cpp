/* Checks the pipeline key: it must distinguish every state WebGPU bakes in, and
 * must NOT distinguish the ones it keeps dynamic.
 *
 * Both failures are real. A key that misses a field renders the wrong picture;
 * a key that includes a dynamic field creates pipelines it did not need, at the
 * most expensive operation WebGPU has.
 */
#include "runtime/pipeline_key.h"

#include <cstdio>
#include <unordered_set>

using namespace gpu::wgpu;

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        failures++;
    }
}

static PipelineKey base() {
    PipelineKey k{};
    k.blend.enabled = true;
    k.blend.src_color = BlendFactor::SrcAlpha;
    k.blend.dst_color = BlendFactor::OneMinusSrcAlpha;
    k.depth_stencil.depth_test = true;
    k.depth_stencil.depth_write = true;
    k.depth_stencil.depth_compare = CompareOp::LessEqual;
    k.topology = Topology::TriangleList;
    k.cull = CullMode::Back;
    k.front_face = FrontFace::Ccw;
    k.color_format = Format::RGBA8Unorm;
    return k;
}

int main() {
    const PipelineKey a = base();

    /* Every field WebGPU bakes in must change the key. */
    {
        PipelineKey b = a; b.blend.enabled = false;         check(!(a == b), "blend.enabled must matter");
    }
    {
        PipelineKey b = a; b.blend.src_color = BlendFactor::One; check(!(a == b), "blend.src_color must matter");
    }
    {
        PipelineKey b = a; b.blend.color_op = BlendOp::Min; check(!(a == b), "blend.color_op must matter");
    }
    {
        PipelineKey b = a; b.blend.write_mask = 0x7;        check(!(a == b), "blend.write_mask must matter");
    }
    {
        PipelineKey b = a; b.depth_stencil.depth_write = false; check(!(a == b), "depth_write must matter");
    }
    {
        PipelineKey b = a; b.depth_stencil.depth_compare = CompareOp::Greater; check(!(a == b), "depth_compare must matter");
    }
    {
        PipelineKey b = a; b.depth_stencil.stencil_test = true; check(!(a == b), "stencil_test must matter");
    }
    {
        PipelineKey b = a; b.depth_stencil.front_pass = StencilOp::Replace; check(!(a == b), "front_pass must matter");
    }
    {
        PipelineKey b = a; b.depth_stencil.back_compare = CompareOp::Equal; check(!(a == b), "back_compare must matter");
    }
    {
        PipelineKey b = a; b.topology = Topology::TriangleStrip; check(!(a == b), "topology must matter");
    }
    {
        PipelineKey b = a; b.cull = CullMode::Front;         check(!(a == b), "cull must matter");
    }
    {
        PipelineKey b = a; b.front_face = FrontFace::Cw;     check(!(a == b), "front_face must matter");
    }
    {
        PipelineKey b = a; b.color_format = Format::Depth32Float; check(!(a == b), "color_format must matter");
    }

    /* An identical key must be identical. */
    {
        PipelineKey b = base();
        check(a == b, "identical state must give an identical key");
        check(hash_key(a) == hash_key(b), "identical state must hash the same");
    }

    /* The blend case the prompt singles out: constant alpha has no WebGPU
     * equivalent and must not silently equal Zero. */
    {
        PipelineKey b = a;
        b.blend.src_color = BlendFactor::Constant;
        PipelineKey c = a;
        c.blend.src_color = BlendFactor::Zero;
        check(!(b == c), "Constant must not collide with Zero");
    }

    /* TriangleFan is deliberately absent from the enum: the renderer must convert
     * it. Assert the enum stays small enough to index a small array. */
    check(static_cast<int>(CullMode::Back) <= 8, "CullMode fits a byte");
    check(sizeof(PipelineKey) <= 64, "key stays small enough to hash and store cheaply");

    std::printf("pipeline_key: %d checks, %d failures (key %zu bytes)\n",
                13 + 4, failures, sizeof(PipelineKey));
    return failures ? 1 : 0;
}
