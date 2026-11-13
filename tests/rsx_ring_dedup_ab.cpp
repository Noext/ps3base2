/* S2 -- the deduplication A/B, on the host, over an IDENTICAL method stream.
 *
 * Why this exists separate from tests/rsx_ring.cpp: the page run dies at ~13 s on
 * an unrelated guest abort (the MSSAsync audio thread, named in R3 section 5.4),
 * so a page A/B compares two different amounts of work. Here the same synthetic
 * guest is replayed twice, once per build, and the only difference between the two
 * binaries is -DPS3BASE_RSX_NODEDUP=1.
 *
 * What it replays: the shape the logo actually shows -- a title that re-selects a
 * handful of textures and shaders and re-points the same vertex arrays, frame after
 * frame. Each "frame" re-writes the same texture unit registers, alternates two
 * vertex programs, and issues three draws over the same vertex block.
 *
 * Everything it prints is a count, so the numbers can be diffed directly.
 */
#include "gpu/rsx_ring.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace gpu::rsxring;

static std::vector<uint8_t> g_guest(4u << 20);
static constexpr uint32_t kGuestIoBase = 0x40100000u;
static constexpr uint32_t kFrames = 240;

static uint32_t fake_io_to_ea(uint32_t io) { return kGuestIoBase + (io & 0xFFFFFu); }
static uint8_t* fake_guest_ptr(uint32_t ea) {
    if (ea < kGuestIoBase) return nullptr;
    const uint32_t off = ea - kGuestIoBase;
    if (off >= g_guest.size()) return nullptr;
    return g_guest.data() + off;
}
/* A 512x512 RGBA image at io 0x200, the size the logo's textures actually are. */
static bool fake_describe(const uint32_t regs[9], uint32_t* w, uint32_t* h,
                          uint32_t* pitch, uint32_t* src) {
    (void)regs;
    *w = 512; *h = 512; *pitch = 512 * 4; *src = 512u * 512u * 4u;
    return true;
}

/* NODRAIN=1 replays the same stream with NO consumer at all: that is the exact
 * configuration R3 measured refused=1160458 in, and the one the S2 objective is
 * stated against. */
static bool g_drain = true;
static uint32_t g_peak_used = 0;
static void drain_all() {
    if (!g_drain) return;
    for (;;) {
        const uint32_t u = control_word(0);
        if (u > g_peak_used) g_peak_used = u;
        const uint32_t k = poll();
        if (!k) break;
    }
}

int main() {
    g_drain = std::getenv("NODRAIN") == nullptr;
    Hooks hooks{};
    hooks.io_to_ea = fake_io_to_ea;
    hooks.guest_ptr = fake_guest_ptr;
    hooks.describe_texture = fake_describe;
    install(hooks);
    set_reporting(false);
    reset_for_test();
    for (size_t i = 0; i < g_guest.size(); i++) g_guest[i] = static_cast<uint8_t>(i * 31 + 7);

    /* Two distinct vertex programs, two distinct textures: the minimum a frame
     * needs to be a frame. */
    for (uint32_t f = 0; f < kFrames; f++) {
        const uint32_t prog = (f & 1u) ? 0xBBBBBBBBu : 0xAAAAAAAAu;

        /* Unit 0: the SAME image every frame, at the same address. */
        on_method(0x1A00, 0, 0);
        on_method(0x1A04, 0x85u, 0);
        on_method(0x1A18, (512u << 16) | 512u, 0);
        on_method(0x1A08, 0x200u, 0);

        /* The vertex program, re-uploaded every frame (a title does exactly this). */
        for (uint32_t i = 0; i < 1024; i++) on_method(0x0B80u + 4u * (i & 3u), prog + i, 0);
        on_method(0x1E9C, 0, 0);

        /* Three draws, same vertex block, same format. */
        for (uint32_t d = 0; d < 3; d++) {
            on_method(0x1740, (12u << 8) | (3u << 4), 0);
            on_method(0x1680, 0x100u, 0);
            on_method(0x1808, 8u, 0);
            on_method(0x1814, 8u << 24, 0);
            on_method(0x1808, 0u, 0);
        }
        /* A clear per frame, as the title does. */
        on_method(0x1D90, 0x11223344u, 0);
        on_method(0x1D94, 3u, 0);
        { const uint32_t u = control_word(0); if (u > g_peak_used) g_peak_used = u; }
        drain_all();
    }
    on_drain_end();
    drain_all();

    Stats s{};
    stats(&s);
    const uint32_t capacity = ring_capacity();
    std::printf("FRAMES %u\n", kFrames);
    std::printf("records %llu\n", (unsigned long long)s.records);
    std::printf("bytes %llu\n", (unsigned long long)s.bytes);
    std::printf("draws %llu\n", (unsigned long long)s.draws);
    std::printf("clears %llu\n", (unsigned long long)s.clears);
    std::printf("tex_bytes %llu\n", (unsigned long long)s.tex_bytes);
    std::printf("tex_objects %llu\n", (unsigned long long)s.tex_objects);
    std::printf("shader_bytes %llu\n", (unsigned long long)s.shader_bytes);
    std::printf("shader_objects %llu\n", (unsigned long long)s.shader_objects);
    std::printf("vtx_bytes %llu\n", (unsigned long long)s.vtx_bytes);
    std::printf("refs %llu\n", (unsigned long long)s.refs);
    std::printf("distinct_objects %llu\n", (unsigned long long)s.distinct_objects);
    std::printf("bytes_saved %llu\n", (unsigned long long)s.bytes_saved);
    std::printf("shader_alternations %llu\n", (unsigned long long)s.shader_alternations);
    std::printf("refused %llu\n", (unsigned long long)s.refused);
    std::printf("refused_bytes %llu\n", (unsigned long long)s.refused_bytes);
    std::printf("bytes_per_draw %.1f\n", s.draws ? double(s.bytes) / double(s.draws) : 0.0);
    std::printf("bytes_per_frame %.1f\n", double(s.bytes) / double(kFrames));
    std::printf("ring_capacity %u\n", capacity);
    std::printf("peak_ring_used %u\n", g_peak_used);
    std::printf("peak_pct_of_capacity %.1f\n",
                capacity ? 100.0 * double(g_peak_used) / double(capacity) : 0.0);
#if defined(PS3BASE_RSX_NODEDUP) && PS3BASE_RSX_NODEDUP
    std::printf("variant NODEDUP\n");
#else
    std::printf("variant DEDUP\n");
#endif
    return 0;
}
