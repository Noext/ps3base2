/* ps3base -- the sysPrxForUser lightweight-mutex table, shared with its test.
 *
 * This is the body of the four sysPrxForUser lwmutex imports, kept out of the
 * HLE file so a test can drive the SAME code rather than a copy of it. A test
 * that re-implements the thing under test proves nothing.
 *
 * The guest structure (refs/jdc-ps3recomp/libs/system/sysPrxForUser.h:36):
 *
 *     u64 lock_var;          // +0, bit 0 set while held
 *     u32 attribute;         // +8
 *     u32 recursive_count;   // +12
 *     u32 sleep_queue;       // +16, nonzero once registered
 *
 * Two properties are load-bearing and were both broken:
 *
 *   OWNERSHIP. A std::mutex does not know which guest thread holds it, and
 *   unlock() from a thread that does not is undefined behaviour -- on glibc it
 *   silently releases the lock. The title unlocks lwmutexes on teardown paths
 *   that are not always the acquiring thread, so without an owner the next
 *   thread walks into a critical section the first is still executing: a list
 *   edited while it is being walked, which is what corrupts +28/+32 of a node.
 *
 *   DESTROY. Erasing the map entry while a thread is inside drops the last
 *   reference; the next lock then finds nothing, creates a fresh UNLOCKED
 *   mutex at the same address, and enters immediately. The entry is marked
 *   dead and reaped by the unlock that finally releases it, so one address
 *   keeps one host mutex for as long as anyone can be inside it.
 */
#ifndef PS3BASE_HLE_LWMUTEX_H
#define PS3BASE_HLE_LWMUTEX_H

#include "runtime/log.h"
#include "runtime/memory.h"

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace hle {
namespace lwmutex {

/* Supplied by each translation unit: the id of the guest thread making the
 * call. 0 means "no guest thread" (a host-side caller, or a test). */
uint32_t me();

constexpr uint32_t OFF_LOCK_VAR = 0;
constexpr uint32_t OFF_ATTRIBUTE = 8;
constexpr uint32_t OFF_RECURSIVE = 12;
constexpr uint32_t OFF_SLEEP_QUEUE = 16;
constexpr uint32_t SIZE = 24;

constexpr uint32_t SYS_SYNC_RECURSIVE = 0x10;

/* The guest lr of the import call, set by the HLE wrapper. */
inline uint32_t& caller_lr() {
    static thread_local uint32_t v = 0;
    return v;
}

inline bool trace_on() {
    static const bool v = std::getenv("PS3BASE_LWMUX_TRACE") != nullptr;
    return v;
}

/* PS3BASE_LWMUX_TRACE=1: every operation with the guest thread, the caller's lr,
 * the guest lock word and the owner the HLE believes in. Two patterns in this
 * log are the mechanism the crash implies:
 *   'owner-mismatch'  an unlock from a thread that is not the owner
 *   'enter-busy'      a thread entered while another was still recorded owner
 * Either before a crash is the proof; their absence over a long run retires it. */
inline void trace(const char* op, uint32_t ea, int64_t rc, uint32_t owner, uint32_t caller) {
    if (!trace_on()) return;
    const uint32_t word = ea && vm::range_mapped(ea, 4) ? vm::read32(ea) : 0;
    LOG_INFO("lwmux", "%s ea=0x%08x by tid 0x%x lr=0x%08x word=0x%08x owner=0x%x rc=0x%llx",
             op, ea, me(), caller, word, owner, static_cast<unsigned long long>(rc));
}
inline void trace_mismatch(uint32_t ea, uint32_t owner, uint32_t caller) {
    LOG_WARN("lwmux", "owner-mismatch ea=0x%08x unlock by tid 0x%x lr=0x%08x but owner=0x%x",
             ea, me(), caller, owner);
}

struct Entry {
    std::mutex m;
    uint32_t owner = 0;
    uint32_t depth = 0;
    bool recursive = false;
    bool dead = false;
};

inline std::mutex& table_lock() {
    static std::mutex m;
    return m;
}

inline std::unordered_map<uint32_t, std::shared_ptr<Entry>>& table() {
    static std::unordered_map<uint32_t, std::shared_ptr<Entry>> t;
    return t;
}

inline std::shared_ptr<Entry> entry_for(uint32_t ea) {
    std::lock_guard lk(table_lock());
    auto it = table().find(ea);
    if (it != table().end()) return it->second;
    auto e = std::make_shared<Entry>();
    table().emplace(ea, e);
    return e;
}

/* Find without creating: unlock and destroy must never resurrect an entry. */
inline std::shared_ptr<Entry> find_entry(uint32_t ea) {
    std::lock_guard lk(table_lock());
    auto it = table().find(ea);
    return it == table().end() ? nullptr : it->second;
}

inline void reap_if_dead(uint32_t ea, const std::shared_ptr<Entry>& e) {
    if (!e || !e->dead) return;
    std::lock_guard lk(table_lock());
    auto it = table().find(ea);
    if (it != table().end() && it->second == e) table().erase(it);
}

inline void mark_held(uint32_t ea) {
    vm::write32(ea + OFF_LOCK_VAR, vm::read32(ea + OFF_LOCK_VAR) | 1u);
}
inline void mark_free(uint32_t ea) {
    vm::write32(ea + OFF_LOCK_VAR, vm::read32(ea + OFF_LOCK_VAR) & ~1u);
}

inline int64_t lock(uint32_t ea) {
    if (!ea || !vm::range_mapped(ea, SIZE)) return static_cast<int32_t>(0x80010002);
    /* A statically initialised mutex has sleep_queue == 0; give it one so the
     * guest's own bookkeeping stays consistent. */
    if (!vm::read32(ea + OFF_SLEEP_QUEUE)) vm::write32(ea + OFF_SLEEP_QUEUE, 1);
    const auto e = entry_for(ea);

    /* Recursive re-acquisition by the owner, without touching the host lock:
     * the host mutex is not recursive and locking it twice would deadlock. */
    {
        std::lock_guard lk(table_lock());
        if (e->owner && e->owner == me() && e->recursive) {
            e->depth++;
            mark_held(ea);
            return 0;
        }
    }
    {
        std::lock_guard lk(table_lock());
        if (e->owner && e->owner != me()) trace("enter-busy", ea, 0, e->owner, caller_lr());
    }
    if (e->m.try_lock()) {
        std::lock_guard lk(table_lock());
        e->owner = me();
        e->depth = 1;
        e->dead = false;
        mark_held(ea);
        trace("lock-fast", ea, 0, e->owner, caller_lr());
        return 0;
    }
    /* Contended: block. The guest allocates one host thread per guest thread,
     * so blocking here is correct behaviour and not a host stall. */
    trace("lock-block", ea, 0, 0, caller_lr());
    e->m.lock();
    {
        std::lock_guard lk(table_lock());
        e->owner = me();
        e->depth = 1;
        e->dead = false;
    }
    mark_held(ea);
    trace("lock-slow", ea, 0, me(), caller_lr());
    return 0;
}

inline int64_t trylock(uint32_t ea) {
    if (!ea || !vm::range_mapped(ea, SIZE)) return static_cast<int32_t>(0x80010002);
    const auto e = entry_for(ea);
    {
        std::lock_guard lk(table_lock());
        if (e->owner && e->owner == me() && e->recursive) {
            e->depth++;
            mark_held(ea);
            return 0;
        }
    }
    if (!e->m.try_lock()) return static_cast<int32_t>(0x8001000A); /* EBUSY */
    {
        std::lock_guard lk(table_lock());
        e->owner = me();
        e->depth = 1;
        e->dead = false;
    }
    mark_held(ea);
    return 0;
}

inline int64_t unlock(uint32_t ea) {
    if (!ea || !vm::range_mapped(ea, SIZE)) return static_cast<int32_t>(0x80010002);
    const auto e = find_entry(ea);
    if (!e) {
        /* No host lock was ever taken for this address. Clearing the guest word
         * is all that can be done, and is what the structure expects. */
        mark_free(ea);
        return 0;
    }
    {
        std::lock_guard lk(table_lock());
        /* An unlock from a thread that does not own the mutex is refused: the
         * console returns an error here, and silently releasing is exactly what
         * lets a second thread into a section the first is still executing. */
        if (e->owner && e->owner != me()) {
            trace_mismatch(ea, e->owner, caller_lr());
            return static_cast<int32_t>(0x80010009); /* EPERM */
        }
        if (e->depth > 1) {
            e->depth--;
            mark_free(ea);
            return 0;
        }
        e->owner = 0;
        e->depth = 0;
    }
    mark_free(ea);
    e->m.unlock();
    trace("unlock", ea, 0, 0, caller_lr());
    reap_if_dead(ea, e);
    return 0;
}

inline int64_t create(uint32_t ea, uint32_t attr) {
    if (!ea || !vm::range_mapped(ea, SIZE)) return static_cast<int32_t>(0x80010002);
    const uint32_t attribute = (attr && vm::range_mapped(attr, 16)) ? vm::read32(attr + 4) : 0;
    vm::write32(ea + OFF_LOCK_VAR, 0);
    vm::write32(ea + OFF_ATTRIBUTE, attribute);
    vm::write32(ea + OFF_RECURSIVE, 0);
    vm::write32(ea + OFF_SLEEP_QUEUE, 1);
    /* A create on an address that still has a live entry must not replace the
     * host mutex a thread may be blocking on. */
    const auto e = entry_for(ea);
    {
        std::lock_guard lk(table_lock());
        e->recursive = (attribute & SYS_SYNC_RECURSIVE) != 0;
        e->dead = false;
    }
    return 0;
}

inline int64_t destroy(uint32_t ea) {
    const auto e = find_entry(ea);
    if (!e) {
        if (ea && vm::range_mapped(ea, SIZE)) vm::write32(ea + OFF_SLEEP_QUEUE, 0);
        return 0;
    }
    bool held;
    {
        std::lock_guard lk(table_lock());
        held = e->owner != 0;
        e->dead = true;
    }
    trace("destroy", ea, held ? 1 : 0, e->owner, caller_lr());
    if (held) {
        /* Still inside a critical section. Marked dead; the unlocking thread
         * reaps it, so the address keeps the same host mutex until then and no
         * second thread can create a fresh, unlocked one. */
        return 0;
    }
    if (ea && vm::range_mapped(ea, SIZE)) {
        vm::write32(ea + OFF_LOCK_VAR, 0);
        vm::write32(ea + OFF_SLEEP_QUEUE, 0);
    }
    reap_if_dead(ea, e);
    return 0;
}

/* Test-only: forget every registration. */
inline void reset_all() {
    std::lock_guard lk(table_lock());
    table().clear();
}

}  // namespace lwmutex
}  // namespace hle

#endif
