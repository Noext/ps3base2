#include "runtime/fibertrace.h"
#include "runtime/log.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace fibertrace {

namespace {

constexpr uint32_t kRingSize = 512;
Event g_ring[kRingSize];
std::atomic<uint64_t> g_pos{0};
std::atomic<int> g_dump_once{0};
thread_local int t_depth = 0;

const char* kind_name(Kind k) {
    switch (k) {
        case Kind::Enter: return "ENTER";
        case Kind::Leave: return "LEAVE";
        case Kind::Switch: return "SWITCH";
    }
    return "?";
}

}  // namespace

namespace {
void exit_dump() { dump("exit"); }
}  // namespace

bool enabled() {
    static const bool on = std::getenv("PS3BASE_FIBER_TRACE") != nullptr;
    /* A run that does not crash still has to prove what it did: without this,
     * "no cellFiber event" and "the ring was never printed" look the same. The
     * dump is registered on the first read, which is the first import. */
    if (on) {
        static const bool once = [] { std::atexit(exit_dump); return true; }();
        (void)once;
    }
    return on;
}

int depth() { return t_depth; }

void note(Kind k, uint32_t thread, uint32_t addr, const char* name, uint32_t lr,
          uint32_t r1, uint64_t r3, bool switched) {
    if (!enabled()) return;
    const uint64_t i = g_pos.fetch_add(1, std::memory_order_relaxed);
    Event& e = g_ring[i % kRingSize];
    e.t = logging::uptime();
    e.thread = thread;
    e.lr = lr;
    e.r1 = r1;
    e.r3 = r3;
    e.addr = addr;
    e.kind = static_cast<uint8_t>(k);
    e.switched = switched;
    std::snprintf(e.name, sizeof e.name, "%s", name ? name : "?");
    if (k == Kind::Enter) ++t_depth;
    else if (k == Kind::Leave && t_depth > 0) --t_depth;
}

void dump(const char* why) {
    /* The ring is process-wide and the crash handler can run on any thread; the
     * first one to get here prints, the rest return so a second signal does not
     * interleave half a ring into the log. */
    if (g_dump_once.fetch_add(1, std::memory_order_relaxed) != 0) return;
    const uint64_t end = g_pos.load(std::memory_order_relaxed);
    const uint64_t count = end < kRingSize ? end : kRingSize;
    const uint64_t start = end - count;
    /* "empty" and "not watching" are different results and an earlier version of
     * this line printed them the same way: with PS3BASE_FIBER_TRACE unset the ring
     * is empty because nothing was ever recorded, not because no import ran. */
    if (!enabled()) {
        LOG_ERROR("fibertrace", "%s: NOT RECORDING -- PS3BASE_FIBER_TRACE is not set, so this "
                  "run gathered no cellFiber events (this is not evidence that none ran)", why);
        return;
    }
    LOG_ERROR("fibertrace", "%s: last %llu cellFiber events of %llu, oldest first%s",
              why, static_cast<unsigned long long>(count),
              static_cast<unsigned long long>(end),
              count == 0 ? " -- THE RING IS EMPTY: the trace was on and no cellFiber import ran" : "");
    for (uint64_t i = start; i < end; i++) {
        const Event& e = g_ring[i % kRingSize];
        LOG_ERROR("fibertrace", "  t=%.6f th=0x%x %-6s %s at 0x%08x r1=0x%08x r3=0x%llx lr=0x%08x%s",
                  e.t, e.thread, kind_name(static_cast<Kind>(e.kind)), e.name, e.addr,
                  e.r1, static_cast<unsigned long long>(e.r3), e.lr,
                  e.switched ? " STACK-SWITCHED" : "");
    }
}

}  // namespace fibertrace
