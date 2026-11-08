/* ps3base -- a guest-authored spinlock, exercised under contention.
 *
 * The title carries thousands of inline lwarx/stwcx loops of its own (the
 * lifted code has 15 301 lwarx and 15 306 stwcx pairs). Those are NOT the LV2
 * syscalls and NOT the sysPrxForUser HLE: they are the title's own atomics, and
 * a corrupted shared structure is exactly what a broken reservation looks like
 * from the outside.
 *
 * This test runs the canonical PowerPC lock acquisition, instruction for
 * instruction, through the same ppu::lwarx / ppu::stwcx the lifted code calls:
 *
 *     1: lwarx   r3, 0, r4        ; load the lock word, set the reservation
 *        cmpwi   r3, 0
 *        bne     1b              ; held -> spin
 *        li      r3, 1
 *        stwcx.  r3, 0, r4       ; commit only if nothing else wrote the line
 *        bne-    1b              ; reservation lost -> retry
 *
 * Two threads take that lock and increment a shared counter a million times
 * each; the result must be exactly 2 000 000. A lost update means stwcx
 * reported success on a line someone else had already written -- the precise
 * failure that lets two threads into one critical section and shreds a list.
 *
 * A second phase interleaves an SPU-side putllc (whole-line store) with the
 * PPU reservation, because the title's server runs both agents on the same
 * memory.
 */
#include "runtime/memory.h"
#include "runtime/reservation.h"
#include "ppu/ops.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

static int fails = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; std::fprintf(stderr, "[FAIL] %s\n", msg); } \
} while (0)

namespace {

constexpr int kIters = 1000000;

uint32_t alloc_line() {
    const uint32_t a = vm::reserve_slot(0x10000, 0x1000);
    if (!a) return 0;
    if (!vm::commit(a, 0x10000)) return 0;
    return a + 0x80;
}

uint32_t g_lock_ea = 0;      /* the lock word, inside guest memory */
uint32_t g_counter_ea = 0;   /* the shared counter, plain memory */

/* The exact instruction sequence above, for one thread. */
void guest_lock(ppu::Context& ctx, uint8_t* base) {
    for (;;) {
        uint32_t v = ppu::lwarx(ctx, base, g_lock_ea);
        if (v != 0) {
            std::this_thread::yield();
            continue;
        }
        if (ppu::stwcx(ctx, base, g_lock_ea, 1)) return;
    }
}

void guest_unlock(ppu::Context& ctx, uint8_t* base) {
    /* stwcx-free release: a plain store, as the title does on the fast path */
    ppu::st32(base, g_lock_ea, 0);
}

uint32_t guest_load_u32(uint32_t ea) { return vm::read32(ea); }
void guest_store_u32(uint32_t ea, uint32_t v) { vm::write32(ea, v); }

}  // namespace

int main()
{
    if (!vm::init()) {
        std::fprintf(stderr, "vm::init failed\n");
        return 1;
    }

    g_lock_ea = alloc_line();
    g_counter_ea = g_lock_ea + 8;
    if (!g_lock_ea) {
        std::fprintf(stderr, "could not map the test area\n");
        return 1;
    }
    guest_store_u32(g_lock_ea, 0);
    guest_store_u32(g_counter_ea, 0);

    /* Phase 1: the title's own spinlock, two threads, one shared counter. */
    {
        std::vector<std::thread> ts;
        for (int t = 0; t < 2; t++) {
            ts.emplace_back([] {
                ppu::Context ctx{};
                uint8_t* base = vm::g_base;
                for (int i = 0; i < kIters; i++) {
                    guest_lock(ctx, base);
                    /* read-modify-write, exactly as the guest would */
                    uint32_t c = guest_load_u32(g_counter_ea);
                    guest_store_u32(g_counter_ea, c + 1);
                    guest_unlock(ctx, base);
                }
            });
        }
        for (auto& t : ts) t.join();
        const uint32_t got = guest_load_u32(g_counter_ea);
        std::fprintf(stderr, "  guest spinlock: %u of %d increments\n", got, 2 * kIters);
        CHECK(got == 2u * kIters, "a guest lwarx/stwcx spinlock must serialise two threads exactly");
    }

    /* Phase 2: a reservation must die when another agent writes the line.
     *
     * This has to be DETERMINISTIC, not statistical. A tight lwarx->stwcx pair
     * against a background writer legitimately succeeds about half the time,
     * because the store only has to land in an instant when the line was not
     * touched -- an earlier version of this test asserted otherwise and
     * correctly failed. The property that matters is: if the line changed
     * between the load and the store, the store MUST fail. So the write is
     * placed between the two instructions instead of beside them. */
    {
        uint32_t line = g_lock_ea;
        ppu::Context ctx{};
        ppu::lwarx(ctx, vm::g_base, line);
        uint32_t other = 0x1234u;
        rsv::store_range(g_counter_ea, reinterpret_cast<const uint8_t*>("\0\0\0\0"), 4);
        (void)other;
        CHECK(!ppu::stwcx(ctx, vm::g_base, line, 0xDEADBEEF),
              "stwcx must fail when the line was written between lwarx and stwcx");
        /* And with no intervening write, it must succeed. */
        ppu::lwarx(ctx, vm::g_base, line);
        CHECK(ppu::stwcx(ctx, vm::g_base, line, 0x11223344),
              "stwcx must succeed when nothing wrote the line since lwarx");
    }

    std::printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
