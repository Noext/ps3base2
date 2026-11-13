/* H4 -- the RSX ring producer, pinned on the host.
 *
 * What this test is for: the transport claims to carry state, vertex/index data,
 * textures and shaders through the ring, in some number of bytes and objects, and
 * to drop (never block, never half-write) when the ring is full. Each of those is
 * a claim a wrong picture would not reveal, so each is measured here.
 *
 * It runs against a SYNTHETIC guest: a 1 MB byte array standing in for guest
 * memory, a fake io->ea map, and a fake texture describer. That is enough because
 * the producer's only inputs are on_method() calls and those three hooks, which is
 * exactly the boundary the wasm build installs real implementations behind.
 */
#include "gpu/rsx_ring.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace gpu::rsxring;

static int failures = 0;
static void check(bool ok, const char* what) {
    if (ok) {
        std::printf("PASS %s\n", what);
    } else {
        std::printf("FAIL %s\n", what);
        failures++;
    }
}

/* ---- the synthetic guest -------------------------------------------------- */
static std::vector<uint8_t> g_guest(1u << 20);
static constexpr uint32_t kGuestIoBase = 0x40100000u;

static uint32_t fake_io_to_ea(uint32_t io) { return kGuestIoBase + (io & 0xFFFFFu); }
static uint8_t* fake_guest_ptr(uint32_t ea) {
    if (ea < kGuestIoBase) return nullptr;
    const uint32_t off = ea - kGuestIoBase;
    if (off >= g_guest.size()) return nullptr;
    return g_guest.data() + off;
}
static bool fake_describe(const uint32_t regs[9], uint32_t* w, uint32_t* h,
                          uint32_t* pitch, uint32_t* src) {
    if (!regs[1] || !regs[2]) return false;
    const uint32_t width = (regs[6] & 0xFFFFu) ? (regs[6] & 0xFFFFu) : 64;
    const uint32_t height = (regs[6] >> 16) ? (regs[6] >> 16) : 64;
    *w = width;
    *h = height;
    *pitch = width * 4;
    *src = *pitch * height;
    return true;
}

/* Reader: pops records and sums what they carried, so the test compares what came
 * OUT with what went in. */
struct Totals {
    uint64_t records = 0;
    uint64_t bytes = 0;
    uint64_t state_words = 0;
    uint64_t vtx = 0;
    uint64_t idx = 0;
    uint64_t tex = 0;
    uint64_t shader = 0;
    uint64_t draws = 0;
    uint64_t clears = 0;
    uint64_t flips = 0;
    uint32_t last_draw_arrays = 0;
    uint32_t last_clear_color = 0;
};

static void drain(Totals& t) {
    const uint8_t* data = poll_bytes();
    for (;;) {
        const uint32_t kind = poll();
        if (!kind) break;
        t.records++;
        const uint32_t size = poll_size();
        t.bytes += size;
        switch (kind) {
        case kStateBatch: t.state_words += size / sizeof(StateWord); break;
        case kBuffer: {
            BufferRecord hdr{};
            std::memcpy(&hdr, static_cast<const void*>(data), sizeof hdr);
            if (hdr.type == kVertex || hdr.type == kInlineVertex) t.vtx += hdr.len;
            else t.idx += hdr.len;
            break;
        }
        case kTexture: {
            TextureRecord hdr{};
            std::memcpy(&hdr, static_cast<const void*>(data), sizeof hdr);
            t.tex += hdr.src_size;
            break;
        }
        case kShader: {
            ShaderRecord hdr{};
            std::memcpy(&hdr, static_cast<const void*>(data), sizeof hdr);
            t.shader += hdr.words * 4u;
            break;
        }
        case kDraw: {
            DrawRecord d{};
            std::memcpy(&d, static_cast<const void*>(data), sizeof d);
            t.draws++;
            uint32_t sum = 0;
            for (uint32_t i = 0; i < 16; i++) sum += d.array_len[i];
            t.last_draw_arrays = sum;
            break;
        }
        case kClear: {
            ClearRecord c{};
            std::memcpy(&c, static_cast<const void*>(data), sizeof c);
            t.clears++;
            t.last_clear_color = c.color;
            break;
        }
        case kFlip: t.flips++; break;
        default: break;
        }
    }
}

int main() {
    Hooks hooks{};
    hooks.io_to_ea = fake_io_to_ea;
    hooks.guest_ptr = fake_guest_ptr;
    hooks.describe_texture = fake_describe;
    install(hooks);
    /* The report is a LOG_INFO line every 1024 methods; this test pushes millions,
     * so leave it off or the PASS lines are buried. */
    set_reporting(false);
    reset_for_test();

    /* Fill the synthetic guest with a known pattern so the copied bytes are
     * checkable, not just counted. */
    for (size_t i = 0; i < g_guest.size(); i++) g_guest[i] = static_cast<uint8_t>(i * 7 + 3);

    Totals t;

    /* ---- 1. state is batched, not one record per method ------------------- */
    for (uint32_t i = 0; i < kStateBatchWords * 2 + 5; i++) on_method(0x0200u + 4u * (i & 0xFFu), i, 0x1000);
    on_drain_end();
    drain(t);
    check(t.records == 3, "257 state methods become 3 batch records, not 257");
    check(t.state_words == kStateBatchWords * 2 + 5, "the batches carry every state word pushed");

    /* ---- 2. a draw carries vertex bytes ---------------------------------- */
    reset_for_test();
    Totals t2;
    /* One array: 8 vertices at a 12-byte stride, offset io 0x100. */
    on_method(0x1740, (12u << 8) | (3u << 4), 0x1000);   /* format: size=3, stride=12 */
    on_method(0x1680, 0x100u, 0x1008);                   /* offset */
    on_method(0x1808, 8u, 0x1010);                       /* begin: triangles */
    on_method(0x1814, 8u << 24 | 0u, 0x1018);            /* draw_arrays first=0 count=8 */
    on_method(0x1808, 0u, 0x1020);                       /* end */
    on_drain_end();
    drain(t2);
    check(t2.draws >= 1, "the DRAW_ARRAYS packet produced a draw record");
    check(t2.vtx == 12u * 8u, "the draw carried stride*count vertex bytes");
    check(t2.last_draw_arrays == 12u * 8u, "the draw record names the bytes it carried");

    /* ---- 3. a texture carries its source bytes ---------------------------- */
    reset_for_test();
    Totals t3;
    on_method(0x1A00, 0, 0);                    /* unit 0 offset */
    on_method(0x1A04, 0x81u, 0);                /* format 0x81 */
    on_method(0x1A18, (64u << 16) | 64u, 0);    /* rect 64x64 */
    on_method(0x1A08, 0x200u, 0);               /* address -> texture complete */
    on_drain_end();
    drain(t3);
    check(t3.tex == 64u * 4u * 64u, "the texture record carried pitch*height source bytes");

    /* ---- 4. a shader carries its ucode ----------------------------------- */
    reset_for_test();
    Totals t4;
    for (uint32_t i = 0; i < 8; i++) on_method(0x0B80u + 4u * i, 0x3F800000u + i, 0);
    on_method(0x1E9C, 0, 0);            /* load position 0 */
    on_method(0x1808, 4u, 0);
    on_method(0x1814, 3u << 24, 0);
    on_drain_end();
    drain(t4);
    check(t4.shader == 1024u * 4u, "the draw carried the whole vertex program when it changed");

    /* ---- 5. clear and flip ------------------------------------------------ */
    reset_for_test();
    Totals t5;
    on_method(0x1D90, 0x11223344u, 0);
    on_method(0x1D94, 3u, 0);
    on_method(0xFEAC, 0, 0x1400);
    on_drain_end();
    drain(t5);
    check(t5.clears == 1, "CLEAR_SURFACE produced exactly one clear record");
    check(t5.last_clear_color == 0x11223344u, "the clear carried the colour register");
    check(t5.flips == 1, "GCM_FLIP_COMMAND produced exactly one flip record");

    /* ---- 6. the ring never half-writes and never blocks ------------------- */
    reset_for_test();
    /* The producer's counters are cumulative over the process, so the delta across
     * this section is what is being asserted, not the absolute value. */
    Stats before{};
    stats(&before);
    /* Push far more than the ring holds. The producer must COUNT the refusals and
     * keep going; a block here would hang the emulation thread.
     * 2 M methods = ~15 600 batch records of 1 KB = ~16 MB against a 4 MB ring,
     * so the ring genuinely fills and the refusal path is exercised. */
    /* Sized from the ring, not guessed: one batch record per 128 methods at 1 KB,
     * so 8 M methods push ~64 MB at a 16 MB ring and the refusal path must fire. */
    for (uint32_t i = 0; i < 8000000u; i++) on_method(0x0200u + 4u * (i & 0x7Fu), i, 0);
    on_drain_end();
    Stats after{};
    stats(&after);
    std::printf("INFO refused=%llu refused_bytes=%llu pushed_bytes=%llu\n",
                (unsigned long long)(after.refused - before.refused),
                (unsigned long long)(after.refused_bytes - before.refused_bytes),
                (unsigned long long)(after.bytes - before.bytes));
    check(after.refused - before.refused > 0, "a full ring is counted as refused, not silently ignored");
    check(after.refused_bytes - before.refused_bytes > 0, "the refused bytes are counted too");

    /* Everything that was ACCEPTED must come out whole: the record sizes the
     * consumer sees must add up to the bytes the producer says it pushed. */
    Totals t6;
    drain(t6);
    Stats end{};
    stats(&end);
    std::printf("INFO consumer_bytes=%llu producer_bytes=%llu records=%llu pops=%u used=%u\n",
                (unsigned long long)t6.bytes, (unsigned long long)(end.bytes - before.bytes),
                (unsigned long long)t6.records, control_word(2), control_word(0));
    check(t6.bytes == end.bytes - before.bytes, "the consumer read exactly the bytes the producer pushed");

    /* ---- 7. ordering: state precedes the record that depends on it -------- */
    reset_for_test();
    on_method(0x1D90, 0xDEADBEEFu, 0);
    on_method(0x1D94, 1u, 0);
    on_drain_end();
    const uint32_t first = poll();
    check(first == kStateBatch, "the clear's colour register was published before the clear");

    std::printf("rsx_ring: %d failures\n", failures);
    return failures ? 1 : 0;
}