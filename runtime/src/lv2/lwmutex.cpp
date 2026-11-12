#include "lwsync.h"

#include "runtime/syncwatch.h"

#include <atomic>
#include <cstdlib>
#include <cstdio>

namespace lv2 {

/* PROBE (L1). The four _sys_lwmutex_* syscalls are what the firmware's own
 * sysPrxForUser wrapper calls; the guest structure they name is a DISJOINT fact
 * from the ID they are given. This records, on the real run, both:
 *
 *   - create: the arguments, the id the runtime minted, and the 24 bytes of the
 *     guest sys_lwmutex_t the call named, immediately after the syscall returns
 *     its id (the wrapper has not yet copied it into +16 at that point);
 *   - every failing lock/unlock/trylock: the VALUE the guest passed as an id,
 *     plus -- when that value is a mapped guest address -- the 24 bytes living
 *     there, because "0x0165afc8" as an id and 0x0165afc8 as the address of a
 *     sys_lwmutex_t are indistinguishable in the register.
 *
 * Zero cost when PS3BASE_LWMUX_PROBE is unset. */
namespace {

bool probe_on() {
    static const bool v = std::getenv("PS3BASE_LWMUX_PROBE") != nullptr;
    return v;
}

/* Failure counts are always maintained, even with the probe off: the number is
 * the measurement the task asks for, and an atomic increment is free.
 *
 * The log itself is sampled, not capped: the first 400 of each operation print
 * in full, then every 400th prints with its running count. An uncapped log at
 * this rate starves the run and measures the logging instead of the runtime;
 * a capped one hides the total. The last sampled line carries the true count. */
std::atomic<uint64_t>& fail_count(int op) {
    static std::atomic<uint64_t> c[8];
    return c[op];
}

enum { LW_F_LOCK, LW_F_UNLOCK, LW_F_TRYLOCK, LW_F_UNLOCK2, LW_F_DESTROY, LW_F_EPERM, LW_F_MAX };

void probe_dump_struct(const char* tag, uint32_t ea, const char* extra) {
    if (!probe_on()) return;
    if (ea && vm::range_mapped(ea, 32)) {
        LOG_INFO("lwprobe", "%s ea=0x%08x lock_var=%08x%08x attribute=%08x recursive=%08x sleep_queue=%08x pad=%08x%s%s",
                 tag, ea, vm::read32(ea), vm::read32(ea + 4), vm::read32(ea + 8),
                 vm::read32(ea + 12), vm::read32(ea + 16), vm::read32(ea + 20),
                 extra && *extra ? " " : "", extra ? extra : "");
    } else {
        LOG_INFO("lwprobe", "%s ea=0x%08x (not 32 mapped bytes)%s%s", tag, ea,
                 extra && *extra ? " " : "", extra ? extra : "");
    }
}

struct Counter {
    std::atomic<uint64_t> n{0};
    std::atomic<uint64_t> logged{0};
};

Counter& counter(int op) {
    static Counter c[LW_F_MAX];
    return c[op];
}

const char* counter_name(int op) {
    switch (op) {
        case LW_F_LOCK: return "lock";
        case LW_F_UNLOCK: return "unlock";
        case LW_F_TRYLOCK: return "trylock";
        case LW_F_UNLOCK2: return "unlock2";
        case LW_F_DESTROY: return "destroy";
        case LW_F_EPERM: return "unlock-EPERM";
        default: return "?";
    }
}

/* The registry, for the case that actually occurs: a lock whose id argument is
 * 0. The id is a VALUE, so the first question is which object the caller meant,
 * and the only way to answer it is to list every live lwmutex with the contents
 * of the guest structure it was created over. A live object whose +16 is 0 is
 * the ghost: it exists here, and its control block says it has no queue. */
void probe_registry(const char* why, int limit) {
    static std::atomic<int> calls{0};
    if (!probe_on()) return;
    if (calls.fetch_add(1) >= limit) return;
    LOG_WARN("lwprobe", "registry at %s", why);
    uint32_t n = 0;
    for (uint32_t idx = 0; idx < 8192; idx++) {
        const uint32_t id = 0x95000000u + idx * 0x100u;
        auto m = get<LwMutex>(id);
        if (!m) continue;
        n++;
        if (n > 24) { LOG_WARN("lwprobe", "  ... more than 24 live lwmutexes"); break; }
        const uint32_t ea = m->control;
        if (ea && vm::range_mapped(ea, 24)) {
            LOG_WARN("lwprobe", "  id=0x%08x control=0x%08x attr=%08x rec=%08x sleep_queue=%08x signaled=%d",
                     id, ea, vm::read32(ea + 8), vm::read32(ea + 12), vm::read32(ea + 16), m->signaled);
        } else {
            LOG_WARN("lwprobe", "  id=0x%08x control=0x%08x (unmapped) signaled=%d", id, ea, m->signaled);
        }
    }
    LOG_WARN("lwprobe", "  %u live lwmutex objects", n);
}

/* Called once per failure: counts always, prints the first 400 then every 400th
 * (and always the 1st, which is the one the task asks to read raw).
 *
 * The callee-saved guest registers are the only way to name the STRUCTURE when
 * the argument is not an id at all: the firmware wrapper keeps the
 * sys_lwmutex_t pointer in a callee-saved register across the sc, so r29..r31
 * at the syscall frame contain it. Reporting them turns "the argument was 0"
 * into "the argument was 0 and the caller's lwmutex_t is at 0x...". */
void probe_fail(int op, uint32_t id, int64_t rc, const ppu::Context& ctx) {
    const uint64_t n = counter(op).n.fetch_add(1) + 1;
    if (!probe_on()) return;
    const uint64_t seen = counter(op).logged.fetch_add(1) + 1;
    if (seen > 400 && (seen % 400) != 0) return;
    const uint32_t lr = static_cast<uint32_t>(ctx.lr);
    char extra[320];
    std::snprintf(extra, sizeof extra,
                  "count=%llu rc=0x%llx cia=0x%08x lr=0x%08x tid=0x%x r28=0x%08x r29=0x%08x r30=0x%08x r31=0x%08x",
                  static_cast<unsigned long long>(n),
                  static_cast<unsigned long long>(rc), static_cast<uint32_t>(ctx.cia), lr,
                  current_thread_id(),
                  static_cast<uint32_t>(ctx.gpr[28]), static_cast<uint32_t>(ctx.gpr[29]),
                  static_cast<uint32_t>(ctx.gpr[30]), static_cast<uint32_t>(ctx.gpr[31]));
    probe_dump_struct(counter_name(op), id, extra);
    if (n == 1) probe_registry("first failure", 2);
    /* The guest structure the caller believes in: for a lock, the wrapper's
     * callee-saved copy of the lwmutex_t pointer is one of r29..r31, and the
     * one that points at a mapped 24-byte block whose +16 is what the syscall
     * was NOT given. Dump every candidate so no guess is needed. */
    const uint32_t cands[3] = {static_cast<uint32_t>(ctx.gpr[29]), static_cast<uint32_t>(ctx.gpr[30]),
                               static_cast<uint32_t>(ctx.gpr[31])};
    for (int i = 0; i < 3; i++) {
        const uint32_t ea = cands[i];
        if (!ea || !vm::range_mapped(ea, 24)) continue;
        char tag[32];
        std::snprintf(tag, sizeof tag, "  caller r%d", 29 + i);
        probe_dump_struct(tag, ea, "");
    }
}

/* The totals, printed at exit and at every process/thread status dump. */
void probe_report_impl(const char* where) {
    uint64_t t[LW_F_MAX];
    for (int i = 0; i < LW_F_MAX; i++) t[i] = counter(i).n.load();
    if (!t[LW_F_LOCK] && !t[LW_F_UNLOCK] && !t[LW_F_TRYLOCK] && !t[LW_F_UNLOCK2] && !t[LW_F_DESTROY] && !t[LW_F_EPERM]) {
        LOG_INFO("lwprobe", "counts at %s: all zero", where);
        return;
    }
    LOG_WARN("lwprobe", "counts at %s: lock-ESRCH=%llu unlock-ESRCH=%llu trylock-ESRCH=%llu unlock2-ESRCH=%llu destroy-ESRCH=%llu unlock-EPERM=%llu",
             where, static_cast<unsigned long long>(t[LW_F_LOCK]),
             static_cast<unsigned long long>(t[LW_F_UNLOCK]),
             static_cast<unsigned long long>(t[LW_F_TRYLOCK]),
             static_cast<unsigned long long>(t[LW_F_UNLOCK2]),
             static_cast<unsigned long long>(t[LW_F_DESTROY]),
             static_cast<unsigned long long>(t[LW_F_EPERM]));
}

}  // namespace

void lwmutex_probe_report(const char* where) { probe_report_impl(where); }

/* An atexit report is the one that survives a run that is killed by timeout:
 * main() returns normally after the guest exits, and a SIGINT path still runs
 * the handlers. A run that is SIGKILLed prints nothing, which is why the
 * hitch/status dump also carries the counts. */
namespace {
struct ReportAtExit {
    ReportAtExit() { std::atexit([] { probe_report_impl("exit"); }); }
} g_report_at_exit;
}

int64_t LwMutex::sleep(std::unique_lock<std::mutex>& lk, LwWaiter& w, uint64_t timeout) {
    sleepers.push_back(&w);
    bool ok = wait_for(lk, cv, timeout, [&] { return w.woken; });
    if (!ok) {
        sleepers.remove(&w);
        return CELL_ETIMEDOUT;
    }
    return w.result;
}

void LwMutex::release(int64_t result) {
    if (sleepers.empty()) {
        signaled |= result == CELL_OK ? 1 : static_cast<int>(0x80000000u);
        return;
    }
    LwWaiter* w = sleepers.front();
    sleepers.pop_front();
    w->woken = true;
    w->result = result;
    cv.notify_all();
}

void lwmutex_debug(uint32_t id) {
    auto m = get<LwMutex>(id);
    if (!m) return;
    std::lock_guard lk(m->m);
    LOG_INFO("status", "   lwmutex 0x%x '%s' control 0x%08x owner 0x%08x waiters %u attr 0x%x recursive %u signaled 0x%x sleepers %zu lwcond_waiters %d", id,
             m->name.c_str(), m->control, vm::read32(m->control), vm::read32(m->control + 4), vm::read32(m->control + 8), vm::read32(m->control + 12),
             m->signaled, m->sleepers.size(), m->lwcond_waiters);
}

LV2_SYSCALL(95, _sys_lwmutex_create) {
    uint32_t out = u32arg(ctx, 0);
    const uint32_t protocol = u32arg(ctx, 1);
    const uint32_t control = u32arg(ctx, 2);
    auto m = std::make_shared<LwMutex>();
    m->protocol = protocol;
    m->control = control;
    if (s32arg(ctx, 3)) m->name = name_u64(u64arg(ctx, 4));
    const uint32_t id = add_object(0x95000000, m);
    vm::write32(out, id);
    if (probe_on()) {
        char extra[200];
        std::snprintf(extra, sizeof extra,
                      "id=0x%08x protocol=0x%x control=0x%08x has_name=%d name=0x%llx lr=0x%08x tid=0x%x",
                      id, protocol, control, s32arg(ctx, 3),
                      static_cast<unsigned long long>(u64arg(ctx, 4)),
                      static_cast<uint32_t>(ctx.cia), current_thread_id());
        probe_dump_struct("create-control", control, extra);
        probe_dump_struct("create-out", out, extra);
    }
    return CELL_OK;
}

LV2_SYSCALL(96, _sys_lwmutex_destroy) {
    auto m = get<LwMutex>(u32arg(ctx, 0));
    if (!m) { probe_fail(LW_F_DESTROY, u32arg(ctx, 0), CELL_ESRCH, ctx); return CELL_ESRCH; }
    std::lock_guard lk(m->m);
    if (!m->sleepers.empty() || m->lwcond_waiters) return CELL_EBUSY;
    remove_object(u32arg(ctx, 0));
    return CELL_OK;
}

LV2_SYSCALL(97, _sys_lwmutex_lock) {
    const uint32_t id = u32arg(ctx, 0);
    auto m = get<LwMutex>(id);
    if (!m) { probe_fail(LW_F_LOCK, id, CELL_ESRCH, ctx); return CELL_ESRCH; }
    std::unique_lock lk(m->m);
    if (int s = m->signaled) {
        m->signaled = 0;
        if (s & 1) syncwatch::push(syncwatch::K_LWMUTEX, id);
        return (s & 1) ? CELL_OK : CELL_EBUSY;
    }
    LwWaiter w{current_thread_id()};
    int64_t rc = m->sleep(lk, w, u64arg(ctx, 1));
    if (rc == CELL_OK) syncwatch::push(syncwatch::K_LWMUTEX, id);
    return rc;
}

LV2_SYSCALL(99, _sys_lwmutex_trylock) {
    const uint32_t id = u32arg(ctx, 0);
    auto m = get<LwMutex>(id);
    if (!m) { probe_fail(LW_F_TRYLOCK, id, CELL_ESRCH, ctx); return CELL_ESRCH; }
    std::lock_guard lk(m->m);
    if (m->signaled & 1) {
        m->signaled = 0;
        syncwatch::push(syncwatch::K_LWMUTEX, id);
        return CELL_OK;
    }
    return CELL_EBUSY;
}

LV2_SYSCALL(98, _sys_lwmutex_unlock) {
    const uint32_t id = u32arg(ctx, 0);
    auto m = get<LwMutex>(id);
    if (!m) { probe_fail(LW_F_UNLOCK, id, CELL_ESRCH, ctx); return CELL_ESRCH; }
    std::lock_guard lk(m->m);
    syncwatch::pop(syncwatch::K_LWMUTEX, id);
    m->release(CELL_OK);
    return CELL_OK;
}

LV2_SYSCALL(117, _sys_lwmutex_unlock2) {
    auto m = get<LwMutex>(u32arg(ctx, 0));
    if (!m) { probe_fail(LW_F_UNLOCK2, u32arg(ctx, 0), CELL_ESRCH, ctx); return CELL_ESRCH; }
    std::lock_guard lk(m->m);
    m->release(CELL_EBUSY);
    return CELL_OK;
}

}