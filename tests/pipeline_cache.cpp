/* The cache is what stops the frame loop from creating a pipeline per draw.
 * These checks pin the behaviour that matters: a hit does not create, a miss
 * reports 0 so the caller must create, LRU keeps the hot set alive, and the
 * stats actually count what the prompt asks to report.
 */
#include "runtime/pipeline_cache.h"

#include <cstdio>

using namespace gpu::wgpu;

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        failures++;
    }
}

static PipelineKey key_with(BlendFactor f) {
    PipelineKey k{};
    k.blend.enabled = true;
    k.blend.src_color = f;
    k.blend.dst_color = BlendFactor::OneMinusSrcAlpha;
    k.topology = Topology::TriangleList;
    k.color_format = Format::RGBA8Unorm;
    return k;
}

int main() {
    /* A miss returns 0 so the caller knows to create; the insert then hits. */
    {
        PipelineCache c(16);
        const PipelineKey k = key_with(BlendFactor::SrcAlpha);
        check(c.find(k) == 0, "first lookup must miss and return 0");
        c.insert(k, 42);
        check(c.find(k) == 42, "after insert the same key must hit");
        check(c.stats().lookups == 2, "two lookups counted");
        check(c.stats().hits == 1, "one hit counted");
        check(c.stats().misses == 1, "one miss counted");
        check(c.stats().hit_rate() > 0.49 && c.stats().hit_rate() < 0.51, "hit rate is half");
    }

    /* Distinct states must not collide. */
    {
        PipelineCache c(16);
        c.insert(key_with(BlendFactor::SrcAlpha), 1);
        c.insert(key_with(BlendFactor::One), 2);
        check(c.find(key_with(BlendFactor::SrcAlpha)) == 1, "first state keeps its handle");
        check(c.find(key_with(BlendFactor::One)) == 2, "second state keeps its handle");
        check(c.size() == 2, "two live entries");
    }

    /* A zero handle must not enter the cache: that would make a failed creation
     * look like a hit on the next draw. */
    {
        PipelineCache c(4);
        const PipelineKey k = key_with(BlendFactor::DstAlpha);
        c.insert(k, 0);
        check(c.find(k) == 0, "a zero handle is never cached as a hit");
        check(c.size() == 0, "the cache stayed empty");
    }

    /* LRU: fill past capacity, the hot entry survives, the cold one is evicted. */
    {
        PipelineCache c(4);
        PipelineKey hot = key_with(BlendFactor::SrcAlpha);
        c.insert(hot, 100);
        for (int i = 0; i < 3; i++) {
            PipelineKey k = key_with(BlendFactor::Constant);
            k.blend.src_alpha = static_cast<BlendFactor>((i + 1) % 6);
            c.insert(k, 200 + i);
        }
        c.find(hot);  /* refresh: hot becomes the most recently used */
        /* Three more inserts into a full 4-slot cache evict exactly the three
         * entries older than hot, leaving it alive. A fourth would evict hot --
         * which is why this inserts three and not four. */
        for (int i = 0; i < 3; i++) {
            PipelineKey k = key_with(BlendFactor::DstColor);
            k.blend.src_alpha = static_cast<BlendFactor>((i + 2) % 6);
            c.insert(k, 300 + i);
        }
        check(c.stats().evictions == 3, "three evictions for three inserts into a full cache");
        check(c.size() <= c.capacity(), "never exceeds capacity");
        check(c.find(hot) == 100, "the recently used entry survived three evictions");
    }

    /* Thrash accounting: a frame with more misses than capacity is a thrash. */
    {
        PipelineCache c(2);
        c.find(key_with(BlendFactor::SrcAlpha));
        c.find(key_with(BlendFactor::One));
        c.find(key_with(BlendFactor::Zero));
        c.end_frame();
        check(c.stats().thrash_frames == 1, "three misses into a 2-slot cache is a thrashing frame");
    }

    std::printf("pipeline_cache: 15 checks, %d failures\n", failures);
    return failures ? 1 : 0;
}
