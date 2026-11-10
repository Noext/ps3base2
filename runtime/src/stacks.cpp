#include "runtime/stacks.h"

#include "runtime/host.h"
#include "runtime/log.h"
#include "runtime/memory.h"
#include "runtime/mem.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

namespace stacks {

namespace {

/* 64 KB below every guest stack, PROT_NONE. Chosen to match the requirement and
 * because a 4 KB guard can be stepped over by a large vectorised store. */
constexpr uint32_t GUARD_BYTES = 0x10000;

std::mutex g_m;
std::vector<Entry> g_list;

Entry* find_locked(uint32_t id) {
    for (auto& e : g_list)
        if (e.id == id) return &e;
    return nullptr;
}

}  // namespace

uint32_t split(uint32_t id, const char* name, uint32_t requested, uint32_t block_base, uint32_t block_size) {
    if (block_size < GUARD_BYTES + 0x1000) return 0;
    const uint32_t allocated = block_size - GUARD_BYTES;
    const uint32_t guard = block_base;
    const uint32_t lo = block_base + GUARD_BYTES;
    if (!mem::protect(vm::g_base, guard, GUARD_BYTES, mem::Prot::None)) {
        LOG_WARN("stacks", "thread %u '%s': guard at 0x%08x could not be protected", id, name ? name : "?", guard);
        return 0;
    }
    /* In bitset mode Prot::None is not enforced by the CPU: the guard is a
     * SOFTWARE range check. Record it so the check has a list to consult. */
    mem::register_guard(guard, GUARD_BYTES);
    Entry e{};
    e.id = id;
    std::snprintf(e.name, sizeof e.name, "%s", name ? name : "?");
    e.requested = requested;
    e.allocated = allocated;
    e.lo = lo;
    e.hi = lo + allocated;
    e.guard_lo = guard;
    e.guard_size = GUARD_BYTES;
    e.r1_min = 0xFFFFFFFFu;
    e.calls = 0;
    std::lock_guard lk(g_m);
    g_list.push_back(e);
    LOG_INFO("stacks", "thread %u '%s' requested 0x%x allocated 0x%x range 0x%08x..0x%08x guard 0x%08x..0x%08x%s",
             id, e.name, requested, allocated, e.lo, e.hi, e.guard_lo, e.guard_lo + GUARD_BYTES,
             lo == guard + GUARD_BYTES ? " (adjacent)" : " (NOT ADJACENT -- bug)");
    return lo;
}

void remove(uint32_t id) {
    std::lock_guard lk(g_m);
    for (auto it = g_list.begin(); it != g_list.end(); ++it) {
        if (it->id != id) continue;
        /* Leave the guard reserved: unmapping it could let a later allocation
         * land there, and a stale overflow would then be invisible. */
        g_list.erase(it);
        return;
    }
}

void note_sp(uint32_t id, uint32_t r1) {
    if (!r1) return;
    std::lock_guard lk(g_m);
    if (Entry* e = find_locked(id)) {
        e->calls++;
        if (r1 < e->r1_min) e->r1_min = r1;
    }
}

bool contains(uint32_t ea, char* name, size_t n) {
    std::lock_guard lk(g_m);
    for (const auto& e : g_list) {
        if (ea >= e.guard_lo && ea < e.hi) {
            if (name && n) std::snprintf(name, n, "%s", e.name);
            return true;
        }
    }
    return false;
}

void dump() {
    std::lock_guard lk(g_m);
    LOG_INFO("stacks", "%zu guest stacks tracked", g_list.size());
    std::vector<Entry> v = g_list;
    std::sort(v.begin(), v.end(), [](const Entry& a, const Entry& b) { return a.lo < b.lo; });
    for (const auto& e : v) {
        /* Distance from the lowest r1 to the guard top is the margin left before
         * the stack would have hit the guard: the number that says whether this
         * thread was ever close to overflowing. */
        const long margin = e.r1_min == 0xFFFFFFFFu ? -1
                                                    : static_cast<long>(e.r1_min) - static_cast<long>(e.guard_lo + GUARD_BYTES);
        LOG_INFO("stacks",
                 "  '%s' id %u range 0x%08x..0x%08x alloc 0x%x (req 0x%x) guard 0x%08x r1_min 0x%08x margin %ld bytes spp %u",
                 e.name, e.id, e.lo, e.hi, e.allocated, e.requested, e.guard_lo, e.r1_min, margin, e.calls);
    }
}

}  // namespace stacks
