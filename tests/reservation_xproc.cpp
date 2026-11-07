/* ps3base -- cross-processor reservation invalidation, tested.
 *
 * PowerPC ldarx/stcx. reserves a 128-byte line, and an SPU putllc must
 * invalidate that reservation: the hardware guarantees the PPU's stdcx fails
 * if any agent wrote the line in between. Without it a PPU thread commits a
 * value it never observed, which is the corruption stcx. exists to prevent.
 */
#include "runtime/memory.h"
#include "runtime/reservation.h"

#include <cstdio>
#include <cstring>

static int fails = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; std::fprintf(stderr, "[FAIL] %s\n", msg); } \
} while (0)

namespace {
/* A mapped guest line. vm::init only reserves the address space; a region must
 * be committed before it can be touched, which is what the loader does for the
 * title's segments. */
uint32_t alloc_line() {
    const uint32_t a = vm::reserve_slot(0x10000, 0x1000);
    if (!a) return 0;
    vm::commit(a, 0x10000);
    return a + 0x80;
}

struct PpuReservation {
    uint32_t addr = 0;
    uint64_t rtime = 0;
    uint8_t data[8] = {};
};

PpuReservation ppu_ldarx(uint32_t ea) {
    PpuReservation r;
    r.addr = ea;
    std::atomic<uint64_t>& ver = rsv::version(ea);
    for (;;) {
        uint64_t v1 = ver.load(std::memory_order_acquire);
        std::memcpy(r.data, vm::ptr(ea), 8);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (ver.load(std::memory_order_acquire) == v1) {
            r.rtime = v1;
            return r;
        }
    }
}

bool ppu_stdcx(PpuReservation& r, uint64_t value) {
    uint64_t be = __builtin_bswap64(value);
    uint8_t data[8];
    std::memcpy(data, r.data, 8);
    std::memcpy(data, &be, 8);
    return rsv::commit_if(r.addr & ~127u, r.rtime, r.data, data, r.addr & 127u, 8);
}
}  // namespace

int main()
{
    if (!vm::init()) {
        std::fprintf(stderr, "vm::init failed\n");
        return 1;
    }

    const uint32_t kLine = alloc_line();
    if (!kLine) {
        std::fprintf(stderr, "could not map a test line\n");
        return 1;
    }

    uint64_t seed = 0x1122334455667788ull;
    rsv::store_range(kLine, reinterpret_cast<const uint8_t*>(&seed), 8);
    {
        PpuReservation r = ppu_ldarx(kLine);
        CHECK(ppu_stdcx(r, 0xAABBu), "stdcx on an untouched line must succeed");
    }

    {
        uint8_t line[128];
        std::memcpy(line, vm::ptr(kLine & ~127u), 128);
        uint64_t want = 0xDEADBEEFCAFEF00Dull;
        std::memcpy(line, &want, 8);

        PpuReservation r = ppu_ldarx(kLine);
        rsv::store_unconditional(kLine & ~127u, line);
        CHECK(!ppu_stdcx(r, 0xCCDDull),
              "stdcx must fail after an SPU write invalidated the reservation");
    }

    {
        PpuReservation r = ppu_ldarx(kLine);
        uint64_t other = 0x0F0F0F0F0F0F0F0Full;
        rsv::store_range(kLine, reinterpret_cast<const uint8_t*>(&other), 8);
        CHECK(!ppu_stdcx(r, 0xEEFFull),
              "stdcx must fail after a store_range write invalidated the reservation");
    }

    {
        PpuReservation r = ppu_ldarx(kLine);
        CHECK(ppu_stdcx(r, 0x1234ull), "a fresh reservation must commit");
    }

    std::printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
