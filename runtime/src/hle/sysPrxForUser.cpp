#include "common.h"

/* sysPrxForUser: the userspace half of the synchronisation primitives.
 *
 * These are NOT the _sys_lwmutex_* syscalls. They take a pointer to a
 * cell_lwmutex structure in guest memory. The layout is load-bearing and comes
 * from refs/jdc-ps3recomp/libs/system/sysPrxForUser.h:36 --
 *
 *     u64 lock_var;          // +0, bit 0 set while held
 *     u32 attribute;         // +8
 *     u32 recursive_count;   // +12
 *     u32 sleep_queue;       // +16, nonzero once registered
 *
 * The reference also documents the trap this title falls into: a mutex that was
 * statically initialised in .data (attribute set, sleep_queue still 0) has never
 * been through the create import. Locking one must serialise the first use
 * rather than fail, which is what "lwmutex_ensure_registered" does there. That
 * is the shape implemented here.
 */

#include <memory>
#include <mutex>
#include <unordered_map>

namespace hle {

namespace {

constexpr uint32_t LW_OFF_LOCK_VAR = 0;
constexpr uint32_t LW_OFF_RECURSIVE = 12;
constexpr uint32_t LW_OFF_SLEEP_QUEUE = 16;
constexpr uint32_t LW_SIZE = 24;

/* One host mutex per guest structure, keyed by its guest address. A guest
 * structure may be used before any create call, so registration is implicit. */
struct Entry {
    std::mutex m;
};
std::mutex g_table_lock;
std::unordered_map<uint32_t, std::shared_ptr<Entry>> g_table;

std::shared_ptr<Entry> entry_for(uint32_t ea) {
    std::lock_guard lk(g_table_lock);
    auto it = g_table.find(ea);
    if (it != g_table.end()) return it->second;
    auto e = std::make_shared<Entry>();
    g_table.emplace(ea, e);
    return e;
}

}  // namespace

HLE_FUNC(sysPrxForUser, sys_lwmutex_lock) {
    const uint32_t ea = a32(ctx, 0);
    if (!ea || !vm::range_mapped(ea, LW_SIZE)) return ret(ctx, static_cast<int32_t>(0x80010002));
    /* A statically initialised mutex has sleep_queue == 0; give it one so the
     * guest's own bookkeeping stays consistent. */
    if (!vm::read32(ea + LW_OFF_SLEEP_QUEUE)) vm::write32(ea + LW_OFF_SLEEP_QUEUE, 1);
    const auto e = entry_for(ea);
    if (e->m.try_lock()) {
        vm::write32(ea + LW_OFF_LOCK_VAR, vm::read32(ea + LW_OFF_LOCK_VAR) | 1u);
        ret(ctx, 0);
        return;
    }
    /* Contended: block. The guest allocates one host thread per guest thread, so
     * blocking here is the correct behaviour and not a host stall. */
    e->m.lock();
    vm::write32(ea + LW_OFF_LOCK_VAR, vm::read32(ea + LW_OFF_LOCK_VAR) | 1u);
    ret(ctx, 0);
}

HLE_FUNC(sysPrxForUser, sys_lwmutex_unlock) {
    const uint32_t ea = a32(ctx, 0);
    if (!ea || !vm::range_mapped(ea, LW_SIZE)) return ret(ctx, static_cast<int32_t>(0x80010002));
    vm::write32(ea + LW_OFF_LOCK_VAR, vm::read32(ea + LW_OFF_LOCK_VAR) & ~1u);
    std::shared_ptr<Entry> e;
    {
        std::lock_guard lk(g_table_lock);
        auto it = g_table.find(ea);
        if (it != g_table.end()) e = it->second;
    }
    if (e) e->m.unlock();
    ret(ctx, 0);
}

HLE_FUNC(sysPrxForUser, sys_lwmutex_destroy) {
    const uint32_t ea = a32(ctx, 0);
    if (ea && vm::range_mapped(ea, LW_SIZE)) {
        vm::write32(ea + LW_OFF_LOCK_VAR, 0);
        vm::write32(ea + LW_OFF_SLEEP_QUEUE, 0);
    }
    std::lock_guard lk(g_table_lock);
    g_table.erase(ea);
    ret(ctx, 0);
}

HLE_FUNC(sysPrxForUser, sys_lwmutex_create) {
    const uint32_t ea = a32(ctx, 0);
    const uint32_t attr = a32(ctx, 1);
    if (!ea || !vm::range_mapped(ea, LW_SIZE)) return ret(ctx, static_cast<int32_t>(0x80010002));
    const uint32_t attribute = (attr && vm::range_mapped(attr, 16)) ? vm::read32(attr + 4) : 0;
    vm::write32(ea + LW_OFF_LOCK_VAR, 0);
    vm::write32(ea + 8, attribute);
    vm::write32(ea + LW_OFF_RECURSIVE, 0);
    vm::write32(ea + LW_OFF_SLEEP_QUEUE, 1);
    entry_for(ea);
    ret(ctx, 0);
}

HLE_FUNC(sysPrxForUser, sys_lwmutex_trylock) {
    const uint32_t ea = a32(ctx, 0);
    if (!ea || !vm::range_mapped(ea, LW_SIZE)) return ret(ctx, static_cast<int32_t>(0x80010002));
    const auto e = entry_for(ea);
    if (!e->m.try_lock()) return ret(ctx, static_cast<int32_t>(0x8001000A)); /* EBUSY */
    vm::write32(ea + LW_OFF_LOCK_VAR, vm::read32(ea + LW_OFF_LOCK_VAR) | 1u);
    ret(ctx, 0);
}

}  // namespace hle
