#include "common.h"

#include "runtime/rsx_iomap.h"
#include "gpu/gcm_fifo.h"

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

/* RPCS3 reserves 4 KB at the start of the default command buffer and 4 bytes at
 * the end for the jump back (cellGcmSys.cpp:479-486). */
constexpr uint32_t kGcmRingReserve = 0x1000;

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
        /* The ring the title builds packets in, and the RSX blocks it addresses
         * through cellGcmGetControlRegister / cellGcmGetLabelAddress.
         *
         * refs/rpcs3/rpcs3/Emu/Cell/Modules/cellGcmSys.cpp:_cellGcmInitBody does
         * three things this stub must reproduce, or the guest's own libgcm has
         * nothing to talk to:
         *   gcmMapEaIoAddress(ioAddress, 0, ioSize)  -- so io 0 is the ring
         *   a context block whose +0x40 is CellGcmControl and whose reports
         *     window holds the labels (cellGcmGetLabelAddress = base + 0x10*index)
         *   begin/end/current/callback in the CellGcmContextData the caller holds
         * The runtime's own sys_rsx_context_allocate uses the same 3 MB layout
         * (lv2/rsx.cpp: g_rsx.dma_control = area, driver_info = +1 MB, reports =
         * +2 MB); after this round the HLE path must agree with it. */
        const uint32_t buf = io_addr ? io_addr : context + 0x40;
        if (buf) {
            /* io table: ring EA -> io 0, so the put/get offsets the guest writes
             * translate back to the buffer it built. Failure is reported, not
             * hidden: without this mapping the reader cannot read the ring. */
            const int32_t map_rc = gcm_map_ea_io(buf, 0, io_size ? io_size : 0x100000);
            if (map_rc != 0)
                LOG_WARN("gcm", "_cellGcmInitBody: ring io map refused (ea=0x%x size=0x%x rc=0x%x)",
                         buf, io_size, static_cast<uint32_t>(map_rc));
            /* The RSX area must be REAL committed memory. A fallback to the
             * guest's own page looked reasonable and was measured wrong: the
             * region allocator refused (0x300000 in RsxContext), the fallback
             * produced 0xd00000 -- an address the pagemap does not cover, so
             * every label write was silently dropped by the bounds check while
             * reads returned zeros. Allocate in main memory, which is the region
             * the runtime always has, and say so if even that fails. */
            uint32_t area = vm::alloc(vm::Region::RsxContext, 0x300000, 0x100000);
            const char* area_region = "rsx";
            if (!area) {
                area = vm::alloc(vm::Region::Main, 0x300000, 0x100000);
                area_region = "main";
            }
            if (!area || !vm::range_mapped(area, 0x300000)) {
                LOG_ERROR("gcm", "_cellGcmInitBody: no RSX context area (0x%08x, mapped=%d) -- labels cannot be written",
                          area, area ? (vm::range_mapped(area, 0x300000) ? 1 : 0) : 0);
                ret(ctx, 0);
                return;
            }
            LOG_INFO("gcm", "RSX context area at 0x%08x (%s, 3 MB)", area, area_region);
            gpu::gcm::init(area, buf, cmd_size ? cmd_size : 0x1000);
            LOG_INFO("gcm", "_cellGcmInitBody: control=0x%08x labels=0x%08x ring=0x%08x/0x%x io_map=0x%x",
                     gpu::gcm::control_address(), gpu::gcm::label_address(), buf, cmd_size,
                     static_cast<uint32_t>(map_rc));
            /* THE INDIRECTION, which is what the null indirect call was.
             *
             * The argument is a vm::pptr<CellGcmContextData>: the guest hands us
             * the address of a STRUCTURE HOLDING A POINTER, and reads back the
             * pointer. refs/rpcs3/cellGcmSys.cpp:487 does exactly that:
             *
             *     vm::write<CellGcmContextData>(gcm_cfg.gcm_info.context_addr, ...);
             *     context->set(gcm_cfg.gcm_info.context_addr);
             *
             * Writing begin/end/current/callback AT the argument address instead
             * made ld32(0xd72800) return the ring base (our begin field), and the
             * guest then passed that ring buffer as if it were the context: its
             * ld32(buffer+12) read a packet word (zero), and call_indirect(0)
             * followed. Measured: "call target 0x00000000 is UNMAPPED
             * (lr=0x005d2c08 r3=0x40100000 r31=0x40100000)".
             *
             * So: the context data lives in the RSX area, the guest pointer points
             * at it, and the ring is described the way RPCS3 does (4 KB reserved at
             * the start, 4 bytes at the end for the jump back). */
            const uint32_t cdata = area;
            vm::write32(cdata + 0, buf + kGcmRingReserve);
            vm::write32(cdata + 4, buf + (cmd_size ? cmd_size : 0x1000) - 4);
            vm::write32(cdata + 8, buf + kGcmRingReserve);
            vm::write32(context + 0, cdata);
            /* +12 is the FIFO flush callback: the crash path reads
             * ld32(callback + 0) as an indirect target. Nothing in the title ever
             * writes it, so the stub must -- this IS the "stub that counts RSX
             * commands" milestone 3 asks for. Reserve a two-word block for it:
             * +0 the handler, +4 its data. */
            /* Place the callback block inside the FIFO buffer we just reserved,
             * at its far end, so it is necessarily mapped. */
            /* Place it at the START of the FIFO, not its end: buf+cmd_size is one
             * past the allocation and range_mapped refuses it. */
            /* The callback block goes at the END of the RSX context area, NOT in
             * the ring: the ring's first words are the guest's own packets
             * (measured: placing it at buf put 0x00010300 in front of the first
             * packet, and the reader then walked 55 zero words). */
            const uint32_t cb = area + 0x2FFFF0u;
            ++g_gcm_stub_cb_addr;
            if (vm::range_mapped(cb, 8)) {
                /* 0x00010300 is a lifted function whose body is a bare return
                 * (mc_000.cpp). The indirect call target must be a GUEST code
                 * address, not a host pointer: this one is a real lifted entry,
                 * does nothing, and keeps the FIFO path alive so the boot can go
                 * on. The command counter lives here rather than in guest code. */
                vm::write32(cb + 0, 0x00010300u);
                vm::write32(cb + 4, cdata);
                vm::write32(cdata + 12, cb);
                LOG_INFO("gcm", "callback block at 0x%08x (handler 0x00010300)", cb);
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


/* --- the label / control / offset interface ------------------------------
 *
 * These three are the whole reason this module exists. With the display path
 * stubbed, the title's libgcm still builds real NV47 packets and kicks the ring
 * through cellGcmGetControlRegister + cellGcmAddressToOffset; it then waits for
 * the back-end label it asked for. On hardware the command processor writes that
 * label. Here the reader in gpu/gcm_fifo.cpp does, and GetLabelAddress is the
 * natural place to run it: the wait loop calls it once per 30 us spin, so a
 * release that has been queued is applied on the next turn.
 *
 * refs/rpcs3/rpcs3/Emu/Cell/Modules/cellGcmSys.cpp:159
 *     cellGcmGetLabelAddress(index) = fw.label_addr + 0x10 * index
 */
HLE_FUNC(cellGcmSys, cellGcmGetLabelAddress) {
    const uint32_t index = a32(ctx, 0) & 0xFFu;
    gpu::gcm::drain();
    const uint32_t addr = gpu::gcm::label_address(index);
    {
        static std::atomic<int> n{0};
        if (n.fetch_add(1) < 4)
            LOG_INFO("gcm", "cellGcmGetLabelAddress(%u) -> 0x%08x", index, addr);
    }
    ret(ctx, addr);
}

/* refs/.../cellGcmSys.cpp:280. The guest stores the ring's io offset here to
 * kick the command processor, so this must be the block the reader polls. */
HLE_FUNC(cellGcmSys, cellGcmGetControlRegister) {
    ret(ctx, gpu::gcm::control_address());
}

HLE_FUNC(cellGcmSys, cellGcmGetDefaultCommandWordSize) {
    ret(ctx, 0x400);
}

HLE_FUNC(cellGcmSys, cellGcmGetDefaultSegmentWordSize) {
    ret(ctx, 0x100);
}

/* refs/.../cellGcmSys.cpp:998: an EA the ring builder can address becomes the io
 * offset the control register takes. Local memory first, then the io table --
 * the same order, and a refusal rather than a zero when neither answers.
 */
HLE_FUNC(cellGcmSys, cellGcmAddressToOffset) {
    const uint32_t ea = a32(ctx, 0);
    const uint32_t out = a32(ctx, 1);
    uint32_t io = 0;
    int32_t rc = gpu::gcm::ea_to_io(ea, &io);
    if (rc != 0) {
        static std::atomic<int> n{0};
        if (n.fetch_add(1) < 8)
            LOG_WARN("gcm", "cellGcmAddressToOffset(0x%08x) is not in the io table", ea);
        return ret(ctx, static_cast<int32_t>(0x802100ffu));
    }
    if (out && vm::range_mapped(out, 4)) vm::write32(out, io);
    {
        static std::atomic<int> n{0};
        if (n.fetch_add(1) < 12)
            LOG_INFO("gcm", "cellGcmAddressToOffset(0x%08x) -> 0x%08x (out=0x%08x)", ea, io, out);
    }
    ret(ctx, 0);
}

HLE_FUNC(cellGcmSys, cellGcmIoOffsetToAddress) {
    const uint32_t io = a32(ctx, 0);
    const uint32_t out = a32(ctx, 1);
    const uint32_t ea = gpu::gcm::io_to_ea(io);
    if (!ea) return ret(ctx, static_cast<int32_t>(0x802100ffu));
    if (out && vm::range_mapped(out, 4)) vm::write32(out, ea);
    ret(ctx, 0);
}

/* A flip is requested through the ring (0xE920/0xE924) or through the driver's
 * context attribute; without a display there is nothing to present, but the
 * request must still be consumed so the guest's frame accounting advances. */
HLE_FUNC(cellGcmSys, cellGcmSetFlipMode) {
    gpu::gcm::drain();
    ret(ctx, 0);
}

HLE_FUNC(cellGcmSys, cellGcmSetDisplayBuffer) {
    ret(ctx, 0);
}

/* The title registers a function pointer here and expects the runtime to call it on
 * every flip. Discarding the argument (ret(ctx,0)) meant the handler at 0x00bbe658
 * was NEVER invoked: track E measured 267,220 consecutive empty waits at
 * lr=0x002dc33c in wasm against 3,985 series of median 22 natively. Store it so the
 * flip path can call it. */
static uint32_t g_flip_handler = 0;
uint32_t gcm_flip_handler() { return g_flip_handler; }

HLE_FUNC(cellGcmSys, cellGcmSetFlipHandler) {
    g_flip_handler = static_cast<uint32_t>(ctx.gpr[3]);
    LOG_INFO("gcm", "cellGcmSetFlipHandler(0x%08x)", g_flip_handler);
    ret(ctx, 0);
}

HLE_FUNC(cellGcmSys, cellGcmSetDefaultFifoSize) {
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
