#include "object.h"

#include "runtime/syncwatch.h"

#include <atomic>
#include <cstdlib>
#include "sync.h"

#include <algorithm>
#include <list>
#include <vector>
#include <cstdlib>

namespace lv2 {

namespace {

struct Mutex : Object {
    std::mutex m;
    std::condition_variable cv;
    uint32_t owner = 0;
    uint32_t count = 0;
    uint32_t cond_count = 0;
    bool recursive = false;
    uint32_t protocol = 0;
};

struct CondWaiter {
    uint32_t tid;
    bool woken = false;
};

struct Cond : Object {
    std::shared_ptr<Mutex> mutex;
    std::condition_variable cv;
    std::list<CondWaiter*> waiters;
};

struct RwLock : Object {
    std::mutex m;
    std::condition_variable cv;
    uint32_t readers = 0;
    uint32_t writer = 0;
    uint32_t waiting_writers = 0;
};

struct Semaphore : Object {
    std::mutex m;
    std::condition_variable cv;
    int32_t value = 0;
    int32_t max = 0;
};

struct EventFlag : Object {
    std::mutex m;
    std::condition_variable cv;
    uint64_t pattern = 0;
    int32_t type = 1;
    uint32_t waiters = 0;
    uint64_t cancel_gen = 0;
};

bool flag_match(uint64_t pattern, uint64_t bits, uint32_t mode) {
    return (mode & 0xF) == 1 ? (pattern & bits) == bits : (pattern & bits) != 0;
}

constexpr uint32_t FLAG_TRACE_ID = 0x98046c00u;

/* The flag that matters belongs to a subsystem whose ids are allocated on the
 * heap, so it changes every run. Watch a list given by PS3BASE_FLAG_TRACE_IDS
 * (comma separated hex) instead of one compiled-in address, and optionally log
 * every set that wakes somebody (PS3BASE_FLAG_TRACE_SETS=1) so the producer of
 * a stuck bit is visible even when its id was not guessed in advance. */
bool trace_sets_all() {
    static const bool on = std::getenv("PS3BASE_FLAG_TRACE_SETS") != nullptr;
    return on;
}

bool flag_watched(uint32_t id) {
    /* getenv is inert under wasm, so the env-driven selection can never enable this
     * there. A compile-time switch makes the trace usable in the build that needs
     * it, and keeps the env path for native. */
#if defined(PS3BASE_FLAG_TRACE_ALL) && PS3BASE_FLAG_TRACE_ALL
    if ((id & 0xff000000u) == 0x98000000u) return true;
#endif
    static const bool all = std::getenv("PS3BASE_FLAG_TRACE_ALL") != nullptr;
    if (all && (id & 0xff000000u) == 0x98000000u) return true;
    static const std::vector<uint32_t> ids = [] {
        std::vector<uint32_t> v;
        const char* e = std::getenv("PS3BASE_FLAG_TRACE_IDS");
        if (!e) return v;
        const char* p = e;
        while (*p) {
            while (*p == ',' || *p == ' ') ++p;
            if (!*p) break;
            v.push_back(static_cast<uint32_t>(std::strtoul(p, nullptr, 16)));
            while (*p && *p != ',' && *p != ' ') ++p;
        }
        return v;
    }();
    if (id == FLAG_TRACE_ID) return true;
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

int flag_waiters(uint32_t id) {
    auto f = get<EventFlag>(id);
    return f ? int(f->waiters) : -1;
}

/* One event flag, traced at every operation, uncapped: a static read cannot say
 * whether a bit is ever produced, only execution order can. */
void flag_trace(const char* op, uint32_t id, uint64_t bits, uint32_t mode,
                uint32_t lr, uint64_t before, uint64_t after) {
    if (!flag_watched(id)) return;
    const guest::Thread* t = guest::current();
    LOG_INFO("flagtrace", "%-8s thread=%s(0x%x) lr=0x%08x bits=0x%llx mode=0x%02x "
             "pattern 0x%llx -> 0x%llx waiters=%d",
             op, t ? t->name.c_str() : "?", t ? t->id : 0, lr,
             static_cast<unsigned long long>(bits), mode,
             static_cast<unsigned long long>(before),
             static_cast<unsigned long long>(after), flag_waiters(id));
}

void flag_consume(uint64_t& pattern, uint64_t bits, uint32_t mode) {
    if (mode & 0x10) pattern &= ~bits;
    else if (mode & 0x20) pattern = 0;
}

}

LV2_SYSCALL(100, sys_mutex_create) {
    uint32_t out = u32arg(ctx, 0);
    uint32_t attr = u32arg(ctx, 1);
    if (!out || !attr) return CELL_EFAULT;
    auto m = std::make_shared<Mutex>();
    m->protocol = vm::read32(attr);
    m->recursive = vm::read32(attr + 4) == 0x10;
    m->name = name8(attr + 32);
    uint32_t id = add_object(0x85000000, m);
    vm::write32(out, id);
    LOG_DEBUG("sync", "sys_mutex_create '%s' -> 0x%x", m->name.c_str(), id);
    return CELL_OK;
}

LV2_SYSCALL(101, sys_mutex_destroy) {
    auto m = get<Mutex>(u32arg(ctx, 0));
    if (!m) return CELL_ESRCH;
    std::lock_guard lk(m->m);
    if (m->cond_count) return CELL_EPERM;
    if (m->owner) return CELL_EBUSY;
    remove_object(u32arg(ctx, 0));
    return CELL_OK;
}

LV2_SYSCALL(102, sys_mutex_lock) {
    const uint32_t id = u32arg(ctx, 0);
    auto m = get<Mutex>(id);
    if (!m) return CELL_ESRCH;
    uint32_t me = current_thread_id();
    std::unique_lock lk(m->m);
    if (m->owner == me) {
        if (!m->recursive) return CELL_EDEADLK;
        m->count++;
        return CELL_OK;
    }
    if (!wait_for(lk, m->cv, u64arg(ctx, 1), [&] { return m->owner == 0; })) return CELL_ETIMEDOUT;
    m->owner = me;
    m->count = 1;
    syncwatch::push(syncwatch::K_MUTEX, id);
    return CELL_OK;
}

LV2_SYSCALL(103, sys_mutex_trylock) {
    const uint32_t id = u32arg(ctx, 0);
    auto m = get<Mutex>(id);
    if (!m) return CELL_ESRCH;
    uint32_t me = current_thread_id();
    std::lock_guard lk(m->m);
    if (m->owner == me) {
        if (!m->recursive) return CELL_EDEADLK;
        m->count++;
        return CELL_OK;
    }
    if (m->owner) return CELL_EBUSY;
    m->owner = me;
    m->count = 1;
    syncwatch::push(syncwatch::K_MUTEX, id);
    return CELL_OK;
}

LV2_SYSCALL(104, sys_mutex_unlock) {
    const uint32_t id = u32arg(ctx, 0);
    auto m = get<Mutex>(id);
    if (!m) return CELL_ESRCH;
    std::lock_guard lk(m->m);
    if (m->owner != current_thread_id()) return CELL_EPERM;
    if (--m->count == 0) {
        m->owner = 0;
        m->cv.notify_all();
        syncwatch::pop(syncwatch::K_MUTEX, id);
    }
    return CELL_OK;
}

LV2_SYSCALL(105, sys_cond_create) {
    uint32_t out = u32arg(ctx, 0);
    auto m = get<Mutex>(u32arg(ctx, 1));
    if (!m) return CELL_ESRCH;
    auto c = std::make_shared<Cond>();
    c->mutex = m;
    if (uint32_t attr = u32arg(ctx, 2)) c->name = name8(attr + 16);
    {
        std::lock_guard lk(m->m);
        m->cond_count++;
    }
    vm::write32(out, add_object(0x86000000, c));
    return CELL_OK;
}

LV2_SYSCALL(106, sys_cond_destroy) {
    auto c = get<Cond>(u32arg(ctx, 0));
    if (!c) return CELL_ESRCH;
    std::lock_guard lk(c->mutex->m);
    if (!c->waiters.empty()) return CELL_EBUSY;
    c->mutex->cond_count--;
    remove_object(u32arg(ctx, 0));
    return CELL_OK;
}

LV2_SYSCALL(107, sys_cond_wait) {
    auto c = get<Cond>(u32arg(ctx, 0));
    if (!c) return CELL_ESRCH;
    /* PS3BASE_COND_TRACE: name the cond, its mutex owner and the timeout. A
     * guest that waits forever on a cond nobody signals hangs without a crash,
     * without an unimplemented import and without a discovery target, which is
     * exactly the shape of the Red Dead Redemption freeze. Counting signals and
     * waits by cond id is what tells "never signalled" from "signalled on the
     * wrong object". */
    static const bool ctrace = std::getenv("PS3BASE_COND_TRACE") != nullptr;
    if (ctrace) {
        static std::atomic<int> n{0};
        if (n.fetch_add(1) < 200)
            LOG_INFO("cond", "wait id=0x%08x mutex=0x%08x owner=0x%x timeout=%llu lr=0x%08x",
                     u32arg(ctx, 0), u32arg(ctx, 1), c->mutex->owner,
                     static_cast<unsigned long long>(u64arg(ctx, 1)),
                     static_cast<uint32_t>(ctx.lr));
    }
    auto& m = *c->mutex;
    uint32_t me = current_thread_id();
    std::unique_lock lk(m.m);
    if (m.owner != me) return CELL_EPERM;
    uint32_t count = m.count;
    m.owner = 0;
    m.count = 0;
    m.cv.notify_all();
    CondWaiter w{me};
    c->waiters.push_back(&w);
    bool ok = wait_for(lk, c->cv, u64arg(ctx, 1), [&] { return w.woken; });
    if (!ok) c->waiters.remove(&w);
    m.cv.wait(lk, [&] { return m.owner == 0; });
    m.owner = me;
    m.count = count;
    return ok ? CELL_OK : CELL_ETIMEDOUT;
}

LV2_SYSCALL(108, sys_cond_signal) {
    auto c = get<Cond>(u32arg(ctx, 0));
    if (!c) return CELL_ESRCH;
    if (std::getenv("PS3BASE_COND_TRACE")) {
        static std::atomic<int> n{0};
        if (n.fetch_add(1) < 200)
            LOG_INFO("cond", "signal id=0x%08x waiters=%zu lr=0x%08x",
                     u32arg(ctx, 0), c->waiters.size(), static_cast<uint32_t>(ctx.lr));
    }
    std::lock_guard lk(c->mutex->m);
    if (!c->waiters.empty()) {
        c->waiters.front()->woken = true;
        c->waiters.pop_front();
        c->cv.notify_all();
    }
    return CELL_OK;
}

LV2_SYSCALL(109, sys_cond_signal_all) {
    auto c = get<Cond>(u32arg(ctx, 0));
    if (!c) return CELL_ESRCH;
    std::lock_guard lk(c->mutex->m);
    for (auto* w : c->waiters) w->woken = true;
    c->waiters.clear();
    c->cv.notify_all();
    return CELL_OK;
}

LV2_SYSCALL(110, sys_cond_signal_to) {
    auto c = get<Cond>(u32arg(ctx, 0));
    if (!c) return CELL_ESRCH;
    uint32_t tid = u32arg(ctx, 1);
    std::lock_guard lk(c->mutex->m);
    for (auto it = c->waiters.begin(); it != c->waiters.end(); ++it) {
        if ((*it)->tid == tid) {
            (*it)->woken = true;
            c->waiters.erase(it);
            c->cv.notify_all();
            return CELL_OK;
        }
    }
    return CELL_EPERM;
}

LV2_SYSCALL(120, sys_rwlock_create) {
    auto r = std::make_shared<RwLock>();
    if (uint32_t attr = u32arg(ctx, 1)) r->name = name8(attr + 24);
    vm::write32(u32arg(ctx, 0), add_object(0x88000000, r));
    return CELL_OK;
}

LV2_SYSCALL(121, sys_rwlock_destroy) {
    auto r = get<RwLock>(u32arg(ctx, 0));
    if (!r) return CELL_ESRCH;
    std::lock_guard lk(r->m);
    if (r->readers || r->writer) return CELL_EBUSY;
    remove_object(u32arg(ctx, 0));
    return CELL_OK;
}

LV2_SYSCALL(122, sys_rwlock_rlock) {
    auto r = get<RwLock>(u32arg(ctx, 0));
    if (!r) return CELL_ESRCH;
    std::unique_lock lk(r->m);
    if (r->writer == current_thread_id()) return CELL_EDEADLK;
    if (!wait_for(lk, r->cv, u64arg(ctx, 1), [&] { return !r->writer && !r->waiting_writers; })) return CELL_ETIMEDOUT;
    r->readers++;
    return CELL_OK;
}

LV2_SYSCALL(123, sys_rwlock_tryrlock) {
    auto r = get<RwLock>(u32arg(ctx, 0));
    if (!r) return CELL_ESRCH;
    std::lock_guard lk(r->m);
    if (r->writer || r->waiting_writers) return CELL_EBUSY;
    r->readers++;
    return CELL_OK;
}

LV2_SYSCALL(124, sys_rwlock_runlock) {
    auto r = get<RwLock>(u32arg(ctx, 0));
    if (!r) return CELL_ESRCH;
    std::lock_guard lk(r->m);
    if (!r->readers) return CELL_EPERM;
    if (--r->readers == 0) r->cv.notify_all();
    return CELL_OK;
}

LV2_SYSCALL(125, sys_rwlock_wlock) {
    auto r = get<RwLock>(u32arg(ctx, 0));
    if (!r) return CELL_ESRCH;
    uint32_t me = current_thread_id();
    std::unique_lock lk(r->m);
    if (r->writer == me) return CELL_EDEADLK;
    r->waiting_writers++;
    bool ok = wait_for(lk, r->cv, u64arg(ctx, 1), [&] { return !r->writer && !r->readers; });
    r->waiting_writers--;
    if (!ok) {
        r->cv.notify_all();
        return CELL_ETIMEDOUT;
    }
    r->writer = me;
    syncwatch::push(syncwatch::K_RWLOCK_W, u32arg(ctx, 0));
    return CELL_OK;
}

LV2_SYSCALL(126, sys_rwlock_trywlock) {
    auto r = get<RwLock>(u32arg(ctx, 0));
    if (!r) return CELL_ESRCH;
    uint32_t me = current_thread_id();
    std::lock_guard lk(r->m);
    if (r->writer == me) return CELL_EDEADLK;
    if (r->writer || r->readers) return CELL_EBUSY;
    r->writer = me;
    return CELL_OK;
}

LV2_SYSCALL(148, _sys_rwlock_trywlock) {
    return sys_rwlock_trywlock(ctx, base);
}

LV2_SYSCALL(127, sys_rwlock_wunlock) {
    auto r = get<RwLock>(u32arg(ctx, 0));
    if (!r) return CELL_ESRCH;
    std::lock_guard lk(r->m);
    if (r->writer != current_thread_id()) return CELL_EPERM;
    r->writer = 0;
    syncwatch::pop(syncwatch::K_RWLOCK_W, u32arg(ctx, 0));
    r->cv.notify_all();
    return CELL_OK;
}

LV2_SYSCALL(90, sys_semaphore_create) {
    uint32_t out = u32arg(ctx, 0);
    uint32_t attr = u32arg(ctx, 1);
    int32_t init = s32arg(ctx, 2);
    int32_t max = s32arg(ctx, 3);
    if (max <= 0 || init < 0 || init > max) return CELL_EINVAL;
    auto s = std::make_shared<Semaphore>();
    s->value = init;
    s->max = max;
    if (attr) s->name = name8(attr + 24);
    vm::write32(out, add_object(0x96000000, s));
    return CELL_OK;
}

LV2_SYSCALL(91, sys_semaphore_destroy) {
    if (!get<Semaphore>(u32arg(ctx, 0))) return CELL_ESRCH;
    remove_object(u32arg(ctx, 0));
    return CELL_OK;
}

LV2_SYSCALL(92, sys_semaphore_wait) {
    const uint32_t id = u32arg(ctx, 0);
    auto s = get<Semaphore>(id);
    if (!s) return CELL_ESRCH;
    std::unique_lock lk(s->m);
    if (!wait_for(lk, s->cv, u64arg(ctx, 1), [&] { return s->value > 0; })) return CELL_ETIMEDOUT;
    s->value--;
    syncwatch::push(syncwatch::K_SEMAPHORE, id);
    return CELL_OK;
}

LV2_SYSCALL(93, sys_semaphore_trywait) {
    const uint32_t id = u32arg(ctx, 0);
    auto s = get<Semaphore>(id);
    if (!s) return CELL_ESRCH;
    std::lock_guard lk(s->m);
    if (s->value <= 0) return CELL_EBUSY;
    s->value--;
    syncwatch::push(syncwatch::K_SEMAPHORE, id);
    return CELL_OK;
}

LV2_SYSCALL(94, sys_semaphore_post) {
    auto s = get<Semaphore>(u32arg(ctx, 0));
    if (!s) return CELL_ESRCH;
    int32_t n = s32arg(ctx, 1);
    if (n < 0) return CELL_EINVAL;
    std::lock_guard lk(s->m);
    if (s->value + n > s->max) return CELL_EBUSY;
    s->value += n;
    for (int32_t k = 0; k < n; k++) syncwatch::pop(syncwatch::K_SEMAPHORE, u32arg(ctx, 0));
    s->cv.notify_all();
    return CELL_OK;
}

LV2_SYSCALL(114, sys_semaphore_get_value) {
    auto s = get<Semaphore>(u32arg(ctx, 0));
    if (!s) return CELL_ESRCH;
    std::lock_guard lk(s->m);
    vm::write32(u32arg(ctx, 1), static_cast<uint32_t>(s->value));
    return CELL_OK;
}

LV2_SYSCALL(82, sys_event_flag_create) {
    uint32_t out = u32arg(ctx, 0);
    uint32_t attr = u32arg(ctx, 1);
    auto f = std::make_shared<EventFlag>();
    f->pattern = u64arg(ctx, 2);
    if (attr) {
        f->type = static_cast<int32_t>(vm::read32(attr + 20));
        f->name = name8(attr + 24);
    }
    const uint32_t id = add_object(0x98000000, f);
    {
        /* getenv is inert under wasm (measured repeatedly in this port), so the
         * env gate made this trace silent exactly where it was needed. */
#if defined(PS3BASE_FLAG_CREATE_TRACE) && PS3BASE_FLAG_CREATE_TRACE
        static const bool on = true;
#else
        static const bool on = std::getenv("PS3BASE_FLAG_TRACE") != nullptr;
#endif
        static std::atomic<int> n{0};
        if (on && n.fetch_add(1) < 200) {
            const guest::Thread* t = guest::current();
            LOG_INFO("flag", "create id=0x%x init=0x%llx thread=%s lr=0x%08x r3=0x%x r4=0x%x r5=0x%llx",
                     id, static_cast<unsigned long long>(f->pattern),
                     t ? t->name.c_str() : "?", static_cast<unsigned>(ctx.lr),
                     static_cast<unsigned>(ctx.gpr[3]), static_cast<unsigned>(ctx.gpr[4]),
                     static_cast<unsigned long long>(ctx.gpr[5]));
        }
    }
    vm::write32(out, id);
    return CELL_OK;
}

LV2_SYSCALL(83, sys_event_flag_destroy) {
    auto f = get<EventFlag>(u32arg(ctx, 0));
    if (!f) return CELL_ESRCH;
    std::lock_guard lk(f->m);
    {
        static const bool on = std::getenv("PS3BASE_FLAG_TRACE") != nullptr;
        static std::atomic<int> n{0};
        if (on && n.fetch_add(1) < 30)
            LOG_INFO("flag", "destroy id=0x%x waiters=%u", u32arg(ctx, 0), f->waiters);
    }
    if (f->waiters) return CELL_EBUSY;
    remove_object(u32arg(ctx, 0));
    return CELL_OK;
}

LV2_SYSCALL(85, sys_event_flag_wait) {
#if defined(PS3BASE_FLAG_ID_PROBE) && PS3BASE_FLAG_ID_PROBE
    static std::atomic<int> n{0};
    if (n.fetch_add(1) < 100000)
        LOG_INFO("flagid", "wait id=%u bits=0x%llx mode=0x%x lr=0x%08x", u32arg(ctx, 0),
                 (unsigned long long)u64arg(ctx, 1), u32arg(ctx, 2), static_cast<uint32_t>(ctx.lr));
#endif
    auto f = get<EventFlag>(u32arg(ctx, 0));
    if (!f) return CELL_ESRCH;
    uint64_t bits = u64arg(ctx, 1);
    uint32_t mode = u32arg(ctx, 2);
    uint32_t result = u32arg(ctx, 3);
    std::unique_lock lk(f->m);
    if (f->type == 1 && f->waiters) return CELL_EPERM;
    uint64_t gen = f->cancel_gen;
    f->waiters++;
    const uint64_t timeout = u64arg(ctx, 4);
    /* The old cap (first 30 waits of the whole run) hid every wait after
     * startup -- the very ones that matter. Count them all, and report a wait
     * only when it actually blocks for more than a moment: an immediately
     * satisfied wait is noise, a blocking one is the answer. */
    const uint64_t before = f->pattern;
    if (flag_watched(u32arg(ctx, 0)) && !flag_match(before, bits, mode)) {
        flag_trace("wait-in", u32arg(ctx, 0), bits, mode,
                   static_cast<uint32_t>(ctx.lr), before, before);
        LOG_INFO("flagtrace", "timeout  value=0x%llx (0 means wait forever)",
                 static_cast<unsigned long long>(u64arg(ctx, 4)));
        const uint32_t st = 0xce5aa0u + 5u;
        LOG_INFO("flagtrace", "state    subsystem[5] value=0x%02x",
                 vm::range_mapped(st, 1) ? vm::ptr(st)[0] : 0xff);
    }
    static const bool trace_flag = std::getenv("PS3BASE_FLAG_TRACE") != nullptr;
    static std::atomic<uint64_t> tl{0};
    const uint64_t seq = trace_flag ? tl.fetch_add(1) : 0;
    const auto t0 = std::chrono::steady_clock::now();
    const bool pre_ok = flag_match(f->pattern, bits, mode);
    /* Log at ENTRY, not only at exit: a wait that never returns was invisible,
     * and the wait that matters -- the chunk thread's -- never returns. */
    if (trace_flag && !pre_ok) {
        static std::atomic<uint64_t> n{0};
        if (n.fetch_add(1) < 200)
            LOG_INFO("flagin", "#%llu seq=%llu thread=0x%x id=0x%x bits=0x%llx mode=0x%x "
                     "timeout=%llu pattern=0x%llx -> will block",
                     static_cast<unsigned long long>(n.load() + 1),
                     static_cast<unsigned long long>(seq),
                     static_cast<unsigned>(ctx.gpr[13]), u32arg(ctx, 0),
                     static_cast<unsigned long long>(bits), mode,
                     static_cast<unsigned long long>(timeout),
                     static_cast<unsigned long long>(f->pattern));
    }
    bool ok = wait_for(lk, f->cv, timeout, [&] { return flag_match(f->pattern, bits, mode) || f->cancel_gen != gen; });
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
    if (trace_flag && !pre_ok) {
        static std::atomic<uint64_t> blocked{0};
        const uint64_t nb = blocked.fetch_add(1);
        if (nb < 400)
            LOG_INFO("flag", "blocked #%llu seq=%llu id=0x%x bits=0x%llx mode=0x%x timeout=%llu "
                     "pattern=0x%llx -> ok=%d canceled=%d waited=%lldus",
                     static_cast<unsigned long long>(nb + 1), static_cast<unsigned long long>(seq),
                     u32arg(ctx, 0), static_cast<unsigned long long>(bits), mode,
                     static_cast<unsigned long long>(timeout),
                     static_cast<unsigned long long>(f->pattern), int(ok),
                     int(f->cancel_gen != gen), static_cast<long long>(us));
    }
    f->waiters--;
    if (flag_watched(u32arg(ctx, 0)))
        flag_trace(ok ? "wait-ok" : "wait-to", u32arg(ctx, 0), bits, mode,
                   static_cast<uint32_t>(ctx.lr), before, f->pattern);
    if (result) vm::write64(result, f->pattern);
    if (f->cancel_gen != gen) return CELL_ECANCELED;
    if (!ok) return CELL_ETIMEDOUT;
    flag_consume(f->pattern, bits, mode);
    return CELL_OK;
}

LV2_SYSCALL(86, sys_event_flag_trywait) {
    auto f = get<EventFlag>(u32arg(ctx, 0));
    if (!f) return CELL_ESRCH;
    uint64_t bits = u64arg(ctx, 1);
    uint32_t mode = u32arg(ctx, 2);
    std::lock_guard lk(f->m);
    if (uint32_t result = u32arg(ctx, 3)) vm::write64(result, f->pattern);
    if (!flag_match(f->pattern, bits, mode)) return CELL_EBUSY;
    flag_consume(f->pattern, bits, mode);
    return CELL_OK;
}


int64_t event_flag_set_bits_traced(uint32_t id, uint64_t bits, uint32_t from_lr,
                                  const char* who);

int64_t event_flag_set_bits(uint32_t id, uint64_t bits) {
    return event_flag_set_bits_traced(id, bits, 0, nullptr);
}

int64_t event_flag_set_bits_traced(uint32_t id, uint64_t bits, uint32_t from_lr,
                                  const char* who) {
    auto f = get<EventFlag>(id);
    if (!f) return CELL_ESRCH;
    std::lock_guard lk(f->m);
    const uint64_t before = f->pattern;
    f->pattern |= bits;
    if (trace_sets_all() && f->waiters && f->pattern != before) {
        const char* by = who ? who : (guest::current() ? guest::current()->name.c_str() : "?");
        static std::atomic<uint64_t> ns{0};
        const uint64_t n = ns.fetch_add(1);
        if (n < 400)
            LOG_INFO("flagset", "#%llu id=0x%x set=0x%llx by=%s lr=0x%08x pattern 0x%llx -> 0x%llx waiters=%u",
                     static_cast<unsigned long long>(n + 1), id,
                     static_cast<unsigned long long>(bits), by,
                     from_lr, static_cast<unsigned long long>(before),
                     static_cast<unsigned long long>(f->pattern), f->waiters);
    }
    if (flag_watched(id) && !who)
        who = guest::current() ? guest::current()->name.c_str() : "?";
    if (flag_watched(id))
        LOG_INFO("flagtrace", "set      from=%s lr=0x%08x bits=0x%llx pattern 0x%llx -> 0x%llx waiters=%d",
                 who, from_lr, static_cast<unsigned long long>(bits),
                 static_cast<unsigned long long>(before),
                 static_cast<unsigned long long>(f->pattern), flag_waiters(id));
    f->cv.notify_all();
    return CELL_OK;
}

LV2_SYSCALL(87, sys_event_flag_set) {
#if defined(PS3BASE_FLAG_ID_PROBE) && PS3BASE_FLAG_ID_PROBE
    /* Which event flag does ./4J Tile update wait on, and does anyone ever set it?
     * An uncapped count per id answers both without guessing. */
    static std::atomic<uint64_t> sets[64];
    static std::atomic<int> n{0};
    const uint32_t id = u32arg(ctx, 0);
    if (n.fetch_add(1) < 400)
        LOG_INFO("flagid", "set id=%u bits=0x%llx lr=0x%08x", id,
                 (unsigned long long)u64arg(ctx, 1), static_cast<uint32_t>(ctx.lr));
    if (id < 64) sets[id].fetch_add(1);
#endif
    return event_flag_set_bits_traced(u32arg(ctx, 0), u64arg(ctx, 1),
                                     static_cast<uint32_t>(ctx.lr), nullptr);
}

LV2_SYSCALL(118, sys_event_flag_clear) {
    auto f = get<EventFlag>(u32arg(ctx, 0));
    if (!f) return CELL_ESRCH;
    std::lock_guard lk(f->m);
    const uint64_t before = f->pattern;
    /* refs/rpcs3/.../sys_event_flag.cpp:470: pattern &= bitptn. bitptn is the
     * set of bits to KEEP, not the mask to remove. */
    f->pattern &= u64arg(ctx, 1);
    flag_trace("clear", u32arg(ctx, 0), u64arg(ctx, 1), 0,
               static_cast<uint32_t>(ctx.lr), before, f->pattern);
    return CELL_OK;
}

LV2_SYSCALL(132, sys_event_flag_cancel) {
    auto f = get<EventFlag>(u32arg(ctx, 0));
    if (!f) return CELL_ESRCH;
    std::lock_guard lk(f->m);
    if (uint32_t num = u32arg(ctx, 1)) vm::write32(num, f->waiters);
    flag_trace("cancel", u32arg(ctx, 0), 0, 0, static_cast<uint32_t>(ctx.lr),
               f->pattern, f->pattern);
    f->cancel_gen++;
    f->cv.notify_all();
    return CELL_OK;
}

LV2_SYSCALL(139, sys_event_flag_get) {
    auto f = get<EventFlag>(u32arg(ctx, 0));
    if (!f) return CELL_ESRCH;
    std::lock_guard lk(f->m);
    vm::write64(u32arg(ctx, 1), f->pattern);
    return CELL_OK;
}

}