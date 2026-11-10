/* SPURS event flag: the struct is written in the guest's REAL big-endian
 * layout, because the SPU task library DMAs it verbatim. Getting one field offset
 * wrong is invisible until the guest reads garbage, so the offsets from
 * refs/jdc-ps3recomp/libs/spurs/cellSpurs.c:239-257 are asserted here.
 *
 * This test links only the module under test plus guest memory, so it stays a
 * unit test rather than dragging in the whole PPU runtime.
 */
#include "runtime/memory.h"

#include <cstdio>
#include <cstring>

/* The handler bodies are static in the module, so include it and drive the same
 * functions the import path calls. */
#include "../runtime/src/hle/cellSpurs.cpp"

/* hle::Reg's constructor normally lives in ppu_runtime.cpp and registers into the
 * import table. This unit test calls the handlers directly, so it does not need
 * the table, and the constructor is a no-op here rather than pulling in the whole
 * PPU runtime. */
namespace hle {
Reg::Reg(const char*, const char*, Fn) {}
}

static int failures = 0;
static int checks = 0;
static void check(bool ok, const char* what) {
    checks++;
    if (!ok) {
        failures++;
        std::printf("FAIL: %s\n", what);
    }
}

int main() {
    if (!vm::init()) return 1;

    /* Allocate for real: writing to an uncommitted guest address faults, which is
     * the whole point of the memory_map suite. */
    const uint32_t base = vm::alloc(vm::Region::Main, 0x10000, 0x10000);
    if (!base) {
        std::printf("FAIL: could not allocate guest memory\n");
        return 1;
    }
    const uint32_t taskset = base;
    const uint32_t flag = base + 0x100;   /* 0x80-aligned */
    for (uint32_t i = 0; i < 0x200; i++) vm::write8(taskset + i, 0xAA);

    auto call = [](void (*fn)(ppu::Context&, uint8_t*), uint64_t a0, uint64_t a1, uint64_t a2,
                   uint64_t a3, uint64_t a4 = 0) -> uint64_t {
        ppu::Context c{};
        c.gpr[3] = a0; c.gpr[4] = a1; c.gpr[5] = a2; c.gpr[6] = a3; c.gpr[7] = a4;
        fn(c, vm::g_base);
        return c.gpr[3];
    };

    /* Real signature: (spurs, taskset, eventFlag, clearMode, direction). */
    check(call(hle::hle_cellSpurs__cellSpursEventFlagInitialize, 0 /* spurs */, taskset, flag,
               1 /* MANUAL */, 3 /* ANY2ANY */) == 0,
          "EventFlagInitialize returns CELL_OK");

    check(vm::ptr(flag)[0x0E] == 3, "direction at 0x0E is ANY2ANY");
    check(vm::ptr(flag)[0x0F] == 1, "clearMode at 0x0F is MANUAL");
    check(vm::ptr(flag)[0x0D] == 0, "isIwl at 0x0D is 0");
    check(vm::read64(flag + 0x70) == taskset, "taskset EA at 0x70 round-trips");
    check(vm::read16(flag + 0x00) == 0, "events at 0x00 start cleared");

    check(call(hle::hle_cellSpurs_cellSpursEventFlagAttachLv2EventQueue, flag, 0, 0, 0) == 0,
          "Attach succeeds after Initialize set a valid direction");

    const uint32_t raw = base + 0x400;
    for (uint32_t i = 0; i < 0x80; i++) vm::write8(raw + i, 0);
    check(call(hle::hle_cellSpurs_cellSpursEventFlagAttachLv2EventQueue, raw, 0, 0, 0) != 0,
          "Attach REFUSES an uninitialized flag instead of returning CELL_OK");

    check(call(hle::hle_cellSpurs_cellSpursEventFlagSet, flag, 4, 0, 0) == 0, "Set returns CELL_OK");
    check(vm::read16(flag + 0x00) == 4, "Set wrote the bits at 0x00");
    check(call(hle::hle_cellSpurs_cellSpursEventFlagClear, flag, 4, 0, 0) == 0, "Clear returns CELL_OK");
    check(vm::read16(flag + 0x00) == 0, "Clear removed the bits");

    std::printf("spurs: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
