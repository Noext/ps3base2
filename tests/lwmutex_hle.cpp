/* ps3base -- the sysPrxForUser lwmutex HLE, driven through the REAL code.
 *
 * The subject is runtime/src/hle/lwmutex_hle.h, the body of the four
 * sysPrxForUser lwmutex imports. It is included, not re-implemented: a test
 * that copies the logic under test can only confirm the copy.
 *
 * Two properties the title depends on, both of which were broken:
 *
 *   1. destroy() while a thread is inside must not hand the address to a second
 *      thread. Erasing the map entry dropped the last reference, so the next
 *      lock built a FRESH unlocked mutex at the same address and walked in.
 *      Two threads then mutate one list: a list edit versus a list walk, which
 *      is what corrupts a node's +28/+32 and derails f_0064c044.
 *
 *   2. unlock() from a thread that does not own the mutex must be refused.
 *      std::mutex::unlock() from a foreign thread is undefined behaviour and on
 *      glibc it silently releases, letting the next thread in.
 */
#include "hle/lwmutex_hle.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

static int fails = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; std::fprintf(stderr, "[FAIL] %s\n", msg); } \
} while (0)

namespace {

thread_local uint32_t t_tid = 0;
uint32_t next_tid = 0x10000000;

/* vm::map is the call that both commits the range and marks its pages mapped;
 * vm::commit alone leaves g_pagemap clear, so range_mapped() would refuse every
 * access. This is the same path the loader uses for the title's segments. */
uint32_t g_next_area = 0x20000000;

uint32_t alloc_area() {
    const uint32_t a = g_next_area;
    if (!vm::map(a, 0x10000)) return 0;
    g_next_area += 0x10000;
    return a + 0x80;
}

int64_t lw_lock(uint32_t ea) { return hle::lwmutex::lock(ea); }
int64_t lw_unlock(uint32_t ea) { return hle::lwmutex::unlock(ea); }
int64_t lw_destroy(uint32_t ea) { return hle::lwmutex::destroy(ea); }

}  // namespace

/* The header asks the translation unit for the current guest thread id. */
namespace hle {
namespace lwmutex {
uint32_t me() { return t_tid; }
}  // namespace lwmutex
}  // namespace hle

int main()
{
    if (!vm::init()) {
        std::fprintf(stderr, "vm::init failed\n");
        return 1;
    }

    /* 1. DESTROY WHILE HELD. Thread A holds; destroy runs; thread B locks the
     *    same address and must stay out until A releases. */
    {
        hle::lwmutex::reset_all();
        const uint32_t lw = alloc_area();
        std::atomic<bool> a_inside{false}, release_a{false}, b_inside{false}, overlap{false};
        std::thread a([&] {
            t_tid = ++next_tid;
            lw_lock(lw);
            a_inside = true;
            while (!release_a.load()) std::this_thread::sleep_for(std::chrono::microseconds(200));
            lw_unlock(lw);
            a_inside = false;
        });
        while (!a_inside.load()) std::this_thread::sleep_for(std::chrono::microseconds(200));
        lw_destroy(lw);                       /* by a thread that holds nothing */
        std::thread b([&] {
            t_tid = ++next_tid;
            lw_lock(lw);
            b_inside = true;
            if (a_inside.load()) overlap = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
            lw_unlock(lw);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        const bool o = overlap.load();
        release_a = true;
        b.join();
        a.join();
        std::fprintf(stderr, "  destroy-while-held: second thread entered while the first was inside: %s\n", o ? "YES" : "no");
        CHECK(!o, "destroy must not admit a second thread while the mutex is held");
    }

    /* 2. UNLOCK BY A NON-OWNER must be refused. */
    {
        hle::lwmutex::reset_all();
        const uint32_t lw = alloc_area();
        t_tid = ++next_tid;
        lw_lock(lw);                          /* held by "this" thread */
        std::atomic<int64_t> foreign_rc{1};
        std::thread foreign([&] {
            t_tid = ++next_tid;
            foreign_rc = lw_unlock(lw);
        });
        foreign.join();
        const bool refused = static_cast<int32_t>(foreign_rc.load()) == static_cast<int32_t>(0x80010009);
        std::fprintf(stderr, "  foreign unlock returned 0x%llx (refused: %s)\n",
                     static_cast<unsigned long long>(foreign_rc.load()), refused ? "yes" : "NO");
        CHECK(refused, "an unlock from a non-owner must return EPERM, not release the mutex");

        std::atomic<bool> entered{false};
        std::thread thief([&] {
            t_tid = ++next_tid;
            lw_lock(lw);
            entered = true;
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        const bool e = entered.load();
        std::fprintf(stderr, "  non-owner unlock let another thread in: %s\n", e ? "YES" : "no");
        CHECK(!e, "the mutex must still be held after a refused foreign unlock");
        lw_unlock(lw);
        thief.join();
    }

    /* 3. The ordinary path still works: N threads x M increments under the real
     *    HLE lock, nothing lost. */
    {
        hle::lwmutex::reset_all();
        const uint32_t lw = alloc_area();
        const uint32_t ctr = lw + 8;
        vm::write32(ctr, 0);
        std::atomic<long> total{0};
        constexpr int kThreads = 4, kIters = 50000;
        std::vector<std::thread> ts;
        for (int t = 0; t < kThreads; t++)
            ts.emplace_back([&] {
                t_tid = ++next_tid;
                for (int i = 0; i < kIters; i++) {
                    lw_lock(lw);
                    vm::write32(ctr, vm::read32(ctr) + 1);
                    lw_unlock(lw);
                    total.fetch_add(1);
                }
            });
        for (auto& t : ts) t.join();
        const uint32_t got = vm::read32(ctr);
        std::fprintf(stderr, "  mutual exclusion: counter %u of %d\n", got, kThreads * kIters);
        CHECK(got == uint32_t(kThreads * kIters), "the HLE lock must serialise four threads exactly");
    }

    /* 4. Recursive attribute: the owner re-locks without deadlocking. */
    {
        hle::lwmutex::reset_all();
        const uint32_t lw = alloc_area();
        const uint32_t attr = lw + 0x40;
        vm::write32(attr + 4, 0x10);          /* SYS_SYNC_RECURSIVE */
        hle::lwmutex::create(lw, attr);
        std::atomic<bool> acquired{false}, done{false};
        std::thread t([&] {
            /* t_tid is thread_local: it must be assigned inside the thread, or
             * the owner id is 0 and the recursive path never matches. */
            t_tid = ++next_tid;
            lw_lock(lw);
            lw_lock(lw);                      /* recursive: must not block */
            acquired = true;
            lw_unlock(lw);
            lw_unlock(lw);
            done = true;
        });
        for (int i = 0; i < 300 && !done.load(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(done.load(), "a recursive lwmutex must let its owner re-lock without deadlocking");
        t.join();
    }

    std::printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
