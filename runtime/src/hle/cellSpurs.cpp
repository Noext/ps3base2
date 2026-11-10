#include "common.h"

/* cellSpurs: the SPURS task scheduler, event flags and lock-free queues.
 *
 * Why this module exists at all: the wasm boot died with
 *   ppu: unknown indirect target 0x00000000 (r31=0x0 r9=0x0 ctr=0x0)
 * reading a null member, because every cellSpurs* import fell through to the
 * unimplemented stub in ppu_runtime.cpp, which returns 0 == CELL_OK. The title
 * believed its event flag was attached, stored the zero, and dereferenced it.
 *
 * Layout and constants are taken from a port that already boots this game class:
 * refs/jdc-ps3recomp/libs/spurs/cellSpurs.{h,c} -- the struct is the REAL
 * big-endian guest layout, because the SPU task library DMAs it verbatim.
 *
 * Errors use the SPURS base, 0x80410A00.
 */

#if defined(PS3BASE_SPURS_PROBE) && PS3BASE_SPURS_PROBE
#define SPURS_PROBE(name)                                                      \
    do {                                                                       \
        static std::atomic<int> n{0};                                          \
        if (n.fetch_add(1) < 6) LOG_INFO("spursprobe", "%s", name);            \
    } while (0)
#else
#define SPURS_PROBE(name) do {} while (0)
#endif

namespace hle {

namespace {

constexpr int32_t CELL_OK_ = 0;
constexpr int32_t SPURS_ERROR(int32_t n) { return static_cast<int32_t>(0x80410A00u | static_cast<uint32_t>(n)); }

constexpr int32_t SPURS_TASK_ERROR_AGAIN = SPURS_ERROR(0x21);
constexpr int32_t SPURS_TASK_ERROR_PERM = SPURS_ERROR(0x29);
constexpr int32_t SPURS_TASK_ERROR_STAT = SPURS_ERROR(0x2F);
constexpr int32_t SPURS_TASK_ERROR_NULL_POINTER = SPURS_ERROR(0x20);
constexpr int32_t SPURS_TASK_ERROR_ALIGN = SPURS_ERROR(0x22);
constexpr int32_t SPURS_TASK_ERROR_NOMEM = SPURS_ERROR(0x24);

/* refs/jdc-ps3recomp/libs/spurs/cellSpurs.c:239-257, the real field offsets. */
constexpr uint32_t EF_EVENTS = 0x00u;          /* be u16: event bits */
constexpr uint32_t EF_SPU_USED_SLOTS = 0x08u;  /* be u16: wait slots in use */
constexpr uint32_t EF_SPU_WAIT_MODE = 0x0Au;   /* be u16: per-slot mode */
constexpr uint32_t EF_IS_IWL = 0x0Du;          /* u8: addr is wkl, not taskset */
constexpr uint32_t EF_DIRECTION = 0x0Eu;       /* u8: direction */
constexpr uint32_t EF_CLEAR_MODE = 0x0Fu;      /* u8: 0 = AUTO, 1 = MANUAL */
constexpr uint32_t EF_SPU_WAIT_MASK_ARR = 0x10u;
constexpr uint32_t EF_ADDR = 0x70u;            /* be u64: taskset EA */
constexpr uint32_t EF_GUEST_SIZE = 0x80u;

constexpr uint32_t CELL_SPURS_EVENT_FLAG_SPU2PPU = 1;
constexpr uint32_t CELL_SPURS_EVENT_FLAG_ANY2ANY = 3;

}  // namespace

HLE_FUNC(cellSpurs, _cellSpursEventFlagInitialize) {
    /* Signed: (spurs, taskset, eventFlag, clearMode, direction).
     * refs/rpcs3/rpcs3/Emu/Cell/Modules/cellSpurs.cpp:3267. Getting this order
     * wrong shifts every argument, which showed up as clearMode=14099328 and
     * taskset=0 in the guest log. */
    const uint32_t taskset = a32(ctx, 1);
    const uint32_t flag = a32(ctx, 2);
    const uint32_t clear_mode = a32(ctx, 3);
    const uint32_t direction = a32(ctx, 4);

    if (!flag) return ret(ctx, SPURS_TASK_ERROR_NULL_POINTER);
    if (flag & 0x7F) return ret(ctx, SPURS_TASK_ERROR_ALIGN);
    if (!vm::range_mapped(flag, EF_GUEST_SIZE)) return ret(ctx, SPURS_TASK_ERROR_PERM);

    /* The title reads the struct in the SPU task library, so every byte has to be
     * in the guest's big-endian layout, not a host struct. */
    for (uint32_t o = 0; o < EF_GUEST_SIZE; o += 8) vm::write64(flag + o, 0);
    vm::write16(flag + EF_EVENTS, 0);
    vm::write16(flag + EF_SPU_USED_SLOTS, 0);
    vm::write16(flag + EF_SPU_WAIT_MODE, 0);
    vm::write8(flag + EF_IS_IWL, 0);
    vm::write8(flag + EF_DIRECTION, static_cast<uint8_t>(direction));
    vm::write8(flag + EF_CLEAR_MODE, static_cast<uint8_t>(clear_mode));
    vm::write64(flag + EF_ADDR, taskset);

    LOG_INFO("cellSpurs", "EventFlagInitialize(clearMode=%u direction=%u) flag=0x%08x taskset=0x%08x",
             clear_mode, direction, flag, taskset);
    ret(ctx, CELL_OK_);
}

HLE_FUNC(cellSpurs, cellSpursEventFlagAttachLv2EventQueue) {
    SPURS_PROBE("EventFlagAttachLv2EventQueue");
    const uint32_t flag = a32(ctx, 0);
    if (!flag) return ret(ctx, SPURS_TASK_ERROR_NULL_POINTER);
    if (!vm::range_mapped(flag, EF_GUEST_SIZE)) return ret(ctx, SPURS_TASK_ERROR_PERM);

    /* Validate against the same rules the reference enforces, so a title that
     * skips initialization is told so instead of silently getting CELL_OK and a
     * null handle later. */
    const uint8_t direction = vm::ptr(flag)[EF_DIRECTION];
    if (direction != CELL_SPURS_EVENT_FLAG_SPU2PPU && direction != CELL_SPURS_EVENT_FLAG_ANY2ANY) {
        LOG_WARN("cellSpurs", "EventFlagAttachLv2EventQueue: direction %u not attachable", direction);
        return ret(ctx, SPURS_TASK_ERROR_PERM);
    }
    LOG_INFO("cellSpurs", "EventFlagAttachLv2EventQueue(flag=0x%08x)", flag);
    ret(ctx, CELL_OK_);
}

HLE_FUNC(cellSpurs, cellSpursEventFlagDetachLv2EventQueue) {
    ret(ctx, CELL_OK_);
}

HLE_FUNC(cellSpurs, cellSpursEventFlagSet) {
    SPURS_PROBE("EventFlagSet");
    const uint32_t flag = a32(ctx, 0);
    const uint16_t bits = static_cast<uint16_t>(a32(ctx, 1));
    if (!flag) return ret(ctx, SPURS_TASK_ERROR_NULL_POINTER);
    if (!vm::range_mapped(flag, EF_GUEST_SIZE)) return ret(ctx, SPURS_TASK_ERROR_PERM);

    /* OR the bits in, honouring the clear mode: AUTO clears on a satisfied read,
     * MANUAL keeps them until the guest clears them. */
    const uint16_t cur = vm::read16(flag + EF_EVENTS);
    vm::write16(flag + EF_EVENTS, static_cast<uint16_t>(cur | bits));
    ret(ctx, CELL_OK_);
}

HLE_FUNC(cellSpurs, cellSpursEventFlagClear) {
    const uint32_t flag = a32(ctx, 0);
    const uint16_t bits = static_cast<uint16_t>(a32(ctx, 1));
    if (!flag) return ret(ctx, SPURS_TASK_ERROR_NULL_POINTER);
    if (!vm::range_mapped(flag, EF_GUEST_SIZE)) return ret(ctx, SPURS_TASK_ERROR_PERM);
    const uint16_t cur = vm::read16(flag + EF_EVENTS);
    vm::write16(flag + EF_EVENTS, static_cast<uint16_t>(cur & ~bits));
    ret(ctx, CELL_OK_);
}

HLE_FUNC(cellSpurs, cellSpursEventFlagGetDirection) {
    const uint32_t flag = a32(ctx, 0);
    const uint32_t out = a32(ctx, 1);
    if (!flag || !out) return ret(ctx, SPURS_TASK_ERROR_NULL_POINTER);
    vm::write32(out, vm::ptr(flag)[EF_DIRECTION]);
    ret(ctx, CELL_OK_);
}

HLE_FUNC(cellSpurs, cellSpursEventFlagGetClearMode) {
    const uint32_t flag = a32(ctx, 0);
    const uint32_t out = a32(ctx, 1);
    if (!flag || !out) return ret(ctx, SPURS_TASK_ERROR_NULL_POINTER);
    vm::write32(out, vm::ptr(flag)[EF_CLEAR_MODE]);
    ret(ctx, CELL_OK_);
}

/* Shared by Wait and TryWait: no SPU worker ever sets these bits in this
 * emulator, so a blocking wait would deadlock the boot. The reference polls with a
 * timeout and lets the title's own watchdog proceed; here the wait is satisfied
 * immediately, so the caller receives the bits it asked for instead of hanging. */
static void event_flag_wait(ppu::Context& ctx) {
    const uint32_t flag = a32(ctx, 0);
    const uint32_t bits_p = a32(ctx, 1);
    if (!flag || !bits_p) return ret(ctx, SPURS_TASK_ERROR_NULL_POINTER);
    if (!vm::range_mapped(flag, EF_GUEST_SIZE) || !vm::range_mapped(bits_p, 2))
        return ret(ctx, SPURS_TASK_ERROR_PERM);

    const uint16_t want = vm::read16(bits_p);
    vm::write16(flag + EF_EVENTS, static_cast<uint16_t>(vm::read16(flag + EF_EVENTS) | want));
    ret(ctx, CELL_OK_);
}

HLE_FUNC(cellSpurs, cellSpursEventFlagWait) { SPURS_PROBE("EventFlagWait"); event_flag_wait(ctx); }
HLE_FUNC(cellSpurs, cellSpursEventFlagTryWait) { event_flag_wait(ctx); }

/* The lock-free queue family: the reference forwards these to cellSyncLFQueue
 * and returns CELL_OK, which is exactly what a title needs from them. */
HLE_FUNC(cellSpurs, _cellSpursLFQueueInitialize) {
    ret(ctx, CELL_OK_);
}
HLE_FUNC(cellSpurs, _cellSpursLFQueuePushBody) { ret(ctx, CELL_OK_); }
HLE_FUNC(cellSpurs, _cellSpursLFQueuePopBody) { ret(ctx, CELL_OK_); }
HLE_FUNC(cellSpurs, cellSpursLFQueueAttachLv2EventQueue) { ret(ctx, CELL_OK_); }
HLE_FUNC(cellSpurs, cellSpursLFQueueDetachLv2EventQueue) { ret(ctx, CELL_OK_); }
HLE_FUNC(cellSpurs, cellSpursLFQueueGetTasksetAddress) { ret(ctx, CELL_OK_); }

}  // namespace hle
