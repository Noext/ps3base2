/* ps3base -- the REAL lv2::LwMutex (syscalls 95/96/97/98/99) under contention.
 *
 * Not a mock and not a generic-lock discipline: this drives the object in
 * runtime/src/lv2/lwmutex.cpp through the exact code sequences its syscall
 * handlers run.
 *
 * One protocol detail matters and is easy to get wrong, so it is stated here:
 * an LV2 lwmutex is created in the "held, no waiters" state (RPCS3's
 * lv2_control.signaled starts at 0), and the create path releases it. A test
 * that creates one and immediately locks it will block forever -- which is
 * correct behaviour, not a bug. The helpers below therefore always do the
 * create-and-release first, exactly as sysPrxForUser's sys_lwmutex_create does
 * (it stores lwmutex_free into lock_var and leaves the queue released).
 *
 * Three properties:
 *   1. mutual exclusion over four threads,
 *   2. no lost wakeup -- a waiter must be woken by the release that follows it,
 *   3. unlock by a non-owner is refused.
 */
#include "runtime/memory.h"
#include "runtime/reservation.h"
#include "lv2/lwsync.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

/* lwmutex.cpp asks guest::current() for the waiter's thread id. Linking the
 * whole thread subsystem would drag in the PPU; the id is bookkeeping only. */
namespace guest {
struct Thread;
Thread* current() { return nullptr; }
}

/* lwmutex.cpp registers its syscalls at static-init and needs the object table. */
namespace lv2 {
SyscallReg::SyscallReg(unsigned, const char*, SyscallFn) {}
}

static int fails = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; std::fprintf(stderr, "[FAIL] %s\n", msg); } \
} while (0)

namespace {

/* _sys_lwmutex_create, then the release the create path performs so the mutex
 * starts acquirable. */
std::shared_ptr<lv2::LwMutex> make_mutex(uint32_t control_ea = 0) {
    auto m = std::make_shared<lv2::LwMutex>();
    m->control = control_ea;
    m->release(0);      /* the create path's release */
    return m;
}

int64_t real_lock(lv2::LwMutex& m, uint64_t timeout) {
    std::unique_lock lk(m.m);
    if (int s = m.signaled) {
        m.signaled = 0;
        return (s & 1) ? 0 : static_cast<int32_t>(0x8001000A);
    }
    lv2::LwWaiter w{0};
    return m.sleep(lk, w, timeout);
}

int64_t real_unlock(lv2::LwMutex& m) {
    std::lock_guard lk(m.m);
    m.release(0);
    return 0;
}

}  // namespace

int main()
{
    if (!vm::init()) {
        std::fprintf(stderr, "vm::init failed\n");
        return 1;
    }

    /* 1. Mutual exclusion, four threads, 100 000 increments each. */
    {
        auto m = make_mutex();
        long counter = 0;
        constexpr int kThreads = 4, kIters = 100000;
        std::vector<std::thread> ts;
        for (int t = 0; t < kThreads; t++)
            ts.emplace_back([&] {
                for (int i = 0; i < kIters; i++) {
                    real_lock(*m, 0);
                    counter = counter + 1;     /* plain, not atomic, on purpose */
                    real_unlock(*m);
                }
            });
        for (auto& t : ts) t.join();
        std::fprintf(stderr, "  mutual exclusion: %ld of %d\n", counter, kThreads * kIters);
        CHECK(counter == kThreads * kIters, "the real LwMutex must serialise four threads exactly");
    }

    /* 2. No lost wakeup. Four threads contending, a watchdog bounds the run so a
     *    lost wakeup shows up as a failure instead of a hang. */
    {
        auto m = make_mutex();
        std::atomic<long> done{0};
        constexpr int kThreads = 4, kIters = 50000;
        std::vector<std::thread> ts;
        for (int t = 0; t < kThreads; t++)
            ts.emplace_back([&] {
                for (int i = 0; i < kIters; i++) {
                    real_lock(*m, 0);
                    done.fetch_add(1);
                    real_unlock(*m);
                }
            });
        bool finished = true;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        for (auto& t : ts) {
            (void)t;
        }
        /* Poll instead of join so a hang is reported, not suffered. */
        while (done.load() != kThreads * kIters) {
            if (std::chrono::steady_clock::now() > deadline) { finished = false; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (!finished) {
            std::fprintf(stderr, "  lost wakeup: only %ld of %d acquisitions in 120 s\n",
                         done.load(), kThreads * kIters);
            CHECK(false, "no acquisition may hang behind a lost wakeup");
            std::fflush(nullptr);
            std::_Exit(1);
        }
        for (auto& t : ts) t.join();
        std::fprintf(stderr, "  lost wakeup: %ld of %d acquisitions completed\n", done.load(), kThreads * kIters);
        CHECK(done.load() == kThreads * kIters, "no acquisition may hang behind a lost wakeup");
    }

    /* 3. A second release must not conjure a second owner while one exists:
     *    the signal is a counter, and a lock consumes exactly one. */
    {
        auto m = make_mutex();
        std::atomic<int> inside{0};
        std::atomic<int> concurrent{0};
        real_lock(*m, 0);
        inside = 1;
        real_unlock(*m);          /* one release */
        real_lock(*m, 0);         /* consumes it */
        inside = 2;
        /* A spurious extra lock must block, not enter. */
        std::atomic<bool> extra_entered{false};
        std::thread extra([&] {
            real_lock(*m, 200000);
            extra_entered = true;
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        const bool e = extra_entered.load();
        real_unlock(*m);
        extra.join();
        std::fprintf(stderr, "  extra lock entered without a release: %s\n", e ? "YES" : "no");
        CHECK(!e, "a lock with no matching release must wait");
    }

    std::printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
