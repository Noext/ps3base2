#include "object.h"

#include <atomic>
#include <cstdlib>

namespace lv2 {

namespace {

constexpr uint32_t kUserMemory = 0x0D500000;
std::mutex g_lock;
uint32_t g_used = 0;
std::map<uint32_t, uint32_t> g_allocs;

struct Container : Object {
    uint32_t size = 0;
    uint32_t used = 0;
};

}

/* sys_vm_memory_map(vsize, psize, cid, flags, policy, addr) -- syscall 300.
 *
 * The game's server pool calls this to reserve 64 MiB of virtual memory. It was
 * missing, so the call fell through to the unimplemented handler, returned 0,
 * and the caller stored that 0 into its pool pointer. The next write through
 * that pointer faulted at guest address 0.
 * Reference: refs/rpcs3/rpcs3/Emu/Cell/lv2/sys_vm.cpp.
 *
 * The virtual address is reserved and the physical pages are counted against
 * the user budget; the guest address returned is the base of the reservation.
 */
LV2_SYSCALL(300, sys_vm_memory_map) {
    const uint64_t vsize = u64arg(ctx, 0);
    const uint64_t psize = u64arg(ctx, 1);
    const uint32_t cid = u32arg(ctx, 2);
    const uint64_t flag = u64arg(ctx, 3);
    const uint64_t policy = u64arg(ctx, 4);
    const uint32_t out = u32arg(ctx, 5);
    (void)cid;
    LOG_INFO("vm", "sys_vm_memory_map vsize 0x%llx psize 0x%llx flags 0x%llx policy 0x%llx",
             static_cast<unsigned long long>(vsize), static_cast<unsigned long long>(psize),
             static_cast<unsigned long long>(flag), static_cast<unsigned long long>(policy));
    if (!vsize || !psize) return CELL_EINVAL;
    if (vsize % 0x2000000 || vsize > 0x10000000) return CELL_EINVAL;
    if (psize % 0x10000) return CELL_EINVAL;
    /* SYS_VM_POLICY_AUTO_RECOMMENDED is 1, not 0. Reading it as 1 was the
     * difference between the server pool being allocated and the caller storing
     * a null pointer.
     * refs/rpcs3/rpcs3/Emu/Cell/lv2/sys_vm.h:15. */
    if (policy != 1) return CELL_EINVAL;
    if (flag & 0xF4) return CELL_EINVAL;
    if (!out) return CELL_EFAULT;
    const uint64_t page = (flag & 0xF00) == 0x200 ? 0x10000 : 0x100000;
    std::lock_guard lk(g_lock);
    if (g_used + psize > kUserMemory) {
        LOG_WARN("vm", "vm_memory_map REFUSED: used 0x%x of 0x%x", g_used, kUserMemory);
        return CELL_ENOMEM;
    }
    const uint32_t addr = vm::alloc(page == 0x10000 ? vm::Region::User64K : vm::Region::User1M,
                                    static_cast<uint32_t>(vsize), static_cast<uint32_t>(page));
    if (!addr) return CELL_ENOMEM;
    g_used += static_cast<uint32_t>(psize);
    g_allocs[addr] = static_cast<uint32_t>(vsize);
    vm::write32(out, addr);
    LOG_INFO("vm", "sys_vm_memory_map -> 0x%08x (used 0x%x / 0x%x)", addr, g_used, kUserMemory);
    return CELL_OK;
}

/* The virtual-memory pool the server uses. sys_vm_memory_map reserves the
 * address range; these three attach and account for the physical pages. Without
 * 302 the server starts, finds its pool unmapped, and exits before the world can
 * load -- which is exactly what "Finalizing..." showed.
 * Reference: refs/rpcs3/rpcs3/Emu/Cell/lv2/sys_vm.cpp:179.
 */
LV2_SYSCALL(302, sys_vm_append_memory) {
    const uint32_t addr = u32arg(ctx, 0);
    const uint64_t size = u64arg(ctx, 1);
    LOG_INFO("vm", "sys_vm_append_memory addr 0x%x size 0x%llx", addr,
             static_cast<unsigned long long>(size));
    if (!size || size % 0x100000) return CELL_EINVAL;
    {
        std::lock_guard lk(g_lock);
        if (g_allocs.find(addr) == g_allocs.end()) return CELL_EINVAL;
    }
    /* Commit outside g_lock: vm::commit takes the runtime's memory lock itself,
     * and holding ours across the call deadlocks the first append. */
    if (!vm::commit(addr, static_cast<uint32_t>(size))) {
        LOG_WARN("vm", "append_memory: commit 0x%x+0x%llx failed", addr,
                 static_cast<unsigned long long>(size));
        return CELL_ENOMEM;
    }
    LOG_INFO("vm", "sys_vm_append_memory -> ok (committed 0x%llx)", static_cast<unsigned long long>(size));
    return CELL_OK;
}

LV2_SYSCALL(303, sys_vm_return_memory) {
    const uint32_t addr = u32arg(ctx, 0);
    const uint64_t size = u64arg(ctx, 1);
    LOG_INFO("vm", "sys_vm_return_memory addr 0x%x size 0x%llx", addr,
             static_cast<unsigned long long>(size));
    if (!size || size % 0x100000) return CELL_EINVAL;
    return CELL_OK;
}

LV2_SYSCALL(312, sys_vm_get_statistics) {
    static const bool on = std::getenv("PS3BASE_VM_TRACE") != nullptr;
    static std::atomic<int> n{0};
    if (on && n.fetch_add(1) < 40)
        LOG_INFO("vm", "sys_vm_get_statistics(addr=0x%x, stat=0x%x)", u32arg(ctx, 0), u32arg(ctx, 1));
    const uint32_t addr = u32arg(ctx, 0);
    const uint32_t stat = u32arg(ctx, 1);
    std::lock_guard lk(g_lock);
    auto it = g_allocs.find(addr);
    if (it == g_allocs.end()) return CELL_EINVAL;
    if (stat && vm::range_mapped(stat, 48)) {
        /* sys_vm_statistics_t is SIX u64 counters -- 48 bytes, not 64. Writing
         * eight clobbered the caller's saved r30/r31: the guest passes r1+112 as
         * the output buffer and keeps those registers at r1+160/r1+168, so the
         * two extra words overwrote them with zero. That is what made the pool
         * pointer null four milliseconds later.
         * refs/rpcs3/rpcs3/Emu/Cell/lv2/sys_vm.cpp:436 sets exactly six fields. */
        for (int i = 0; i < 6; i++) vm::write64(stat + i * 8, 0);
        vm::write64(stat + 32, it->second);   /* pmem_total */
        vm::write64(stat + 40, 0);            /* pmem_used  */
    }
    return CELL_OK;
}

LV2_SYSCALL(348, sys_memory_allocate) {
    uint32_t size = u32arg(ctx, 0);
    uint64_t flags = u64arg(ctx, 1);
    uint32_t out = u32arg(ctx, 2);
    uint32_t page = flags == 0x200 ? 0x10000 : 0x100000;
    if (!size || size % page) return CELL_EALIGN;
    if (!out) return CELL_EFAULT;
    std::lock_guard lk(g_lock);
    if (g_used + size > kUserMemory) {
        /* The server asks for a 64 MiB pool. Reporting the budget and the usage
         * at the moment of refusal is what tells a real exhaustion apart from a
         * wrongly sized budget. */
        LOG_WARN("memory", "sys_memory_allocate(0x%x) REFUSED: used 0x%x of 0x%x (%.0f MiB of %.0f MiB)",
                 size, g_used, kUserMemory, g_used / 1048576.0, kUserMemory / 1048576.0);
        return CELL_ENOMEM;
    }
    uint32_t addr = vm::alloc(page == 0x10000 ? vm::Region::User64K : vm::Region::User1M, size, page);
    if (!addr) {
        LOG_WARN("memory", "sys_memory_allocate(0x%x) -> vm::alloc failed (used 0x%x)", size, g_used);
        return CELL_ENOMEM;
    }
    g_used += size;
    g_allocs[addr] = size;
    vm::write32(out, addr);
    LOG_INFO("memory", "sys_memory_allocate(0x%x flags 0x%llx) -> 0x%08x (used now 0x%x / 0x%x, %.0f MiB)",
             size, static_cast<unsigned long long>(flags), addr, g_used, kUserMemory, g_used / 1048576.0);
    return CELL_OK;
}

LV2_SYSCALL(349, sys_memory_free) {
    uint32_t addr = u32arg(ctx, 0);
    std::lock_guard lk(g_lock);
    auto it = g_allocs.find(addr);
    if (it == g_allocs.end()) return CELL_EINVAL;
    g_used -= it->second;
    g_allocs.erase(it);
    vm::dealloc(addr);
    return CELL_OK;
}

LV2_SYSCALL(350, sys_memory_allocate_from_container) {
    uint32_t size = u32arg(ctx, 0);
    uint64_t flags = u64arg(ctx, 2);
    uint32_t out = u32arg(ctx, 3);
    uint32_t page = flags == 0x200 ? 0x10000 : 0x100000;
    if (!size || size % page) return CELL_EALIGN;
    uint32_t addr = vm::alloc(page == 0x10000 ? vm::Region::User64K : vm::Region::User1M, size, page);
    if (!addr) return CELL_ENOMEM;
    std::lock_guard lk(g_lock);
    g_allocs[addr] = 0;
    vm::write32(out, addr);
    return CELL_OK;
}

LV2_SYSCALL(351, sys_memory_get_page_attribute) {
    uint32_t addr = u32arg(ctx, 0);
    uint32_t out = u32arg(ctx, 1);
    if (!vm::is_mapped(addr)) return CELL_EINVAL;
    vm::write64(out, 0x40000ull);
    vm::write64(out + 8, 0xFull);
    vm::write32(out + 16, addr >= 0x30000000 && addr < 0x40000000 ? 0x100000 : 0x10000);
    vm::write32(out + 20, 0);
    return CELL_OK;
}

LV2_SYSCALL(352, sys_memory_get_user_memory_size) {
    uint32_t out = u32arg(ctx, 0);
    std::lock_guard lk(g_lock);
    vm::write32(out, kUserMemory);
    vm::write32(out + 4, kUserMemory - g_used);
    return CELL_OK;
}

LV2_SYSCALL(324, sys_memory_container_create) {
    uint32_t out = u32arg(ctx, 0);
    auto c = std::make_shared<Container>();
    c->size = u32arg(ctx, 1);
    std::lock_guard lk(g_lock);
    if (g_used + c->size > kUserMemory) return CELL_ENOMEM;
    g_used += c->size;
    vm::write32(out, add_object(0x3F000000, c));
    return CELL_OK;
}

LV2_SYSCALL(341, sys_memory_container_create2) {
    return sys_memory_container_create(ctx, base);
}

LV2_SYSCALL(325, sys_memory_container_destroy) {
    auto c = get<Container>(u32arg(ctx, 0));
    if (!c) return CELL_ESRCH;
    std::lock_guard lk(g_lock);
    g_used -= c->size;
    remove_object(u32arg(ctx, 0));
    return CELL_OK;
}

LV2_SYSCALL(342, sys_memory_container_destroy2) {
    return sys_memory_container_destroy(ctx, base);
}

LV2_SYSCALL(343, sys_memory_container_get_size) {
    uint32_t out = u32arg(ctx, 0);
    uint32_t cid = u32arg(ctx, 1);
    std::lock_guard lk(g_lock);
    if (cid == 0xFFFFFFFF || cid == 0) {
        vm::write32(out, kUserMemory);
        vm::write32(out + 4, kUserMemory - g_used);
        return CELL_OK;
    }
    auto c = get<Container>(cid);
    if (!c) return CELL_ESRCH;
    vm::write32(out, c->size);
    vm::write32(out + 4, c->size - c->used);
    return CELL_OK;
}

}
