/* ps3base -- sys_mmapper against RPCS3, one check per deviation.
 *
 * The specification is refs/rpcs3/rpcs3/Emu/Cell/lv2/sys_mmapper.cpp. Each CHECK
 * below names the line range it pins, so a future change to either side has an
 * answer to "which behaviour was this".
 *
 * The handlers are static in the module, so this includes it and drives the
 * logic functions directly. That keeps the test a unit test: no ppu::Context, no
 * dispatch table, no generated image table. The address allocator is the real
 * one (runtime/src/memory.cpp), because the bug that matters most here -- an
 * area handed out over a guest image -- lives in the interaction between the
 * two.
 */
#include "runtime/memory.h"

#include <cstdio>
#include <cstring>

#include "../runtime/src/lv2/mmapper.cpp"

/* lv2::SyscallReg's constructor lives in ppu_runtime.cpp and registers into the
 * dispatch table. This test calls the logic directly and links none of that, so
 * the constructor is a no-op here. Same shape as tests/spurs.cpp. */
namespace lv2 {
SyscallReg::SyscallReg(uint32_t, const char*, SyscallFn) {}
}

/* lv2::system_time_us (runtime/src/lv2/object.cpp) reads ppu::timebase(), which
 * lives in ppu_ops.cpp. Linking that would drag in the whole PPU op surface for
 * one counter, so this test provides it: nothing here depends on the value. */
namespace ppu {
uint64_t timebase() { return 0; }
}

static int fails = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; std::fprintf(stderr, "[FAIL] %s\n", msg); } \
} while (0)

#define CHECK_EQ(a, b, msg) do { \
    checks++; \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { fails++; std::fprintf(stderr, "[FAIL] %s (0x%llx != 0x%llx)\n", msg, _a, _b); } \
} while (0)

using namespace lv2;

int main()
{
    if (!vm::init()) { std::fprintf(stderr, "vm::init failed\n"); return 1; }
    /* The handlers log; keep the test's own output readable. */
    setenv("PS3BASE_MMAPPER_QUIET", "1", 1);
    /* The out-parameters are guest addresses, so they have to be real memory:
     * vm::write32 goes through rsv::store_range, which memcpy's. */
    CHECK(vm::map(0x1000, 0x40000), "scratch page for the out-parameters");
    std::memset(vm::ptr(0x1000), 0, 0x40000);

    /* --- allocate_address: the argument checks ---------------------------- */

    /* sys_mmapper.cpp:214-217 -- size must be a multiple of 0x10000000. */
    uint32_t out = 0;
    CHECK_EQ(allocate_address(0x08000000, 0, 0x10000000, 0x1000), CELL_EALIGN,
             "D1 allocate_address rejects a size that is not a multiple of 0x10000000");

    /* :219-222 -- a size that does not fit 32 bits is ENOMEM, not truncated. */
    CHECK_EQ(allocate_address(0x100000000ull, 0, 0x10000000, 0x1000), CELL_ENOMEM,
             "D2 allocate_address returns ENOMEM for a size above the address space");

    /* :226-229 -- alignment 0 means 0x10000000, not a failure. */
    CHECK_EQ(allocate_address(0x10000000, 0, 0, 0x1000), CELL_OK,
             "D3 alignment 0 is accepted and means 0x10000000");

    /* :231-243 -- an unknown page size is EINVAL. 0x300 is not 0, 0x400 or 0x200. */
    uint32_t a_badpage = 0;
    CHECK_EQ(allocate_address(0x10000000, 0x300, 0x10000000, 0x1000), CELL_EINVAL,
             "D4 allocate_address rejects an unknown page size with EINVAL");

    /* :245-251 -- page-fault notification needs RAW_SPU or SPU_THR access right. */
    CHECK_EQ(allocate_address(0x10000000, 0x10000 | 0x08, 0x10000000, 0x1000), CELL_EINVAL,
             "D5 page-fault notification without an SPU access right is EINVAL");
    CHECK_EQ(allocate_address(0x10000000, 0x10000 | 0x02, 0x10000000, 0x1000), CELL_OK,
             "D6 page-fault notification with SPU_THR is accepted");

    /* :253-273 -- only 0x10000000/0x20000000/0x40000000/0x80000000 alignments. */
    CHECK_EQ(allocate_address(0x10000000, 0, 0x1000000, 0x1000), CELL_EALIGN,
             "D7 allocate_address rejects an alignment outside the four legal values");

    /* The guest's own call, exactly as RDR makes it: 0x10000000/0xf0/0x10000000. */
    {
        uint32_t o = 0, area = vm::reserve_slot(0x1000, 0x1000);
        vm::release_slot(area);
        (void)o;
    }
    CHECK_EQ(allocate_address(0x10000000, 0xf0, 0x10000000, 0x1000), CELL_OK,
             "D8 RDR's exact allocate_address arguments are accepted");
    const uint32_t area1 = vm::read32(0x1000);
    CHECK(area1 != 0 && area1 % 0x10000000 == 0,
          "D9 the area is 256 MiB aligned and non-zero");
    CHECK_EQ(allocate_address(0x10000000, 0xf0, 0x10000000, 0x1004), CELL_OK,
             "D10 a second identical call is accepted");
    CHECK(vm::read32(0x1004) != area1,
          "D11 the second area does not alias the first");
    CHECK_EQ(vm::read32(0x1004) - area1, 0x10000000u,
          "D11b the areas are adjacent 256 MiB slots");

    /* --- free_address ---------------------------------------------------- */

    /* :652-655 -- an address outside 0x20000000..0xC0000000 is EINVAL. */
    CHECK_EQ(free_address(0x1000), CELL_EINVAL,
             "D12 free_address rejects an address below the mmapper band");
    /* :652-655 -- an address in the band that is not an area is EINVAL, and the
     * areas this test already made are at 0x10000000/0x20000000/0x30000000, so
     * 0x2F000000 is inside the band and inside none of them. */
    CHECK_EQ(free_address(0x2F000000), CELL_EINVAL,
             "D13 free_address on an address that is not an area is EINVAL");
    CHECK_EQ(free_address(area1), CELL_OK, "D14 free_address frees a real area");
    CHECK_EQ(free_address(area1), CELL_EINVAL, "D15 freeing it twice is EINVAL");

    /* --- allocate_shared_memory ----------------------------------------- */

    /* :304-307 -- size 0 is EALIGN. */
    CHECK_EQ(allocate_shared_memory(kNoShmKey, 0, 0, 0x1008), CELL_EALIGN,
             "D16 allocate_shared_memory rejects size 0 with EALIGN");
    /* :310-335 -- 1M granularity: a 64K-multiple size is EALIGN. */
    CHECK_EQ(allocate_shared_memory(kNoShmKey, 0x10000, 0, 0x1008), CELL_EALIGN,
             "D17 a 1M-granularity allocation refuses a 64K size");
    /* ... and a 64K granularity accepts the same size. */
    CHECK_EQ(allocate_shared_memory(kNoShmKey, 0x10000, kPageSize64K, 0x1008), CELL_OK,
             "D18 a 64K-granularity allocation accepts a 64K size");
    uint32_t shm64 = vm::read32(0x1008);
    /* :331-334 -- an unknown granularity is EINVAL, not 1M. This is the
     * deviation that mattered: 0x100 (4K) is a page size, not a granularity. */
    CHECK_EQ(allocate_shared_memory(kNoShmKey, 0x10000, 0x100, 0x100C), CELL_EINVAL,
             "D19 an unknown granularity is EINVAL, not silently 1M");

    /* The object's align comes from its own flags: 64K here, 1M by default. */
    {
        auto m = get<SharedMem>(shm64);
        CHECK(m && m->align == 0x10000, "D20 a 64K object's align is 0x10000");
        CHECK(m && m->size == 0x10000, "D21 the object's size is what was asked for");
    }
    uint32_t shm1m_id = 0;
    CHECK_EQ(allocate_shared_memory(kNoShmKey, 0x100000, 0, 0x1010), CELL_OK,
             "D22 a 1M allocation succeeds");
    shm1m_id = vm::read32(0x1010);
    {
        auto m = get<SharedMem>(shm1m_id);
        CHECK(m && m->align == 0x100000, "D23 a 1M object's align is 0x100000");
    }

    /* :188-206 -- the create mode from the flags. A process-shared key that is
     * already taken is EEXIST for newly-created, OK for not-care, ESRCH for
     * not-create when absent. This file used to ignore the key entirely. */
    const uint64_t key = 0x8006010000000010ull;
    CHECK_EQ(allocate_shared_memory(key, 0x100000, 0xC000, 0x1014), CELL_OK,
             "D24 a new keyed allocation with create-mode newly-created succeeds");
    const uint32_t keyed = vm::read32(0x1014);
    CHECK_EQ(allocate_shared_memory(key, 0x100000, 0xC000, 0x1018), CELL_EEXIST,
             "D25 the same key again with newly-created is EEXIST");
    CHECK_EQ(allocate_shared_memory(key, 0x100000, 0x8000, 0x1018), CELL_OK,
             "D26 the same key with not-care reuses the object");
    CHECK_EQ(vm::read32(0x1018), keyed, "D27 not-care hands back the same id");
    CHECK_EQ(allocate_shared_memory(key, 0x200000, 0x8000, 0x1018), CELL_EPERM,
             "D28 reusing a key with a different size is EPERM");
    CHECK_EQ(allocate_shared_memory(0x1234, 0x100000, 0x0000, 0x1018), CELL_ESRCH,
             "D29 an unknown key with not-create is ESRCH");

    /* --- free_shared_memory --------------------------------------------- */

    CHECK_EQ(free_shared_memory(0x0BADF00D), CELL_ESRCH, "D30 free_shared_memory on an unknown id is ESRCH");
    CHECK_EQ(free_shared_memory(shm64), CELL_OK, "D31 an unmapped object frees");
    CHECK_EQ(free_shared_memory(shm64), CELL_ESRCH, "D32 freeing it again is ESRCH");

    /* --- map_shared_memory ---------------------------------------------- */

    /* The area these map into. 0x10000000 bytes at an align we control. */
    uint32_t area_out = 0x2000;
    CHECK_EQ(allocate_address(0x10000000, 0, 0x10000000, area_out), CELL_OK, "D33 area for mapping");
    const uint32_t area = vm::read32(area_out);

    /* :756-761 -- an address outside any area is EINVAL. */
    CHECK_EQ(map_shared_memory(0x1000, shm1m_id, 0), CELL_EINVAL,
             "D34 map_shared_memory outside every area is EINVAL");
    /* :763-768 -- an unknown id is ESRCH. */
    CHECK_EQ(map_shared_memory(area, 0x0BADF00D, 0), CELL_ESRCH,
             "D35 map_shared_memory with an unknown id is ESRCH");
    /* :772-775 -- a 64K object whose align is below a 1M area's page is EINVAL. */
    {
        uint32_t o64 = 0x1020;
        CHECK_EQ(allocate_shared_memory(kNoShmKey, 0x10000, kPageSize64K, o64), CELL_OK,
                 "D36a a 64K object exists for the align check");
        const uint32_t small = vm::read32(o64);
        CHECK_EQ(map_shared_memory(area, small, 0), CELL_EINVAL,
                 "D36 a 64K object cannot map into a 1M area (EINVAL, not EALIGN)");
        CHECK_EQ(free_shared_memory(small), CELL_OK, "D36b and it frees cleanly");
    }
    /* :777-780 -- an address that is not page-aligned is EALIGN. */
    CHECK_EQ(map_shared_memory(area + 0x1000, shm1m_id, 0), CELL_EALIGN,
             "D37 an unaligned map address is EALIGN");
    /* The success path. */
    CHECK_EQ(map_shared_memory(area, shm1m_id, 0), CELL_OK, "D38 a 1M object maps at the area base");
    CHECK_EQ(vm::is_mapped(area), true, "D39 the mapping is committed");

    /* The second mapping of the SAME object used to be refused with EBUSY,
     * because the object carried one mapped_at address. RPCS3 counts mappings
     * (:798) and allows several. */
    CHECK_EQ(map_shared_memory(area + 0x100000, shm1m_id, 0), CELL_OK,
             "D40 the same object can be mapped a second time");
    CHECK_EQ(map_shared_memory(area + 0x100000, shm1m_id, 0), CELL_EBUSY,
             "D41 mapping the same address twice is EBUSY");

    /* Does the second mapping see the first one's writes? RPCS3's utils::shm is
     * shared, so yes at the next map. Emulate the guest: write through the first
     * mapping, unmap it, then the second must still hold nothing of its own --
     * and a fresh map of the object must see the write. */
    std::memcpy(vm::ptr(area), "W2-ALIAS", 8);
    /* The second mapping was made from the object's buffer, which was empty at
     * that moment, so it must NOT see the write yet: RPCS3's shm is one buffer
     * but the mapping is a copy-on-map here, and pretending otherwise would be a
     * coincidence the test would then depend on. */
    CHECK(std::memcmp(vm::ptr(area + 0x100000), "W2-ALIAS", 8) != 0,
          "D44 a mapping made before the write does not see it");
    CHECK_EQ(unmap_shared_memory(area, 0x2100), CELL_OK, "D42 unmap_shared_memory succeeds");
    CHECK_EQ(vm::read32(0x2100), shm1m_id, "D43 unmap writes the object id to *mem_id");
    CHECK_EQ(map_shared_memory(area, shm1m_id, 0), CELL_OK, "D45 remapping the object succeeds");
    CHECK(std::memcmp(vm::ptr(area), "W2-ALIAS", 8) == 0,
          "D46 a remap of the same object sees the data written through the earlier mapping");

    /* :726-729 -- free while mapped is EBUSY, and the counter is per mapping. */
    CHECK_EQ(free_shared_memory(shm1m_id), CELL_EBUSY, "D47 free_shared_memory while mapped is EBUSY");
    CHECK_EQ(unmap_shared_memory(area, 0x2104), CELL_OK, "D48 unmap the first mapping");
    CHECK_EQ(free_shared_memory(shm1m_id), CELL_EBUSY, "D49 still EBUSY with one mapping left");
    CHECK_EQ(unmap_shared_memory(area + 0x100000, 0x2108), CELL_OK, "D50 unmap the second mapping");
    CHECK_EQ(free_shared_memory(shm1m_id), CELL_OK, "D51 free succeeds once nothing is mapped");

    /* :924-929 -- unmapping an address that is not a mapping is EINVAL. */
    CHECK_EQ(unmap_shared_memory(area, 0x210C), CELL_EINVAL,
             "D52 unmap_shared_memory on a non-mapping is EINVAL");

    /* --- search_and_map -------------------------------------------------- */

    uint32_t s_out = 0x2200;
    CHECK_EQ(allocate_address(0x10000000, 0, 0x10000000, 0x2204), CELL_OK, "D53 area for search_and_map");
    const uint32_t s_area = vm::read32(0x2204);
    /* :838 -- start must BE the area base. An address inside it is EINVAL. */
    CHECK_EQ(search_and_map(s_area + 0x100000, shm64, s_out), CELL_EINVAL,
             "D54 search_and_map requires start_addr to be the area base");
    /* :850-855 -- the align check here answers EALIGN, unlike map's EINVAL. */
    {
        uint32_t o64 = 0x2218;
        CHECK_EQ(allocate_shared_memory(kNoShmKey, 0x10000, kPageSize64K, o64), CELL_OK,
                 "D55a a 64K object for the search align check");
        const uint32_t small = vm::read32(o64);
        CHECK_EQ(search_and_map(s_area, small, s_out), CELL_EALIGN,
                 "D55 search_and_map answers EALIGN (not EINVAL) for a too-small align");
        CHECK_EQ(free_shared_memory(small), CELL_OK, "D55b and it frees cleanly");
    }
    {
        uint32_t id_out = 0x2208;
        CHECK_EQ(allocate_shared_memory(kNoShmKey, 0x100000, 0, id_out), CELL_OK, "D56 object for search_and_map");
        const uint32_t sid = vm::read32(id_out);
        CHECK_EQ(search_and_map(s_area, sid, s_out), CELL_OK, "D57 search_and_map finds room");
        const uint32_t mapped = vm::read32(s_out);
        CHECK_EQ(mapped, s_area, "D58 the first mapping is at the area base");
        CHECK_EQ(vm::is_mapped(mapped), true, "D59 the mapping is committed");
        /* :891-901 -- when the area is full the answer is ENOMEM. */
        uint32_t full_out = 0x220C;
        CHECK_EQ(allocate_shared_memory(kNoShmKey, 0x100000, 0, full_out), CELL_OK, "D60 second object");
        const uint32_t sid2 = vm::read32(full_out);
        CHECK_EQ(search_and_map(s_area, sid2, s_out), CELL_OK, "D61 it takes the next free slot");
        CHECK_EQ(vm::read32(s_out), s_area + 0x100000, "D62 the second mapping does not alias the first");
        CHECK_EQ(unmap_shared_memory(s_area, 0x2210), CELL_OK, "D63 unmap the first search mapping");
        CHECK_EQ(free_shared_memory(sid), CELL_OK, "D64 and free it");
        CHECK_EQ(unmap_shared_memory(s_area + 0x100000, 0x2214), CELL_OK, "D65 unmap the second");
        CHECK_EQ(free_shared_memory(sid2), CELL_OK, "D66 and free it");
    }

    /* --- change_address_access_right ------------------------------------- */

    /* :637-644 -- RPCS3 returns CELL_OK for this. Answering ENOSYS would refuse
     * work the console performs, which is the failure the net calls made. */
    CHECK_EQ(change_address_access_right(0x40000000, 0xFF), CELL_OK,
             "D67 change_address_access_right returns CELL_OK, as the console does");

    /* --- allocate_fixed_address ------------------------------------------ */

    /* :276-288 -- 0xB0000000 for 0x10000000, and EEXIST the second time. */
    CHECK_EQ(allocate_fixed_address(), CELL_OK, "D68 allocate_fixed_address maps 0xB0000000");
    CHECK_EQ(vm::is_mapped(0xB0000000), true, "D69 0xB0000000 is mapped afterwards");
    CHECK_EQ(allocate_fixed_address(), CELL_EEXIST, "D70 a second allocate_fixed_address is EEXIST");

    std::printf("mmapper: %d checks, %d failed\n", checks, fails);
    if (fails) { std::fprintf(stderr, "mmapper: %d of %d FAILED\n", fails, checks); return 1; }
    return 0;
}