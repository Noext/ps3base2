#include "object.h"
#include "runtime/pgwrite.h"

#include "runtime/image.h"
#include "runtime/log.h"

#include <cstdlib>
#include <vector>

/* sys_mmapper -- the LV2 shared-memory heap.
 *
 * The specification is refs/rpcs3/rpcs3/Emu/Cell/lv2/sys_mmapper.cpp. Every
 * handler below is written against it, and the places this file knowingly
 * differs are named in the comment above the code and in RESULTS.md's deviation
 * table, because a silent difference here shows up as a guest structure that is
 * never written -- which is exactly the shape of the RDR +0x060 fault.
 *
 * Unit tests: tests/mmapper_conformance.cpp, one check per deviation.
 */
namespace lv2 {

namespace {

/* refs/rpcs3/rpcs3/Emu/Cell/lv2/sys_memory.h:22-44. Read, not remembered. */
constexpr uint64_t kAccessRightMask    = 0xFF;
constexpr uint64_t kAccessRightSpuThr  = 0x02;
constexpr uint64_t kAccessRightRawSpu  = 0x01;
constexpr uint64_t kPageFaultSpuAllow  = 0x10000;
constexpr uint64_t kPageSize64K        = 0x200;
constexpr uint64_t kPageSize1M         = 0x400;
constexpr uint64_t kPageSizeMask       = 0xF00;
constexpr uint64_t kGranularityMask    = 0xF00;
/* refs/.../sys_mmapper.h:57. The key a caller passes when it wants an
 * unshared allocation. */
constexpr uint64_t kNoShmKey           = 0xFFFF000000000000ull;

struct Area {
    uint32_t addr;
    uint32_t size;
    uint32_t page;
    uint64_t flags;
};

struct SharedMem : Object {
    uint32_t size = 0;
    uint32_t align = 0x100000;
    uint64_t flags = 0;
    uint64_t key = 0;
    bool pshared = false;
    /* RPCS3 keeps one utils::shm per lv2_memory and counts the mappings that
     * refer to it (sys_mmapper.cpp:46-54, :798, :956). One host buffer per
     * object is what makes a second mapping see the first one's writes, and
     * `counter` is what makes free_shared_memory refuse while either exists. */
    std::vector<uint8_t> data;
    uint32_t counter = 0;
};

std::mutex g_lock;
std::map<uint32_t, Area> g_areas;
std::map<uint32_t, uint32_t> g_maps;      /* mapping address -> mem id */
std::map<uint64_t, uint32_t> g_keyed;     /* IPC key -> mem id (pshared only) */

const Area* area_of_locked(uint32_t addr) {
    for (auto& [a, ar] : g_areas) {
        if (addr >= a && addr < a + ar.size) return &ar;
    }
    return nullptr;
}

uint32_t page_for(uint64_t flags) { return (flags & kPageSizeMask) == kPageSize64K ? 0x10000u : 0x100000u; }

/* The lv2_object::create contract RPCS3 uses for shared memory
 * (sys_mmapper.cpp:188-206).
 *
 *   create_mode from flags: 0xc000 -> newly created, 0x8000 -> not care, else
 *   not create; and for the from_container_ext variants always newly created.
 *
 * A keyed allocation is process-shared and therefore has to be found again by
 * key: that is the whole point of ipc_key, and ignoring it -- which this file
 * did -- makes two guests that agree on a key get two different heaps. */
int64_t create_shm(uint64_t key, uint64_t size, uint64_t flags, bool pshared, bool exclusive, uint32_t* out_id) {
    if (!pshared) key = 0;
    const int mode = (exclusive || (flags & 0xC000) == 0xC000) ? 1   /* newly created */
                   : (flags & 0x8000)                        ? 3   /* not care       */
                                                             : 2;  /* not create     */
    if (pshared && key != 0) {
        auto it = g_keyed.find(key);
        if (it != g_keyed.end()) {
            if (mode == 1) return CELL_EEXIST;
            auto m = get<SharedMem>(it->second);
            if (m) {
                if (m->size != size) return CELL_EPERM;
                *out_id = it->second;
                return CELL_OK;
            }
            g_keyed.erase(it);
        } else if (mode == 2) {
            return CELL_ESRCH;
        }
    }
    auto m = std::make_shared<SharedMem>();
    m->size = static_cast<uint32_t>(size);
    m->align = page_for(flags);
    m->flags = flags;
    m->key = pshared ? key : 0;
    m->pshared = pshared;
    m->data.assign(size, 0);
    const uint32_t id = add_object(0x08000000, m);
    if (pshared && key != 0) g_keyed[key] = id;
    *out_id = id;
    return CELL_OK;
}

/* Every sys_mmapper call, its raw arguments and its return value.
 *
 * RPCS3 logs each of these (refs/rpcs3/rpcs3/Emu/Cell/lv2/sys_mmapper.cpp:212,
 * 650, 716, 754, 834, 915), so its behaviour is the specification this runtime
 * is audited against -- and the audit needs to know which calls the title
 * actually reaches. The five argument registers are logged raw rather than
 * interpreted: which register is which argument differs per call, and an
 * interpreted one hides a caller that passed something unexpected.
 *
 * On by default: RDR issues a bounded number of these during boot and a missing
 * sys_mmapper call is otherwise invisible. PS3BASE_MMAPPER_QUIET=1 silences it
 * -- a title that allocates in a loop would otherwise bury its own log. */
bool quiet() {
    static const bool q = std::getenv("PS3BASE_MMAPPER_QUIET") != nullptr;
    return q;
}

int64_t note(ppu::Context& ctx, const char* name, int64_t v) {
    if (!quiet()) {
        LOG_INFO("mmapper", "%s r3=0x%llx r4=0x%llx r5=0x%llx r6=0x%llx r7=0x%llx -> 0x%llx",
                 name, static_cast<unsigned long long>(ctx.gpr[3]),
                 static_cast<unsigned long long>(ctx.gpr[4]),
                 static_cast<unsigned long long>(ctx.gpr[5]),
                 static_cast<unsigned long long>(ctx.gpr[6]),
                 static_cast<unsigned long long>(ctx.gpr[7]),
                 static_cast<unsigned long long>(static_cast<uint64_t>(v)));
    }
    return v;
}

/* The logic, separated from the register file so the unit test can drive it
 * without building a ppu::Context. */
int32_t allocate_address(uint64_t size, uint64_t flags, uint64_t align, uint32_t out) {
    if (size % 0x10000000) return CELL_EALIGN;
    /* sys_mmapper.cpp:219-222. A size that does not fit the guest address space
     * must fail rather than be truncated into a smaller area. */
    if (size > 0xFFFFFFFFull) return CELL_ENOMEM;
    if (!align) align = 0x10000000;
    /* :231-243 -- only 1M, 64K and "unspecified" are legal page sizes. */
    switch (flags & kPageSizeMask) {
    case 0:
    case kPageSize1M:
    case kPageSize64K:
        break;
    default:
        return CELL_EINVAL;
    }
    /* :245-251 -- a page-fault-notification area is only legal for the SPU
     * access rights it is meaningful for. */
    if ((flags & kAccessRightMask) && (flags & kPageFaultSpuAllow)) {
        if ((flags & (kAccessRightRawSpu | kAccessRightSpuThr)) == 0) return CELL_EINVAL;
    }
    /* :253-273 -- the alignment is one of four values, not "any power of two". */
    switch (align) {
    case 0x10000000:
    case 0x20000000:
    case 0x40000000:
    case 0x80000000:
        break;
    default:
        return CELL_EALIGN;
    }
    std::lock_guard lk(g_lock);
    uint32_t a = vm::reserve_slot(static_cast<uint32_t>(size), static_cast<uint32_t>(align));
    if (!a) return CELL_ENOMEM;
    /* An mmapper area that contains a guest image corrupts that image.
     *
     * Measured on Red Dead Redemption: layout_for.py placed the firmware modules
     * at 0x20000000 and up, sys_mmapper_allocate_address handed the guest
     * 0x10000000 bytes starting at exactly 0x20000000, and the guest wrote over
     * liblv2. module_start then parked in sys_cond_wait for a wakeup nobody was
     * left to send: black screen, 0.2% draws, no crash, no unimplemented import.
     *
     * reserve_slot cannot see the image table (vm does not depend on it), so the
     * check lives here, where the runtime knows both. RPCS3 places modules at
     * 0x30000000 for the same reason (PPUModule.cpp:2242). */
    /* The image table is weak: the unit-test executables link this file without
     * the generated table.cpp, and a weak undefined COUNTER reads as a null
     * address rather than as 0 -- loading it faults. memory.cpp's
     * seed_image_slots_locked guards the same two symbols the same way. */
    const uint32_t n_images = (&image::image_count && &image::images) ? image::image_count : 0;
    for (uint32_t i = 0; i < n_images; i++) {
        const image::ImageEntry& im = image::images[i];
        const uint64_t area_lo = a, area_hi = uint64_t(a) + size;
        const uint64_t img_lo = im.text_lo, img_hi = uint64_t(im.text_hi) + 0x1000000;
        if (area_lo < img_hi && img_lo < area_hi) {
            LOG_ERROR("mmapper", "allocate_address 0x%llx -> 0x%08x..0x%08x OVERLAPS image "
                      "'%s' (0x%08x..0x%08x); refusing",
                      static_cast<unsigned long long>(size), a, uint32_t(area_hi),
                      im.name, im.text_lo, im.text_hi);
            vm::release_slot(a);
            return CELL_ENOMEM;
        }
    }
    g_areas[a] = {a, static_cast<uint32_t>(size), page_for(flags), flags};
    vm::write32(out, a);
    LOG_INFO("mmapper", "allocate_address 0x%llx flags 0x%llx align 0x%llx -> 0x%08x",
             static_cast<unsigned long long>(size), static_cast<unsigned long long>(flags),
             static_cast<unsigned long long>(align), a);
    return CELL_OK;
}

int32_t free_address(uint32_t addr) {
    /* :652-655 -- an address outside the mmapper band is not an area at all, and
     * reporting EINVAL for it is what tells a caller it passed nonsense rather
     * than that the area leaked. */
    if (addr < 0x20000000 || addr >= 0xC0000000) return CELL_EINVAL;
    std::lock_guard lk(g_lock);
    auto it = g_areas.find(addr);
    if (it == g_areas.end()) return CELL_EINVAL;
    /* :668-674 -- RPCS3 refuses to free an area a page-fault notification still
     * watches. This runtime has no page-fault notification yet (see the note on
     * syscall 327), so there is nothing to check and the branch is unreachable
     * rather than omitted. */
    vm::release_slot(addr);
    g_areas.erase(it);
    return CELL_OK;
}

int32_t allocate_shared_memory(uint64_t key, uint64_t size, uint64_t flags, uint32_t out) {
    if (!size) return CELL_EALIGN;
    /* :310-335 -- the granularity decides the alignment, and an unknown
     * granularity is EINVAL, not "assume 1M". Treating 0x100 (4K) as 1M let a
     * caller allocate an object whose own page size the guest never agreed to. */
    switch (flags & kGranularityMask) {
    case 0:
    case 0x400:
        if (size % 0x100000) return CELL_EALIGN;
        break;
    case 0x200:
        if (size % 0x10000) return CELL_EALIGN;
        break;
    default:
        return CELL_EINVAL;
    }
    const bool pshared = key != kNoShmKey;
    std::lock_guard lk(g_lock);
    uint32_t id = 0;
    const int64_t r = create_shm(key, size, flags, pshared, false, &id);
    if (r != CELL_OK) return static_cast<int32_t>(r);
    vm::write32(out, id);
    return CELL_OK;
}

int32_t allocate_shared_memory_from_container(uint64_t key, uint64_t size, uint32_t cid, uint64_t flags, uint32_t out) {
    if (!size) return CELL_EALIGN;
    switch (flags & kGranularityMask) {
    case 0:
    case 0x400:
        if (size % 0x100000) return CELL_EALIGN;
        break;
    case 0x200:
        if (size % 0x10000) return CELL_EALIGN;
        break;
    default:
        return CELL_EINVAL;
    }
    /* :397-400 -- this variant always creates, so a create-mode flag is
     * contradictory and rejected. */
    if (flags & 0xC000) return CELL_EINVAL;
    /* :402-407 -- the container has to exist. Ignoring cid, which this file did,
     * hands back memory that is not charged to the container the caller named. */
    if (!container_exists(cid)) return CELL_ESRCH;
    std::lock_guard lk(g_lock);
    uint32_t id = 0;
    const int64_t r = create_shm(key, size, flags, key != kNoShmKey, true, &id);
    if (r != CELL_OK) return static_cast<int32_t>(r);
    vm::write32(out, id);
    return CELL_OK;
}

int32_t free_shared_memory(uint32_t id) {
    std::lock_guard lk(g_lock);
    auto m = get<SharedMem>(id);
    if (!m) return CELL_ESRCH;
    /* :726-729 -- counter is the number of live mappings; EBUSY while any exists.
     * This used to test a single mapped_at address, so the SECOND mapping of the
     * same object was rejected with EBUSY instead of being allowed. */
    if (m->counter) return CELL_EBUSY;
    if (m->pshared && m->key) g_keyed.erase(m->key);
    remove_object(id);
    return CELL_OK;
}

int32_t map_shared_memory(uint32_t addr, uint32_t id, uint64_t flags) {
    (void)flags;
    std::lock_guard lk(g_lock);
    const Area* ar = area_of_locked(addr);
    if (!ar) return CELL_EINVAL;
    auto m = get<SharedMem>(id);
    if (!m) return CELL_ESRCH;
    /* :770-776 -- the check is on the MEMORY's align against the AREA's page
     * size (EINVAL), then on the address's alignment (EALIGN). Two different
     * answers for two different mistakes; this returned EALIGN for both. */
    if (m->align < ar->page) return CELL_EINVAL;
    if (addr % ar->page) return CELL_EALIGN;
    if (uint64_t(addr) + m->size > uint64_t(ar->addr) + ar->size) return CELL_EINVAL;
    auto ins = g_maps.find(addr);
    if (ins != g_maps.end()) return CELL_EBUSY;
    if (!vm::map(addr, m->size)) return CELL_ENOMEM;
    /* The object's own buffer is the backing store, so a write through one
     * mapping is visible through every other one at the next map/unmap. */
    pgw::write(addr, m->data.data(), m->size);
    g_maps[addr] = id;
    m->counter++;
    return CELL_OK;
}

int32_t search_and_map(uint32_t start, uint32_t id, uint32_t out) {
    std::lock_guard lk(g_lock);
    /* :838 -- start_addr must BE the area's base, not merely inside it. */
    const Area* ar = area_of_locked(start);
    if (!ar || start != ar->addr) return CELL_EINVAL;
    auto m = get<SharedMem>(id);
    if (!m) return CELL_ESRCH;
    /* :850-855 -- EALIGN here, unlike map_shared_memory's EINVAL. That
     * asymmetry is RPCS3's and is preserved. */
    if (m->align < ar->page) return CELL_EALIGN;
    for (uint64_t a = ar->addr; a + m->size <= uint64_t(ar->addr) + ar->size; a += ar->page) {
        bool used = false;
        for (auto& [ma, mid] : g_maps) {
            auto mm = get<SharedMem>(mid);
            if (mm && a < uint64_t(ma) + mm->size && uint64_t(ma) < a + m->size) { used = true; break; }
        }
        if (used) continue;
        if (!vm::map(static_cast<uint32_t>(a), m->size)) return CELL_ENOMEM;
        pgw::write(static_cast<uint32_t>(a), m->data.data(), m->size);
        g_maps[static_cast<uint32_t>(a)] = id;
        m->counter++;
        vm::write32(out, static_cast<uint32_t>(a));
        return CELL_OK;
    }
    return CELL_ENOMEM;
}

int32_t unmap_shared_memory(uint32_t addr, uint32_t out) {
    std::lock_guard lk(g_lock);
    /* :924-929 -- the address has to be a live mapping. */
    auto it = g_maps.find(addr);
    if (it == g_maps.end()) return CELL_EINVAL;
    auto m = get<SharedMem>(it->second);
    if (!m) return CELL_EINVAL;
    std::memcpy(m->data.data(), vm::ptr(addr), m->size);
    vm::dealloc(addr);
    /* :952-956 -- RPCS3 always writes *mem_id and then decrements the counter. */
    vm::write32(out, it->second);
    m->counter--;
    g_maps.erase(it);
    return CELL_OK;
}

int32_t change_address_access_right(uint32_t addr, uint64_t flags) {
    /* :637-644 -- RPCS3 logs this as todo and returns CELL_OK. It validated
     * nothing, so returning ENOSYS (which is what the unimplemented path did)
     * refuses work the console performs, which AGENTS.md forbids: success is
     * the answer here, and the caller's access rights are already what it asked
     * for because this runtime does not restrict them. */
    LOG_INFO("mmapper", "change_address_access_right(0x%08x, 0x%llx) -> CELL_OK (unrestricted host memory)",
             addr, static_cast<unsigned long long>(flags));
    return CELL_OK;
}

int32_t allocate_fixed_address() {
    /* :276-288 -- 0xB0000000 .. 0xC0000000 with 1M pages, once. EEXIST if it is
     * already mapped, which is what RPCS3's vm::map returning false means. */
    if (vm::is_mapped(0xB0000000)) return CELL_EEXIST;
    if (!vm::map(0xB0000000, 0x10000000)) return CELL_EEXIST;
    return CELL_OK;
}

#define MMAPPER_SYSCALL(num, name) \
    static int64_t name##_impl(ppu::Context& ctx); \
    static int64_t name##_logged(ppu::Context& ctx, uint8_t*) { \
        return note(ctx, #name, name##_impl(ctx)); \
    } \
    static lv2::SyscallReg reg_##name(num, #name, name##_logged); \
    static int64_t name##_impl(ppu::Context& ctx)

}

MMAPPER_SYSCALL(326, sys_mmapper_allocate_fixed_address) {
    return allocate_fixed_address();
}

MMAPPER_SYSCALL(330, sys_mmapper_allocate_address) {
    return allocate_address(u64arg(ctx, 0), u64arg(ctx, 1), u64arg(ctx, 2), u32arg(ctx, 3));
}

MMAPPER_SYSCALL(331, sys_mmapper_free_address) {
    return free_address(u32arg(ctx, 0));
}

MMAPPER_SYSCALL(332, sys_mmapper_allocate_shared_memory) {
    return allocate_shared_memory(u64arg(ctx, 0), u64arg(ctx, 1), u64arg(ctx, 2), u32arg(ctx, 3));
}

MMAPPER_SYSCALL(362, sys_mmapper_allocate_shared_memory_from_container) {
    return allocate_shared_memory_from_container(u64arg(ctx, 0), u64arg(ctx, 1), u32arg(ctx, 2),
                                                 u64arg(ctx, 3), u32arg(ctx, 4));
}

MMAPPER_SYSCALL(329, sys_mmapper_free_shared_memory) {
    return free_shared_memory(u32arg(ctx, 0));
}

MMAPPER_SYSCALL(334, sys_mmapper_map_shared_memory) {
    return map_shared_memory(u32arg(ctx, 0), u32arg(ctx, 1), u64arg(ctx, 2));
}

MMAPPER_SYSCALL(337, sys_mmapper_search_and_map) {
    return search_and_map(u32arg(ctx, 0), u32arg(ctx, 1), u32arg(ctx, 3));
}

MMAPPER_SYSCALL(335, sys_mmapper_unmap_shared_memory) {
    return unmap_shared_memory(u32arg(ctx, 0), u32arg(ctx, 1));
}

MMAPPER_SYSCALL(336, sys_mmapper_change_address_access_right) {
    return change_address_access_right(u32arg(ctx, 0), u64arg(ctx, 1));
}

}
