#include "common.h"

/* cellGcmSys: the RSX command interface.
 *
 * The title calls these during boot and treats a zero return as a valid pointer
 * or size, so an unimplemented stub that returns 0 sends it jumping through NULL.
 * Measured: the boot died at 'unknown indirect target 0x00000000' right after
 * 'unimplemented cellGcmSys::cellGcmGetTiledPitchSize', so these return real
 * values rather than zeros.
 *
 * Reference: refs/rpcs3/rpcs3/Emu/Cell/Modules/cellGcmSys.cpp:60 (the pitch
 * table) and :389 (the lookup, which returns 0 only past the end of the table).
 */

namespace hle {

namespace {

/* refs/rpcs3/rpcs3/Emu/Cell/Modules/cellGcmSys.cpp:60-71, verbatim. */
constexpr uint32_t kTiledPitches[] = {
    0x00000000, 0x00000200, 0x00000300, 0x00000400, 0x00000500, 0x00000600,
    0x00000700, 0x00000800, 0x00000A00, 0x00000C00, 0x00000D00, 0x00000E00,
    0x00001000, 0x00001400, 0x00001800, 0x00001A00, 0x00001C00, 0x00002000,
    0x00002800, 0x00003000, 0x00003400, 0x00003800, 0x00004000, 0x00005000,
    0x00006000, 0x00006800, 0x00007000, 0x00008000, 0x0000A000, 0x0000C000,
    0x0000D000, 0x0000E000, 0x00010000,
};
constexpr size_t kTiledPitchCount = sizeof(kTiledPitches) / sizeof(kTiledPitches[0]);

}  // namespace

HLE_FUNC(cellGcmSys, cellGcmGetTiledPitchSize) {
    const uint32_t size = a32(ctx, 0);
    /* The smallest pitch strictly greater than the request, as the reference does. */
    for (size_t i = 0; i + 1 < kTiledPitchCount; i++) {
        if (kTiledPitches[i] < size && size <= kTiledPitches[i + 1]) {
            return ret(ctx, static_cast<int32_t>(kTiledPitches[i + 1]));
        }
    }
    ret(ctx, 0);
}

HLE_FUNC(cellGcmSys, cellGcmInitDefaultFifoMode) {
    /* Enables the default FIFO mode. Nothing to emulate without a GPU: the title
     * only checks the error code. */
    ret(ctx, 0);
}

HLE_FUNC(cellGcmSys, cellGcmSetDebugOutputLevel) {
    ret(ctx, 0);
}

HLE_FUNC(cellGcmSys, cellGcmGetConfiguration) {
    /* CellGcmConfig, 7 u32 fields at the pointer the guest supplies. The values
     * are the ones the reported video mode implies: 1920x1080, 4 bytes per pixel,
     * the standard local and main memory windows. */
    const uint32_t p = a32(ctx, 0);
    if (!p || !vm::range_mapped(p, 28)) return ret(ctx, static_cast<int32_t>(0x80010002));
    vm::write32(p + 0, 0xC0000000u);   /* localAddress */
    /* RPCS3 reports a 249 MB local window (cellGcmSys.cpp:425-430:
     * local_size = 0xf900000, local_addr = 0xc0000000). Announcing 2 MB was a
     * guess, and a title that sizes its surface pool against this value would
     * see a window too small to hold one. */
    vm::write32(p + 4, 0x0F900000u);   /* localSize, as RPCS3 reports it */
    vm::write32(p + 8, 0x00000000u);   /* mainAddress */
    vm::write32(p + 12, 0x00000000u);  /* mainSize */
    vm::write32(p + 16, 0x00000000u);  /* ioAddress */
    vm::write32(p + 20, 0x00000000u);  /* ioSize */
    vm::write32(p + 24, 0x00000100u);  /* memoryFrequency */
    ret(ctx, 0);
}

HLE_FUNC(cellGcmSys, _cellGcmInitBody) {
    /* The real body takes a context pointer and builds the command FIFO. Without a
     * GPU it only has to report success: the title proceeds to load its shaders
     * and then fails on the first draw, which is a later problem than the boot. */
    ret(ctx, 0);
}

}  // namespace hle
