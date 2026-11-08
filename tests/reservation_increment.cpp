/* ps3base -- lwarx/stwcx used the way the title actually uses it: a lock-free
 * read-modify-write retry loop, with no lock around it.
 *
 *     1: lwarx  r3, 0, r4
 *        addi   r3, r3, 1
 *        stwcx. r3, 0, r4
 *        bne-   1b
 *
 * Each successful stwcx. must have observed the value it replaced. If two
 * threads both report success while one of the increments was lost, the counter
 * ends smaller than the number of successes -- and that lost update is exactly
 * how a shared list gets a field overwritten with garbage (a freed node's next
 * pointer, a code address, anything).
 *
 * This is stricter than tests/lock_counter.cpp, which hides the reservation
 * behind a host mutex and therefore cannot see a reservation bug at all.
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
uint32_t g_ctr = 0;

uint32_t alloc_word() {
    const uint32_t a = vm::reserve_slot(0x10000, 0x1000);
    if (!a || !vm::commit(a, 0x10000)) return 0;
    return a + 0x80;
}

/* The instruction sequence above, for one thread. Returns the number of
 * successful stwcx. */
long increment_loop() {
    ppu::Context ctx{};
    uint8_t* base = vm::g_base;
    long ok = 0;
    for (int i = 0; i < kIters; i++) {
        for (;;) {
            uint32_t v = ppu::lwarx(ctx, base, g_ctr);
            if (ppu::stwcx(ctx, base, g_ctr, v + 1)) {
                ok++;
                break;
            }
        }
    }
    return ok;
}

}  // namespace

int main()
{
    if (!vm::init()) {
        std::fprintf(stderr, "vm::init failed\n");
        return 1;
    }
    g_ctr = alloc_word();
    if (!g_ctr) {
        std::fprintf(stderr, "could not map the test word\n");
        return 1;
    }
    vm::write32(g_ctr, 0);

    /* Sanity: one thread alone must be exact. */
    {
        const long ok = increment_loop();
        const uint32_t got = vm::read32(g_ctr);
        std::fprintf(stderr, "  single thread: %ld successes, counter %u\n", ok, got);
        CHECK(ok == kIters && got == uint32_t(kIters), "a lone lwarx/stwcx loop must be exact");
        vm::write32(g_ctr, 0);
    }

    /* Two threads, one word, no lock: successes and the counter must agree. */
    for (int round = 0; round < 3; round++) {
        vm::write32(g_ctr, 0);
        std::atomic<long> total{0};
        std::vector<std::thread> ts;
        for (int t = 0; t < 2; t++) ts.emplace_back([&] { total.fetch_add(increment_loop()); });
        for (auto& t : ts) t.join();
        const uint32_t got = vm::read32(g_ctr);
        std::fprintf(stderr, "  round %d: %ld successful stwcx, counter %u%s\n", round,
                     total.load(), got, got == uint32_t(total.load()) ? "" : "  <-- LOST UPDATES");
        CHECK(got == uint32_t(total.load()),
              "every successful stwcx must have replaced the value it loaded");
        CHECK(got == 2u * kIters, "two threads x 1e6 lock-free increments must give exactly 2000000");
    }

    /* 4 threads, same invariant: the crash is under many agents, not two. */
    {
        vm::write32(g_ctr, 0);
        std::atomic<long> total{0};
        std::vector<std::thread> ts;
        for (int t = 0; t < 4; t++) ts.emplace_back([&] { total.fetch_add(increment_loop()); });
        for (auto& t : ts) t.join();
        const uint32_t got = vm::read32(g_ctr);
        std::fprintf(stderr, "  4 threads: %ld successes, counter %u\n", total.load(), got);
        CHECK(got == uint32_t(total.load()), "4 threads: successes must equal the counter");
    }

    std::printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
