/* What the pipeline cache actually costs on a realistic RSX frame.
 *
 * The prompt asks to "count and report: pipelines created over 60 s of play,
 * CPU time of the render thread". The CPU time needs the port; the pipeline count
 * does not -- it is a property of the state sequence, which RSX produces from the
 * guest command stream and which can be sampled without a device.
 *
 * The sequence below is not invented from nothing. Minecraft's RSX stream, as
 * measured in the boot logs, is dominated by one primitive (draw share 0.1% of
 * packets but 2.9M draws), so a frame is hundreds of near-identical draws with a
 * few state changes between passes: world, then UI, then a clear.
 */
#include "runtime/pipeline_cache.h"

#include <cstdio>
#include <cstdint>
#include <cstdlib>

using namespace gpu::wgpu;

/* A small deterministic PRNG so the run is reproducible byte for byte. */
struct Rng {
    uint64_t s = 0x9E3779B97F4A7C15ull;
    uint32_t next() {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        return static_cast<uint32_t>(s >> 32);
    }
    uint32_t below(uint32_t n) { return next() % n; }
};

static PipelineKey make(uint32_t depth_compare, uint32_t blend_src, uint32_t cull, uint32_t fmt) {
    PipelineKey k{};
    k.depth_stencil.depth_test = true;
    k.depth_stencil.depth_write = true;
    k.depth_stencil.depth_compare = static_cast<CompareOp>(depth_compare % 8);
    k.blend.enabled = true;
    k.blend.src_color = static_cast<BlendFactor>(blend_src % 12);
    k.blend.dst_color = BlendFactor::OneMinusSrcAlpha;
    k.cull = static_cast<CullMode>(cull % 3);
    k.topology = Topology::TriangleList;
    k.color_format = static_cast<Format>(fmt % 8);
    return k;
}

int main(int argc, char** argv) {
    constexpr int kFrames = 3600;   /* 60 s at 60 Hz */
    constexpr int kDrawsPerFrame = 400;
    /* How often the state changes. 64 is the pessimistic default; the sweep below
     * asks what happens if RSX is far messier than the boot logs suggest. */
    const int change_every = argc > 1 ? std::atoi(argv[1]) : 64;

    PipelineCache cache(512);
    Rng rng;

    for (int f = 0; f < kFrames; f++) {
        for (int d = 0; d < kDrawsPerFrame; d++) {
            /* Most draws in a frame repeat the previous state; every so often a
             * pass boundary changes it. 1 in 64 is a deliberately pessimistic
             * guess -- the boot logs show far fewer changes per draw. */
            const bool change = (d % change_every) == 0;
            static uint32_t dc = 6, bs = 4, cu = 2, fm = 0;
            if (change) {
                /* Draw from the full ranges make() accepts, so the state space is
                 * really explored. The earlier version clamped dc to 4 values and
                 * the sweep then reported the same 180 misses at every frequency,
                 * which was the harness lying, not the cache being good. */
                dc = rng.below(8);
                bs = rng.below(12);
                cu = rng.below(3);
                fm = rng.below(8);
            }
            const PipelineKey k = make(dc, bs, cu, fm);
            if (cache.find(k) == 0) {
                /* The caller would create the pipeline here. We count, not create:
                 * a fake handle is enough to exercise the cache. */
                cache.insert(k, cache.stats().misses + 1);
            }
        }
        cache.end_frame();
    }

    const PipelineCacheStats& s = cache.stats();
    std::printf("pipeline cost over %d frames (%d draws each, %d draws total, change every %d)\n",
                kFrames, kDrawsPerFrame, kFrames * kDrawsPerFrame, change_every);
    std::printf("  lookups      %llu\n", (unsigned long long)s.lookups);
    std::printf("  hits         %llu\n", (unsigned long long)s.hits);
    std::printf("  MISSES       %llu   <- pipelines created\n", (unsigned long long)s.misses);
    std::printf("  hit rate     %.4f\n", s.hit_rate());
    std::printf("  evictions    %llu\n", (unsigned long long)s.evictions);
    std::printf("  thrash frames %llu\n", (unsigned long long)s.thrash_frames);
    std::printf("  live entries %zu of %zu\n", cache.size(), cache.capacity());

    /* The whole point of the cache: after warm-up the miss count must stop
     * growing with the frame count. A miss per draw would mean the cache is
     * useless and the port cannot meet frame time. */
    const bool bounded = s.misses < 200;
    std::printf("\nverdict: %.2f pipelines per frame average, %s\n",
                static_cast<double>(s.misses) / kFrames,
                bounded ? "BOUNDED -- the cache holds an RSX frame" : "UNBOUNDED -- thrashing");
    return bounded ? 0 : 1;
}
