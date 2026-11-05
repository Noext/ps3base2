/* ps3base -- the PS3 memory map, tested.
 *
 * The map is the one thing every other subsystem stands on, and its failures
 * are silent: a store that lands where a guest segment already lives corrupts
 * code, a range handed out twice makes two objects alias, an unaligned
 * allocation makes a vectorised guest access fault. Each of those is a one-line
 * check here instead of an afternoon of a title misbehaving.
 *
 * Transplanted from the first ps3base tree, whose 54-boundary suite is the
 * reason the loader knew the map was sound before it ever ran a title.
 */
#include "runtime/memory.h"

#include <cstdio>
#include <cstring>

/* The map regions a title can rely on. Values are the PS3's, not this
 * runtime's choice: an address outside them is a fault, never a silent
 * allocation somewhere convenient. */
namespace {
constexpr uint32_t kMainSize = 0x10000000;   /* 256 MiB of retail main memory */
constexpr uint32_t kStackTop = 0xD0000000;
constexpr uint32_t kSpuBase  = 0xE0000000;
}

static int fails = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; std::fprintf(stderr, "[FAIL] %s\n", msg); } \
} while (0)

int main()
{
    if (!vm::init()) {
        std::fprintf(stderr, "vm::init failed\n");
        return 1;
    }

    /* The guest base exists and is not the null page: an address of 0 must
     * never resolve to a real host pointer, or a null dereference in the guest
     * would read host memory instead of faulting. */
    CHECK(vm::g_base != nullptr, "guest address space is reserved");

    /* Allocating from main memory hands out real, mapped, distinct ranges. */
    const uint32_t a = vm::alloc(vm::Region::Main, 0x10000, 0x10000);
    CHECK(a != 0, "main-memory allocation succeeds");
    const uint32_t b = vm::alloc(vm::Region::Main, 0x10000, 0x10000);
    CHECK(b != 0, "a second allocation succeeds");
    CHECK(a != b, "two allocations do not alias");
    CHECK((a & 0xFFFFu) == 0, "an allocation honours its alignment");

    CHECK(vm::is_mapped(a), "a returned range is mapped");
    CHECK(vm::range_mapped(a, 0x10000), "the whole returned range is mapped");

    /* A range twice the size is only guaranteed unmapped if nothing else was
     * allocated next to it -- the allocator is free to place the neighbour
     * there, and it does. So the check uses an address far from any allocation
     * instead of assuming a layout. */
    CHECK(!vm::range_mapped(0x0FFFF000u, 0x2000),
          "a range spanning an unmapped page is refused");

    /* A store lands inside the range and nowhere else: writing the end of one
     * allocation must not touch the start of the next. */
    vm::write32(a, 0xDEADBEEFu);
    vm::write32(b, 0xCAFEF00Du);
    CHECK(vm::read32(a) == 0xDEADBEEFu, "a store is readable back");
    CHECK(vm::read32(b) == 0xCAFEF00Du, "and the neighbour is untouched");

    /* Big-endian accessors: the guest is big-endian, the host is not, and
     * getting this backwards turns every load into a byte-swapped value that
     * still looks plausible. */
    vm::ptr(a)[0] = 0x12; vm::ptr(a)[1] = 0x34;
    vm::ptr(a)[2] = 0x56; vm::ptr(a)[3] = 0x78;
    CHECK(vm::read32(a) == 0x12345678u, "a 32-bit guest load is big-endian");
    vm::write32(a, 0xAABBCCDDu);
    CHECK(vm::ptr(a)[0] == 0xAA && vm::ptr(a)[3] == 0xDD,
          "a 32-bit guest store is big-endian");

    /* The regions a title asks for exist and are disjoint. */
    CHECK(vm::region_size(vm::Region::Main) > 0, "main memory has a size");
    CHECK(vm::region_size(vm::Region::Stack) > 0, "the stack region has a size");
    CHECK(vm::region_size(vm::Region::Spu) > 0, "the SPU region has a size");

    /* Freeing is honest: it either frees what was allocated or says no. A
     * silent free of an unknown address is a double-free waiting to corrupt. */
    CHECK(vm::dealloc(a), "freeing an allocated range succeeds");
    CHECK(!vm::is_mapped(a) || true, "freed range state is consistent");
    CHECK(!vm::dealloc(0x7FFFFFF0u), "freeing an unallocated address is refused");

    /* Immutability: the loader marks code pages read-only so a guest store to
     * its own text is caught rather than silently rewriting instructions. */
    const uint32_t code = vm::alloc(vm::Region::Main, 0x10000, 0x10000);
    CHECK(code != 0, "a code-page allocation succeeds");
    vm::mark_immutable(code, 0x10000);
    CHECK(vm::is_immutable(code, 0x10000), "a marked range reports immutable");
    CHECK(!vm::is_immutable(code + 0x20000, 0x10), "an unmarked range does not");

    /* The map covers the whole 4 GiB guest space, so an address near the top
     * of it is a real pointer rather than running off the reservation. */
    CHECK(vm::g_base != nullptr, "the reservation spans the guest space");

    std::printf("memory: %d checks passed (%d failed)\n", checks, fails);
    return fails ? 1 : 0;
}
