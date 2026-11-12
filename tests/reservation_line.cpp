/* ps3base -- a reservation covers the whole 128-byte line, not the 8 bytes
 * that lwarx happened to load.
 *
 * On Cell the reservation set is the cache line: an lwarx at line+0x10 reserves
 * line+0x00..line+0x7f. Two consequences, and both are tested here:
 *
 *   1. a store into the SAME line, at a different 8-byte block, by another
 *      agent must cancel the reservation -- stwcx. must FAIL. Today it
 *      succeeds, because reserve() snapshots only 8 bytes and commit_if
 *      compares only those 8 bytes, so a write that does not go through rsv::
 *      (the loader, the HLE handlers and the RSX uploader write vm::ptr
 *      directly, 81 such sites) is invisible to the reservation.
 *   2. an stwcx. to a DIFFERENT word of the reserved line must SUCCEED when
 *      nothing wrote in between -- that is what reserving the line means.
 *      Today it fails, because store_conditional() requires the store to be on
 *      the same aligned 8 bytes as the load.
 *
 * The store in test 1 is a plain memcpy into vm::ptr, exactly like
 * runtime/src/loader.cpp:80 and runtime/src/hle/*.cpp do it. That is the point:
 * the failure being reproduced is a guest reservation being committed over data
 * this runtime wrote behind rsv::'s back.
 */
#include "runtime/memory.h"
#include "runtime/reservation.h"
#include "ppu/ops.h"

#include <cstdio>
#include <cstring>

static int fails = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; std::fprintf(stderr, "[FAIL] %s\n", msg); } \
} while (0)

namespace {

uint32_t alloc_line() {
    const uint32_t a = vm::reserve_slot(0x10000, 0x1000);
    if (!a) return 0;
    if (!vm::commit(a, 0x10000)) return 0;
    return a + 0x80;
}

/* The write shape the loader and the HLE handlers use: straight into the guest
 * mapping, no rsv:: call, no version bump. */
void bypass_store32(uint32_t ea, uint32_t value) {
    const uint32_t be = __builtin_bswap32(value);
    std::memcpy(vm::ptr(ea), &be, 4);
}

}  // namespace

int main()
{
    if (!vm::init()) {
        std::fprintf(stderr, "vm::init failed\n");
        return 1;
    }

    const uint32_t line = alloc_line();
    if (!line) {
        std::fprintf(stderr, "could not map a test line\n");
        return 1;
    }
    /* Two 8-byte blocks of the same 128-byte line, 16 bytes apart. */
    const uint32_t a = line + 0x10;
    const uint32_t b = line + 0x20;

    vm::write32(a, 0x11111111u);
    vm::write32(b, 0x22222222u);

    ppu::Context ctx{};
    uint8_t* base = vm::g_base;

    /* 1. lwarx at a, a write by another agent to b (same line), stwcx. at a. */
    {
        const uint32_t v = ppu::lwarx(ctx, base, a);
        std::fprintf(stderr, "  lwarx  [0x%08x] = 0x%08x, then a bypass store to 0x%08x (same line)\n", a, v, b);
        bypass_store32(b, 0x33333333u);
        const bool ok = ppu::stwcx(ctx, base, a, v + 1);
        std::fprintf(stderr, "  stwcx. -> %s\n", ok ? "SUCCESS (reservation survived a write to its own line)" : "failed (correct)");
        CHECK(!ok, "a store to another 8-byte block of the reserved line must cancel the reservation");
    }

    /* 2. The same, but the write lands on a DIFFERENT line: the reservation
     *    must still commit. This is the control that keeps test 1 honest. */
    {
        vm::write32(a, 0x44444444u);
        vm::write32(b, 0x55555555u);
        const uint32_t v = ppu::lwarx(ctx, base, a);
        bypass_store32(line + 0x200, 0x66666666u);
        const bool ok = ppu::stwcx(ctx, base, a, v + 1);
        std::fprintf(stderr, "  stwcx. after a write to a different line -> %s\n", ok ? "success (correct)" : "FAILED");
        CHECK(ok, "a store to a different line must not cancel the reservation");
    }

    /* 3. Reserving the line means stwcx. may target any word in it. */
    {
        vm::write32(a, 0x77777777u);
        vm::write32(b, 0x88888888u);
        const uint32_t v = ppu::lwarx(ctx, base, a);
        (void)v;
        const bool ok = ppu::stwcx(ctx, base, b, 0x99999999u);
        std::fprintf(stderr, "  lwarx at 0x%08x then stwcx. at 0x%08x (same line) -> %s\n", a, b, ok ? "success (correct)" : "FAILED");
        CHECK(ok, "the reservation covers the whole 128-byte line, so stwcx. to another word in it must succeed");
    }

    /* 4. ldarx has the same granule as lwarx. */
    {
        const uint32_t d = line + 0x48;
        const uint32_t e = line + 0x58;
        vm::write64(d, 0x0123456789ABCDEFull);
        vm::write64(e, 0x1111222233334444ull);
        const uint64_t v = ppu::ldarx(ctx, base, d);
        bypass_store32(e, 0xAAAABBBBu);
        const bool ok = ppu::stdcx(ctx, base, d, v + 1);
        std::fprintf(stderr, "  ldarx then a bypass store to the same line -> stdcx. %s\n", ok ? "SUCCESS (wrong)" : "failed (correct)");
        CHECK(!ok, "a store to another block of the line reserved by ldarx must cancel the reservation");
    }

    /* 5. An rsv:: write to the line cancels it too (already worked, kept as a
     *    regression guard against fixing 1 by weakening this). */
    {
        vm::write32(a, 0x01010101u);
        const uint32_t v = ppu::lwarx(ctx, base, a);
        (void)v;
        vm::write32(b, 0x02020202u);
        const bool ok = ppu::stwcx(ctx, base, a, 0x03030303u);
        std::fprintf(stderr, "  lwarx then vm::write32 to the same line -> stwcx. %s\n", ok ? "SUCCESS (wrong)" : "failed (correct)");
        CHECK(!ok, "a vm::write32 into the reserved line must cancel the reservation");
    }

    /* 6. And a successful stwcx. must still publish exactly the loaded value +1
     *    when nothing wrote in between. */
    {
        vm::write32(a, 0x00000010u);
        const uint32_t v = ppu::lwarx(ctx, base, a);
        const bool ok = ppu::stwcx(ctx, base, a, v + 1);
        const uint32_t got = vm::read32(a);
        std::fprintf(stderr, "  clean lwarx/stwcx.: ok=%d counter=0x%08x\n", ok ? 1 : 0, got);
        CHECK(ok && got == 0x11u, "an uncontended lwarx/stwcx. must commit");
    }

    std::printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
