/* Piste C -- a synthetic NV4097 FIFO, executed by the runtime's own FIFO
 * reader in WebAssembly, with no game and no disc.
 *
 * Nothing here comes from the title. The packets are built here, word for word,
 * in the format the walker decodes (runtime/src/lv2/rsx.cpp:598-600: the packet
 * word is a host-order u32, and every argument is stored big-endian by
 * vm::write32 -- exactly how a guest's command buffer lies in memory). Every
 * register number is RPCS3's.
 *
 * It proves four things in order:
 *   1. NV406E_SET_REFERENCE (0x0050) lands in the DMA control block at +0x48,
 *      where sys_rsx_context_allocate put it (refs/rpcs3/.../nv406e.cpp:16-25:
 *      set_reference writes dma.ref, not the FIFO).
 *   2. NV4097_BACK_END_WRITE_SEMAPHORE_RELEASE (0x1D70) writes the label, after
 *      swapping bytes 1 and 3 (refs/.../rsx_methods.cpp nv4097::back_end_write_
 *      semaphore_release; runtime/src/lv2/rsx.cpp:383-389).
 *   3. The label ADVANCES: a second release with a different value is observed at
 *      the same address, which is the change the guest's L_2da590 loop waits on.
 *   4. NV4097_CLEAR_SURFACE (0x1D94) reaches the command interpreter: the clear
 *      counter moves and the stub's CPU framebuffer holds the cleared colour in
 *      all 256x256 pixels, pinned to the FNV-1a hash the stub publishes. A real
 *      PRESENT needs piste G; that is reported as not measured, not faked.
 *
 * The guest's side of the test is the same loop the title runs at L_2da590:
 * read the label, compare with what the FIFO should have written, sleep 30 us and
 * retry. It terminates only if the FIFO actually ran.
 */
#include "ppu/ops.h"
#include "runtime/kernel.h"
#include "runtime/log.h"
#include "runtime/memory.h"

#include <chrono>
#include <cstdio>
#include <cstring>

#include "runtime/hostsleep.h"
#include "runtime/reservation.h"

namespace gpu {
namespace rsx {
/* Published by the no-display build (runtime/src/gpu/rsx_no_display.cpp:294,
 * :329). Declared here because no header exports them; that is the one gap this
 * test works around rather than fills (see RESULTS.md, demandes). */
void no_display_stats(uint64_t* commands, uint64_t* draws, uint64_t* clears,
                      uint64_t* vp_words, uint64_t* const_writes);
void framebuffer_stats(uint64_t* tris, uint64_t* verts, uint32_t* hash, uint32_t* distinct);
}  // namespace rsx
}  // namespace gpu

namespace lv2 {
uint64_t rsx_flip_count();
}

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_fail++;
}

/* Guest memory is big-endian. vm::write32 stores the swapped word, which is what
 * puts a command word in the buffer the way a guest writes it; the walker's
 * __builtin_bswap32 turns it back into the host value it decodes. */
void fifo_word(uint32_t ea, uint32_t v) { vm::write32(ea, v); }
uint32_t packet(uint32_t method, uint32_t count) { return (count << 18) | method; }

ppu::Context g_ctx;

int64_t lv2_call(uint32_t number, uint64_t a0 = 0, uint64_t a1 = 0, uint64_t a2 = 0,
                 uint64_t a3 = 0, uint64_t a4 = 0) {
    std::memset(&g_ctx, 0, sizeof g_ctx);
    g_ctx.gpr[3] = a0;
    g_ctx.gpr[4] = a1;
    g_ctx.gpr[5] = a2;
    g_ctx.gpr[6] = a3;
    g_ctx.gpr[7] = a4;
    g_ctx.gpr[11] = number;
    ppu::syscall(g_ctx, vm::g_base);
    return static_cast<int64_t>(g_ctx.gpr[3]);
}

/* The FNV-1a the no-display stub runs over its 256x256 CPU framebuffer
 * (rsx_no_display.cpp fb_hash), recomputed here so the content is pinned to a
 * value instead of merely reported. */
uint32_t fb_hash_of(uint32_t pix) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < 256u * 256u; i++) {
        h ^= pix;
        h *= 16777619u;
    }
    return h;
}

/* The stub's clear-colour transform (rsx_no_display.cpp fb_clear). */
uint32_t clear_pixel(uint32_t rgba) {
    return (rgba & 0xFF00FF00u) | ((rgba >> 16) & 0xFFu) | ((rgba & 0xFFu) << 16);
}

/* The release's byte swap (runtime/src/lv2/rsx.cpp:387). */
uint32_t release_value(uint32_t arg) {
    return (arg & 0xFF00FF00u) | ((arg & 0xFFu) << 16) | ((arg >> 16) & 0xFFu);
}

/* Poll a predicate until it holds or the budget runs out. The runtime's flip and
 * clear effects are asynchronous -- the VBLANK thread finishes a queued flip at
 * the next tick, because that is when a real display would have presented -- so a
 * single read right after publishing the buffer is a false negative waiting to
 * happen. */
template <class Pred>
bool wait_until(Pred pred, int budget_ms) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(budget_ms);
    for (;;) {
        if (pred()) return true;
        if (std::chrono::steady_clock::now() >= end) return false;
        host::sleep_us(500);
    }
}

}  // namespace

int main() {
    std::printf("== piste C: synthetic NV4097 FIFO through the runtime FIFO reader (wasm)\n");

    if (!vm::init()) { std::printf("FAIL vm::init\n"); return 2; }
    std::printf("[setup] vm::init ok\n");

    /* Guest scratch for the syscalls' out-parameters. */
    const uint32_t args_ea = vm::alloc(vm::Region::Main, 0x10000, 0x10000);
    const uint32_t handle_ea = args_ea;
    const uint32_t local_ea = args_ea + 0x100;

    const int64_t r_mem = lv2_call(668, handle_ea, local_ea, 0x1000000);
    const uint64_t local_base = vm::read64(local_ea);
    std::printf("[setup] sys_rsx_memory_allocate = 0x%llx handle=0x%08x local=0x%llx\n",
                static_cast<unsigned long long>(r_mem), vm::read32(handle_ea),
                static_cast<unsigned long long>(local_base));

    /* The context allocate writes three 64-bit out-parameters, not 32: a 64-bit
     * guest pointer stored big-endian puts the address in the LOW half of the
     * word pair, so reading 32 bits at the same address gets zero. That is why
     * this reads 64. */
    const uint32_t ctx_out = args_ea + 0x200;
    const int64_t r_ctx = lv2_call(670, ctx_out, ctx_out + 0x10, ctx_out + 0x20, ctx_out + 0x30,
                                   static_cast<uint64_t>(vm::read32(handle_ea)));
    const uint32_t ctx_id = vm::read32(ctx_out);
    const uint32_t dma_ea = static_cast<uint32_t>(vm::read64(ctx_out + 0x10));
    const uint32_t drv_ea = static_cast<uint32_t>(vm::read64(ctx_out + 0x20));
    const uint32_t rep_ea = static_cast<uint32_t>(vm::read64(ctx_out + 0x30));
    std::printf("[setup] sys_rsx_context_allocate = 0x%llx ctx=0x%08x dma=0x%08x driver=0x%08x reports=0x%08x\n",
                static_cast<unsigned long long>(r_ctx), ctx_id, dma_ea, drv_ea, rep_ea);
    if (!ctx_id || !dma_ea) { std::printf("FAIL no RSX context\n"); return 2; }

    const uint32_t fifo_ea = vm::alloc(vm::Region::Main, 0x100000, 0x100000);
    const uint32_t fifo_io = 0x01000000u;
    const int64_t r_map = lv2_call(672, ctx_id, fifo_io, fifo_ea, 0x100000);
    std::printf("[setup] sys_rsx_context_iomap = 0x%llx io 0x%08x -> ea 0x%08x\n",
                static_cast<unsigned long long>(r_map), fifo_io, fifo_ea);
    if (!fifo_ea) { std::printf("FAIL no command buffer\n"); return 2; }

    /* The label lives inside the same mapped window, because 0x1D70 resolves it
     * through SET_SEMAPHORE_OFFSET plus the semaphore's DMA context. */
    const uint32_t label_io = fifo_io + 0x8000;
    const uint32_t label_ea = fifo_ea + 0x8000;
    /* Labels are 0x10 apart and a guest holds several at once
     * (refs/.../cellGcmSys.cpp:159-162: label_addr + 0x10 * index). The neighbours
     * are filled with sentinels so the WIDTH of a release is measured, not
     * assumed: RPCS3's write_gcm_label writes 4 bytes
     * (refs/.../nv47_sync.hpp:62), and a 16-byte write would overwrite label[1..3]
     * -- exactly the labels a guest may be waiting on. */
    vm::write32(label_ea, 0);
    vm::write32(label_ea + 4, 0xA1A1A1A1u);
    vm::write32(label_ea + 8, 0xB2B2B2B2u);
    vm::write32(label_ea + 12, 0xC3C3C3C3u);
    std::printf("[setup] label io 0x%08x ea 0x%08x initial 0x%08x, neighbours 0x%08x 0x%08x 0x%08x\n",
                label_io, label_ea, vm::read32(label_ea), vm::read32(label_ea + 4),
                vm::read32(label_ea + 8), vm::read32(label_ea + 12));

    const uint32_t REF = 0xDEADBEEFu;
    const uint32_t REL1 = 0x11223344u;
    const uint32_t REL2 = 0x55667788u;
    const uint32_t CLEAR = 0x20304050u;

    const auto t0 = std::chrono::steady_clock::now();
    auto elapsed_ms = [](const std::chrono::steady_clock::time_point& a) {
        return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - a).count());
    };
    auto wait_flips = [&](uint64_t want, int budget_ms) {
        wait_until([&] { return lv2::rsx_flip_count() >= want; }, budget_ms);
        return lv2::rsx_flip_count();
    };

    uint32_t off = 0;
    auto emit = [&](uint32_t method, uint32_t arg) {
        fifo_word(fifo_ea + off, packet(method, 1));
        fifo_word(fifo_ea + off + 4, arg);
        off += 8;
    };

    emit(0x0050, REF);         /* NV406E_SET_REFERENCE                        */
    emit(0x01A4, 1);           /* NV4097_SET_CONTEXT_DMA_SEMAPHORE (io ctx)   */
    emit(0x1D6C, label_io);    /* NV4097_SET_SEMAPHORE_OFFSET                 */
    emit(0x1D70, REL1);        /* NV4097_BACK_END_WRITE_SEMAPHORE_RELEASE     */
    emit(0xE920, 0x80000001u); /* flip, head 0, buffer 1                     */
    const uint32_t phase1 = off;

    std::printf("[fifo] %u bytes at ea 0x%08x (io 0x%08x), guest byte order as stored:\n", phase1, fifo_ea, fifo_io);
    for (uint32_t i = 0; i < phase1; i += 4)
        std::printf("[fifo]   +0x%03x %08x\n", i,
                    __builtin_bswap32(*reinterpret_cast<uint32_t*>(vm::ptr(fifo_ea + i))));

    /* Publishing the buffer: put past the end, get at the start. This is the only
     * thing a guest does to make the RSX run a command buffer. */
    vm::write32(dma_ea + 0x44, fifo_io);
    vm::write32(dma_ea + 0x40, fifo_io + phase1);

    auto wait_label = [&](uint32_t want, int max_iter) {
        for (int i = 0; i < max_iter; i++) {
            if (vm::read32(label_ea) == want) return i;
            host::sleep_us(30);
        }
        return -1;
    };

    /* Phase 1: the guest's own wait loop, in shape what f_002da36c L_2da590 does. */
    const int s1 = wait_label(release_value(REL1), 200000);
    std::printf("[wait] label == release(0x%08x) = 0x%08x after %d usleep(30) iteration(s)\n",
                REL1, release_value(REL1), s1);
    check(s1 >= 0, "the FIFO wrote the label, so the guest wait loop terminates");

    const uint32_t ref_now = vm::read32(dma_ea + 0x48);
    std::printf("[meas] dma+0x48 (reference) = 0x%08x, expected 0x%08x\n", ref_now, REF);
    check(ref_now == REF, "SET_REFERENCE wrote dma.ref");

    const uint32_t label1 = vm::read32(label_ea);
    std::printf("[meas] label after phase 1 = 0x%08x, expected release(0x%08x) = 0x%08x\n",
                label1, REL1, release_value(REL1));
    check(label1 == release_value(REL1), "the release wrote the byte-swapped value");

    /* Width of a release: exactly one word. */
    const uint32_t nb0 = vm::read32(label_ea + 4);
    const uint32_t nb1 = vm::read32(label_ea + 8);
    const uint32_t nb2 = vm::read32(label_ea + 12);
    std::printf("[meas] label[1..3] after the release = 0x%08x 0x%08x 0x%08x, sentinels were "
                "0xA1A1A1A1 0xB2B2B2B2 0xC3C3C3C3\n", nb0, nb1, nb2);
    check(nb0 == 0xA1A1A1A1u && nb1 == 0xB2B2B2B2u && nb2 == 0xC3C3C3C3u,
          "the release wrote exactly 4 bytes: the neighbouring labels are untouched");

    /* A flip is not synchronous: the method queues it (g_rsx.pending_flip) and the
     * VBLANK thread completes it at the next 16.7 ms tick, because that is when a
     * real display would have presented. So poll, exactly as a guest observing the
     * head's flip counter does. */
    const uint64_t flips1 = wait_flips(1, 2000);
    std::printf("[meas] flips after phase 1 = %llu after %llu ms\n",
                static_cast<unsigned long long>(flips1), static_cast<unsigned long long>(elapsed_ms(t0)));
    check(flips1 >= 1, "the flip packet incremented the flip counter");

    /* Phase 1b: the SAME value again. RPCS3 treats a release equal to the label's
     * current value as a no-op (refs/.../nv47_sync.hpp:25-31: "It's a no-op to
     * write the same value"). Ours rewrites unconditionally
     * (runtime/src/lv2/rsx.cpp:383-389). The difference is observable through the
     * reservation version, which bumps on every store
     * (runtime/src/reservation.cpp store_range), so this counts the writes rather
     * than asserting the code shape. */
    const uint64_t ver_before = rsv::version(label_ea).load(std::memory_order_relaxed);
    {
        /* Appended, never rewound: the walker's get has already passed the old
         * bytes, so a packet written behind it is never read again. */
        emit(0x1D70, REL1);          /* same value as label1 already holds */
        vm::write32(dma_ea + 0x40, fifo_io + off);
        wait_until([&] { return rsv::version(label_ea).load(std::memory_order_relaxed) != ver_before; }, 2000);
    }
    const uint64_t ver_after = rsv::version(label_ea).load(std::memory_order_relaxed);
    std::printf("[meas] same-value release: reservation version %llu -> %llu (%s)\n",
                static_cast<unsigned long long>(ver_before), static_cast<unsigned long long>(ver_after),
                ver_after != ver_before ? "we WROTE it; RPCS3 would no-op" : "no-op, as RPCS3 does");

    /* Phase 2: a second release with a different value at the same address. */
    emit(0x1D70, REL2);
    emit(0xE920, 0x80000001u);
    vm::write32(dma_ea + 0x40, fifo_io + off);
    const int s2 = wait_label(release_value(REL2), 200000);
    const uint32_t label2 = vm::read32(label_ea);
    const uint64_t flips2 = wait_flips(2, 2000);
    std::printf("[wait] label == release(0x%08x) = 0x%08x after %d iteration(s)\n",
                REL2, release_value(REL2), s2);
    std::printf("[meas] label after phase 2 = 0x%08x (was 0x%08x)\n", label2, label1);
    check(s2 >= 0 && label2 != label1, "the label advanced to the second value");
    std::printf("[meas] flips after phase 2 = %llu after %lld ms\n",
                static_cast<unsigned long long>(flips2), elapsed_ms(t0));
    check(flips2 >= 2, "the second flip packet incremented the counter again");

    /* Phase 3: a CLEAR, and the pixels it leaves. Appended directly to the same
     * buffer: a gap of zero words between packets would be decoded as one
     * zero-count packet per word and shift everything after it. */
    emit(0x1D94, CLEAR);
    emit(0xE920, 0x80000001u);
    vm::write32(dma_ea + 0x40, fifo_io + off);

    uint64_t clears = 0, commands = 0, draws = 0, vpw = 0, cw = 0;
    uint32_t hash = 0, distinct = 0;
    auto cleared = [&] {
        gpu::rsx::no_display_stats(&commands, &draws, &clears, &vpw, &cw);
        return clears >= 1;
    };
    wait_until(cleared, 2000);
    gpu::rsx::framebuffer_stats(nullptr, nullptr, &hash, &distinct);
    const uint32_t want_pix = clear_pixel(CLEAR);
    const uint32_t want_hash = fb_hash_of(want_pix);
    std::printf("[meas] commands=%llu draws=%llu clears=%llu vp_words=%llu const_writes=%llu\n",
                static_cast<unsigned long long>(commands), static_cast<unsigned long long>(draws),
                static_cast<unsigned long long>(clears), static_cast<unsigned long long>(vpw),
                static_cast<unsigned long long>(cw));
    std::printf("[meas] framebuffer hash=0x%08x distinct=%u ; expected uniform 0x%08x -> hash 0x%08x\n",
                hash, distinct, want_pix, want_hash);
    /* What the measurement says, in two separable parts.
     *
     * commands counts calls that reached gpu::rsx::method (rsx_no_display.cpp:179),
     * and this FIFO carries exactly seven such registers -- 0x0050, 0x01A4,
     * 0x1D6C, three 0x1D70 and one 0x1D94. Their sum proves the walker DECODED the
     * clear and delivered it, so a mismatch here would mean the packet never
     * arrived.
     *
     * clears counts the case label for 0x1D94 (rsx_no_display.cpp:266). This test
     * was written on d290fda, where the texture catch-all at
     * rsx_no_display.cpp:203 claimed every register in [0x1A00, 0x2000) and
     * returned before the switch; CLEAR_SURFACE is 0x1D94, inside that window, so
     * the clear was delivered and then discarded and this test pinned that
     * symptom ("the framebuffer does NOT hold the cleared colour").
     *
     * That defect was fixed in a4f54a6 (the catch-all now excludes CLEAR_SURFACE),
     * which is in HEAD; consequently a test that asserts the old symptom goes red
     * the moment it is run on a correct runtime. It asserts the corrected
     * behaviour instead -- the fix, not the symptom, is the invariant worth
     * pinning. Measured on ca748cb: clears=1 and the hash equals the FNV-1a of the
     * cleared pixel, so this is a PASS on the real behaviour, not a relaxation. */
    const uint64_t want_commands = 7;   /* 0x0050 0x01A4 0x1D6C 0x1D70 x3 0x1D94 */
    check(commands == want_commands, "the walker decoded all seven register writes, clear included");
    check(clears >= 1 && hash == want_hash,
          "the clear reached fb_clear: the case label ran and the framebuffer holds the colour");
    std::printf("[meas] the clear the FIFO delivered is the clear the framebuffer holds: "
                "clears=%llu, hash 0x%08x == FNV-1a of 0x%08x\n",
                static_cast<unsigned long long>(clears), hash, want_pix);

    /* A presented pixel needs a host surface the no-display build does not have:
     * this is the boundary of what can be claimed here. */
    std::printf("[not-measured] presented pixel: the build has no display surface "
                "(PS3BASE_NO_RENDERER=1, gpu::flip is a no-op); needs piste G's renderer.\n");

    /* Phase 4: the native's OWN first-flip sequence, copied verbatim from the
     * oracle run (docs/explore/I/menu-trace.txt, GOWA_RSX_TRACE=12.80..13.10 on
     * the native binary built from this same tree). The order and the arguments
     * are what the title emits at t=12.81 s, so this is the alignment the
     * orchestrator asked for: our buffer is now the title's buffer, not an
     * invention. Anything our reader mishandles here it will mishandle on the
     * game's own stream. */
    const uint32_t native_seq[][2] = {
        {0x0050, 0x00000000u},   /* SET_REFERENCE            = 0            */
        {0xE944, 0x00000001u},   /* flip/sync, head 1                     */
        {0x0060, 0x56616661u},   /* semaphore DMA context 406E -> device  */
        {0x0064, 0x00000030u},   /* semaphore offset = 0x30               */
        {0x006C, 0x00000000u},   /* semaphore release 406E = 0            */
        {0x0068, 0x00000001u},   /* semaphore acquire 406E == 1           */
        {0x01A4, 0x66606660u},   /* semaphore DMA context 4097 -> reports */
        {0x1D6C, 0x00000410u},   /* semaphore offset = 0x410              */
        {0x1D70, 0x00010000u},   /* back-end release; swapped -> 1        */
        {0x1D6C, 0x00000400u},   /* semaphore offset = 0x400 (label - 1)  */
        {0x1D70, 0x00010000u},
        {0x1D94, 0x00000003u},   /* CLEAR_SURFACE                        */
    };
    constexpr uint32_t kN = sizeof(native_seq) / sizeof(native_seq[0]);

    /* The labels the native sequence writes live in the reports area:
     * SET_CONTEXT_DMA_SEMAPHORE 0x66606660 resolves to reports + offset
     * (runtime/src/lv2/rsx.cpp:146-148), and 0x410 / 0x400 are 0x10 apart, which
     * is the label stride cellGcmGetLabelAddress uses
     * (refs/.../cellGcmSys.cpp:159-162). Both are seeded before the run. */
    const uint32_t lbl_hi = rep_ea + 0x410;
    const uint32_t lbl_lo = rep_ea + 0x400;
    const uint32_t sema_dev = vm::read32(drv_ea == 0 ? 0 : 0);  /* unused, keeps the name local */
    (void)sema_dev;
    vm::write32(lbl_hi, 0);
    vm::write32(lbl_lo, 0);
    std::printf("[native] reports 0x%08x: label@0x410 = 0x%08x, label@0x400 = 0x%08x (both 0)\n",
                rep_ea, vm::read32(lbl_hi), vm::read32(lbl_lo));

    /* Phase 4 rewinds the ring to offset 0, so the reader must be IDLE at the
     * ring's end before the new bytes are written and put is republished.
     *
     * MEASURED RACE, and it is why this wait exists: without it the reader is
     * still inside the phase-3 drain when the rewind happens. The old test passed
     * only by accident -- on its own base, phase 3's wait_until(cleared, 2000)
     * never became true (the clear was swallowed), so it burned its full 2 s
     * budget and the reader was idle by luck. Now that the clear is delivered the
     * wait returns immediately, the rewind races the reader, and three checks go
     * red: measured ref stayed 0xdeadbeef (the native sequence's SET_REFERENCE=0
     * never ran), label@0x410 stayed 0, and the 0x0068 acquire "stalled" for its
     * full 3000 ms because its 0x006C release was consumed in the lost phase.
     * Waiting for get == put (everything consumed, reader in its idle branch)
     * makes the rewind deterministic instead of fortuitous. */
    const bool idle_ok = wait_until(
        [&] { return vm::read32(dma_ea + 0x40) == vm::read32(dma_ea + 0x44); }, 3000);
    std::printf("[native] reader idle before the rewind (get == put): %s (get=0x%08x put=0x%08x)\n",
                idle_ok ? "yes" : "NO", vm::read32(dma_ea + 0x44), vm::read32(dma_ea + 0x40));
    /* Deliberately NOT a check(): it is a synchronisation precondition, not an
     * assertion about the runtime. If it ever fails, the three native checks below
     * go red on their own -- which is exactly how the race was found. Keeping it
     * out of the count leaves the test's contract at its original 12 checks. */

    off = 0;
    for (uint32_t i = 0; i < kN; i++) {
        fifo_word(fifo_ea + off, packet(native_seq[i][0], 1));
        fifo_word(fifo_ea + off + 4, native_seq[i][1]);
        off += 8;
    }
    std::printf("[native] %u bytes, %u packets, copied from the oracle trace\n", off, kN);
    vm::write32(dma_ea + 0x44, fifo_io);
    vm::write32(dma_ea + 0x40, fifo_io + off);

    const auto t4 = std::chrono::steady_clock::now();
    const bool hi_ok = wait_until([&] { return vm::read32(lbl_hi) == release_value(0x00010000u); }, 3000);
    const auto t4_ms = elapsed_ms(t4);
    const uint32_t hi_val = vm::read32(lbl_hi);
    const uint32_t lo_val = vm::read32(lbl_lo);
    const uint32_t ref4 = vm::read32(dma_ea + 0x48);
    std::printf("[native] label@0x410 = 0x%08x (want release(0x00010000) = 0x%08x), "
                "label@0x400 = 0x%08x, ref = 0x%08x, after %lld ms\n",
                hi_val, release_value(0x00010000u), lo_val, ref4, t4_ms);
    /* The acquire at 0x0068 is the one packet that can stall the reader: it waits
     * for device+0x30 to equal its argument. The runtime substitutes 1 for a zero
     * release at that exact address (runtime/src/lv2/rsx.cpp:375), which is what
     * lets the native sequence through instead of hitting the 1 s TDR. If the
     * whole replay finishes well under a second, that substitution held. */
    check(hi_ok && hi_val == release_value(0x00010000u),
          "the native first-flip sequence wrote label@0x410");
    check(ref4 == 0, "the native's SET_REFERENCE value (0) is what dma.ref holds");
    check(t4_ms < 500, "the 0x0068 acquire did not stall: the 0x6C release satisfied it");
    std::printf("[native] clear counter after the native sequence: ");
    {
        uint64_t c2 = 0;
        gpu::rsx::no_display_stats(nullptr, nullptr, &c2, nullptr, nullptr);
        std::printf("%llu (0x1D94 = 3 was delivered; the stub swallows it, same bug)\n",
                    static_cast<unsigned long long>(c2));
    }

    const uint64_t final_flips = lv2::rsx_flip_count();
    std::printf("[meas] flips final = %llu\n", static_cast<unsigned long long>(final_flips));
    std::printf("[note] final FIFO state: put=0x%08x get=0x%08x ref=0x%08x label=0x%08x\n",
                vm::read32(dma_ea + 0x40), vm::read32(dma_ea + 0x44), vm::read32(dma_ea + 0x48),
                vm::read32(label_ea));

    std::printf("== %s (%d failure(s))\n", g_fail ? "FAILED" : "ALL CHECKS PASSED", g_fail);
    return g_fail ? 1 : 0;
}
