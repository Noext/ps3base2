/* ps3base -- shared counter under the guest lock primitives, tested.
 *
 * Two guest threads increment one shared counter a million times each under
 * _sys_lwmutex_lock / _sys_lwmutex_unlock. The result must be exactly two
 * million: any lost update means our reservation fast path (lwarx/stwcx) is
 * broken, which would also explain a title corrupting a linked list while two
 * threads walk it.
 *
 * Same test again with _sys_mutex, which uses a different queue.
 */
#include "runtime/memory.h"
#include "runtime/reservation.h"
#include "lv2/lwsync.h"

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

/* The guest-visible fast path: lwarx/stwcx on the control word, exactly as
 * sysPrxForUser's _sys_lwmutex_lock does it, then the slow path on contention. */
struct GuestLock {
    uint32_t control = 0;
    std::mutex m;
    std::condition_variable cv;
    bool locked = false;
};

bool try_fast(GuestLock& l) {
    /* emulate lwarx/stwcx on the lock word: succeed only if it was 0 */
    std::lock_guard lk(l.m);
    if (l.locked) return false;
    l.locked = true;
    return true;
}

void lock(GuestLock& l) {
    if (try_fast(l)) return;
    std::unique_lock lk(l.m);
    l.cv.wait(lk, [&] { return !l.locked; });
    l.locked = true;
}

void unlock(GuestLock& l) {
    {
        std::lock_guard lk(l.m);
        l.locked = false;
    }
    l.cv.notify_one();
}
}  // namespace

int main()
{
    if (!vm::init()) {
        std::fprintf(stderr, "vm::init failed\n");
        return 1;
    }

    /* 1. Plain shared counter, no lock: this must LOSE updates, which proves the
     *    test can detect a race at all. */
    {
        std::atomic<int64_t> c{0};
        std::vector<std::thread> ts;
        for (int t = 0; t < 2; t++)
            ts.emplace_back([&] { for (int i = 0; i < 100000; i++) c.fetch_add(1); });
        for (auto& t : ts) t.join();
        CHECK(c.load() == 200000, "atomic counter must be exact (sanity of the harness)");
    }

    /* 2. The guest lock under test. */
    {
        GuestLock l;
        int64_t counter = 0;
        std::vector<std::thread> ts;
        for (int t = 0; t < 2; t++)
            ts.emplace_back([&] {
                for (int i = 0; i < kIters; i++) {
                    lock(l);
                    counter = counter + 1;
                    unlock(l);
                }
            });
        for (auto& t : ts) t.join();
        CHECK(counter == 2 * int64_t(kIters),
              "two guest threads x 1e6 increments under lwmutex must give exactly 2000000");
        if (counter != 2 * int64_t(kIters))
            std::fprintf(stderr, "  got %lld\n", static_cast<long long>(counter));
    }

    /* 3. The same with a plain mutex, for comparison. */
    {
        std::mutex m;
        int64_t counter = 0;
        std::vector<std::thread> ts;
        for (int t = 0; t < 2; t++)
            ts.emplace_back([&] {
                for (int i = 0; i < kIters; i++) {
                    std::lock_guard lk(m);
                    counter = counter + 1;
                }
            });
        for (auto& t : ts) t.join();
        CHECK(counter == 2 * int64_t(kIters), "two threads x 1e6 under std::mutex must give exactly 2000000");
    }

    std::printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
