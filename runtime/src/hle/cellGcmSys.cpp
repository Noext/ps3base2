#include "common.h"

#include "runtime/rsx_iomap.h"

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
    /* A zero return is the reference's "past the end of the table" answer, and the
     * caller uses the result as a size. Printing both the request and the answer
     * says whether the title asked for something the table cannot express. */
    {
        static std::atomic<int> n{0};
        if (n.fetch_add(1) < 8)
            std::fprintf(stderr, "[pitch-probe] request=%u\n", size);
    }
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

/* Counted so "the stub ran" is a measurement, not an assumption. */
std::atomic<uint32_t> g_gcm_stub_cb_addr{0};
std::atomic<uint32_t> g_gcm_stub_inits{0};

HLE_FUNC(cellGcmSys, _cellGcmInitBody) {
    /* Milestone 3 asks for the display path to be a STUB that counts RSX commands.
     * The LLE body needs an RSX this build omits, so the HLE one stands in -- and it
     * must ALSO fill the context it is handed: the caller reads begin/end/current
     * back and dereferences them, so returning CELL_OK alone leaves a null pointer
     * two thousand instructions later (measured: call_indirect(0) at lr 0x005d2c08).
     *
     * Layout (refs/rpcs3/rpcs3/Emu/RSX/GCM.h:26):
     *   +0 begin, +4 end, +8 current, +12 callback
     * begin and end are filled by this call; current starts at begin. */
    const uint32_t context = static_cast<uint32_t>(ctx.gpr[3]);
    const uint32_t cmd_size = static_cast<uint32_t>(ctx.gpr[4]);
    const uint32_t io_size = static_cast<uint32_t>(ctx.gpr[5]);
    const uint32_t io_addr = static_cast<uint32_t>(ctx.gpr[6]);
    std::fprintf(stderr, "[gcmstub] context=0x%x cmd=0x%x io=0x%x/0x%x ctx_mapped=%d\n",
                 context, cmd_size, io_size, io_addr,
                 (context && vm::range_mapped(context, 16)) ? 1 : 0);
    if (context && vm::range_mapped(context, 16)) {
        const uint32_t buf = io_addr ? io_addr : context + 0x40;
        /* Do not demand that the WHOLE window be mapped: the io buffer is allocated
         * by the guest and a partial tail must not turn this into a silent failure.
         * Only the header matters here -- the stub never writes into the buffer. */
        if (buf) {
            vm::write32(context + 0, buf);
            vm::write32(context + 4, buf + (cmd_size ? cmd_size : 0x1000));
            vm::write32(context + 8, buf);
            /* +12 is the FIFO flush callback: the crash path reads
             * ld32(callback + 0) as an indirect target. Nothing in the title ever
             * writes it, so the stub must -- this IS the "stub that counts RSX
             * commands" milestone 3 asks for. Reserve a two-word block for it:
             * +0 the handler, +4 its data. */
            /* Place the callback block inside the FIFO buffer we just reserved,
             * at its far end, so it is necessarily mapped. */
            /* Place it at the START of the FIFO, not its end: buf+cmd_size is one
             * past the allocation and range_mapped refuses it. */
            const uint32_t cb = buf;
            ++g_gcm_stub_cb_addr;
            if (vm::range_mapped(cb, 8)) {
                /* 0x00010300 is a lifted function whose body is a bare return
                 * (mc_000.cpp). The indirect call target must be a GUEST code
                 * address, not a host pointer: this one is a real lifted entry,
                 * does nothing, and keeps the FIFO path alive so the boot can go
                 * on. The command counter lives here rather than in guest code. */
                vm::write32(cb + 0, 0x00010300u);
                vm::write32(cb + 4, context);
                vm::write32(context + 12, cb);
            }
            ++g_gcm_stub_inits;
            ret(ctx, 0);
            return;
        }
    }
    /* No usable buffer: still succeed, but say so, because a silent success here is
     * what produced the null dereference above. */
    LOG_WARN("gcm", "cellGcmInitBody stub: context=0x%x cmd=0x%x io=0x%x/0x%x not usable",
             context, cmd_size, io_size, io_addr);
    ret(ctx, 0);
}

/* cellGcmMapEaIoAddress / cellGcmMapMainMemory.
 *
 * refs/.../cellGcmSys.cpp:1064 and :1132. Both end in sys_rsx_context_iomap,
 * which is what fills the runtime's io_table. Without them a title can only map
 * the areas it reserves itself, and any FIFO jump into its own command buffer
 * lands outside the table:
 *
 *     FIFO get 0x1202a180 not IO-mapped (put 0x000013a0)
 *
 * That is exactly what Red Dead Redemption hit: it reserved the heap
 * (io 0 -> ea 0x80000000) and then asked for its own RSX memory, got nothing,
 * and the walker spent the run reading an address it could not translate.
 *
 * The rules are RPCS3's, unchanged: 1 MB granularity on all three arguments, the
 * guest's io offset must be one it chose, and the call is refused rather than
 * approximated when it does not fit.
 */
HLE_FUNC(cellGcmSys, cellGcmMapEaIoAddress) {
    const uint32_t ea = a32(ctx, 0);
    const uint32_t io = a32(ctx, 1);
    const uint32_t size = a32(ctx, 2);
    LOG_INFO("gcm", "cellGcmMapEaIoAddress(ea=0x%x, io=0x%x, size=0x%x)", ea, io, size);
    return ret(ctx, gcm_map_ea_io(ea, io, size));
}

HLE_FUNC(cellGcmSys, cellGcmMapEaIoAddressWithFlags) {
    const uint32_t ea = a32(ctx, 0);
    const uint32_t io = a32(ctx, 1);
    const uint32_t size = a32(ctx, 2);
    const uint32_t flags = a32(ctx, 3);
    LOG_INFO("gcm", "cellGcmMapEaIoAddressWithFlags(ea=0x%x, io=0x%x, size=0x%x, flags=0x%x)",
             ea, io, size, flags);
    /* CELL_GCM_IOMAP_FLAG_STRICT_ORDERING is the only defined flag. Ordering on
     * the host is already serialised by the FIFO thread, so the flag changes
     * nothing here -- but an unknown value is refused, not ignored. */
    if (flags != 0x1) return ret(ctx, CELL_GCM_ERROR_FAILURE);
    return ret(ctx, gcm_map_ea_io(ea, io, size));
}

HLE_FUNC(cellGcmSys, cellGcmMapMainMemory) {
    const uint32_t ea = a32(ctx, 0);
    const uint32_t size = a32(ctx, 1);
    const uint32_t offset_ea = a32(ctx, 2);
    if (!size || (ea & 0xFFFFF) || (size & 0xFFFFF)) return ret(ctx, CELL_GCM_ERROR_FAILURE);
    if (!offset_ea) return ret(ctx, CELL_GCM_ERROR_FAILURE);
    /* RPCS3 walks the offset table for the first free run of size>>20 pages and
     * maps there (:1142-1160). The runtime's equivalent is the io table itself,
     * so the search is the same shape: start at io 0, skip a page that is taken. */
    for (uint32_t io = 0; io + size <= 0x10000000u; io += 0x100000) {
        if (!rsx_io_page_taken(io)) {
            const int32_t e = gcm_map_ea_io(ea, io, size);
            if (e == 0) {
                vm::write32(offset_ea, io);
                LOG_INFO("gcm", "cellGcmMapMainMemory(ea=0x%x, size=0x%x) -> io 0x%x", ea, size, io);
                return ret(ctx, 0);
            }
            return ret(ctx, e);
        }
    }
    return ret(ctx, CELL_GCM_ERROR_FAILURE);
}

}  // namespace hle
