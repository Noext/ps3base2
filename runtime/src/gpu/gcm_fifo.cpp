#include "gpu/gcm_fifo.h"

#include "gpu/rsx_ring.h"
#include "runtime/log.h"
#include "runtime/memory.h"
#include "runtime/regions.h"
#include "runtime/reservation.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <utility>
#include <vector>
#include <cstdint>
#include <cstring>

/* The NV47 command processor, without a GPU.
 *
 * Why this file exists: with the display path stubbed, nothing ever consumed the
 * title's command ring, so the back-end label the title waits on was never
 * written and it spun:
 *
 *     L_2da590:  cellGcmGetLabelAddress(255) -> label
 *                if (label == submitted) goto done;  usleep(30); goto L_2da590
 *
 * The title's libgcm builds its packets in guest code and kicks the ring by
 * storing the ring's io offset into the control register's put pointer
 * (out/ppu/mc_058.cpp f_005d2810: import 133 = cellGcmGetControlRegister,
 * import 121 = cellGcmAddressToOffset, then st32(ctrl+0, offset)). Everything
 * this file does is downstream of that kick.
 *
 * The packet format is the one runtime/src/lv2/rsx.cpp's walker already decodes,
 * and the method semantics are RPCS3's:
 *   Emu/RSX/NV47/HW/nv4097.cpp:784  texture read semaphore release
 *   Emu/RSX/NV47/HW/nv4097.cpp:817  back-end write semaphore release
 *   Emu/RSX/NV47/HW/nv406e.cpp:113  NV406E semaphore release
 *   Emu/RSX/NV47/HW/nv406e.cpp:24   NV406E set reference
 *   Emu/RSX/NV47/HW/nv47_sync.hpp:14 write_gcm_label writes FOUR bytes
 *   Emu/RSX/Utils/rsx_utils.cpp:224  the DMA context -> address decode
 * Every one of those writes is four bytes. Writing a wider block would overwrite
 * the neighbouring labels the title also waits on (measured discrepancy raised by
 * piste B, refs checked here).
 */

namespace lv2 {
/* The io table's forward translation, defined in runtime/src/lv2/rsx.cpp. Using
 * the authority rather than a second table is what keeps the release addresses
 * and the guest's label addresses the same object. */
uint32_t rsx_io_to_ea(uint32_t io);
}  // namespace lv2

namespace gpu {
namespace gcm {

namespace {

/* The title's RSX context, recorded by _cellGcmInitBody. Layout is the runtime's
 * own (lv2/rsx.cpp sys_rsx_context_allocate): device/control block at the base,
 * driver info one MB up, reports (and therefore the label window) two MB up. */
uint32_t g_ctx = 0;          /* = dma_control = device_addr */
uint32_t g_fifo = 0;         /* ring base, io address = the EAs the guest built */
uint32_t g_fifo_size = 0;

constexpr uint32_t kControlOffset = 0x40;  /* CellGcmControl inside the context */
constexpr uint32_t kReportsOffset = 0x200000;
constexpr uint32_t kRingStart = 0x1000;  /* 4 KB reserved at the ring's start (RPCS3) */
constexpr uint32_t kIoPages = 256;      /* io window is 256 MB, 1 MB pages */

/* Program state the command processor keeps. Nothing here draws; the values are
 * the ones the release paths need. */
struct Regs {
    /* The DMA context a semaphore lives in, defaulted to "local report" rather
     * than to zero. The title's libgcm writes the semaphore OFFSET (0x1D6C) and
     * the release value (0x1D70) but never the context register 0x1A4, so a
     * zero default would decode the label offset into local video memory and
     * write there while the guest reads the label window -- a silent mismatch.
     * CELL_GCM_CONTEXT_DMA_SEMAPHORE_R (refs GCM.h / gcm_enums.h:525) is what
     * libgcm uses for these labels, and it decodes to the reports window, which
     * is exactly the base cellGcmGetLabelAddress returns. */
    uint32_t sema_ctx_406e = 0x66616661u;
    uint32_t sema_off_406e = 0;
    uint32_t sema_ctx_4097 = 0x66616661u;
    uint32_t sema_off_4097 = 0;
    uint32_t last_reference = 0;
    bool call_active = false;
    uint32_t call_return = 0;
};
Regs g_regs;

std::atomic<uint64_t> g_drains{0}, g_methods{0}, g_draws{0}, g_clears{0};
std::atomic<uint64_t> g_releases{0}, g_references{0}, g_flips{0}, g_packets{0};
/* GCM_FLIP_HEAD (0xE920/0xE924) is which head is next, NOT a flip (gcm_enums.h:826). */
std::atomic<uint64_t> g_flip_heads{0};
/* Z-DUMP: distribution of the get pointer, to see whether the reader revisits a
 * small set of offsets (a sub-routine cycle) or walks the whole ring. */
std::atomic<uint32_t> g_get_hist[65536]{};
/* The decisive measurement for Z1: WHICH method numbers does this decoder actually
 * see? The native flips at 60 Hz on GCM_FLIP_COMMAND (0xFEAC >> 2). If that number
 * never appears here, the decoder is reading a different stream than the title
 * writes, and no amount of counting will produce a flip. */
std::atomic<uint64_t> g_method_hist[2048]{};
/* Self-jumps are the guest's idle marker, NOT the end of the ring (track X4). */
std::atomic<uint64_t> g_idle_selfjumps{0};
std::atomic<uint32_t> g_sema_timeouts{0};
std::atomic<uint32_t> g_unmapped_stops{0};
std::atomic<bool> g_logged_first_release{false};
/* H4: where in the GUEST's own ring the method being executed was found, so a
 * record the page consumes can name the packet that produced it instead of a
 * position this runtime invented. drain() sets it before each packet's args are
 * dispatched; execute() forwards it. */
uint32_t g_cur_io = 0;

inline uint32_t label_base() { return g_ctx ? g_ctx + kReportsOffset : 0; }

/* Same decode as runtime/src/lv2/rsx.cpp get_address(), with the base addresses
 * this module recorded. Only the contexts a semaphore can name are needed. */
uint32_t decode(uint32_t offset, uint32_t loc) {
    switch (loc) {
    case 0:
    case 0xFEED0000: return vm::region_bases().video + offset;
    case 1:
    case 0xFEED0001: return lv2::rsx_io_to_ea(offset);
    case 0x66626660: return g_ctx + kReportsOffset + 0x1400 + offset;
    case 0x66606660:
    case 0x66616661: return g_ctx + kReportsOffset + offset;
    case 0x56616660:
    case 0x56616661: return g_ctx + offset;
    case 0xBAD68000: return lv2::rsx_io_to_ea(0x0E000000 + offset);
    default:
        LOG_WARN("gcmfifo", "unknown DMA context 0x%08x (offset 0x%x)", loc, offset);
        return 0;
    }
}

/* ea -> io, the inverse of what cellGcmAddressToOffset must answer. The io table
 * lives in lv2/rsx.cpp and is private there, so this rebuilds the inverse by
 * asking the forward translation about every page: 4096 calls, done lazily and
 * only again after a miss. */
struct Reverse {
    uint32_t ea_page[kIoPages] = {};
    bool built = false;
};
Reverse g_rev;

/* The io table's inverse, 256 pages (the whole 256 MB io window). lv2/rsx.cpp
 * keeps the forward direction private, and duplicating the table there would let
 * the two disagree -- the bug class the rsx_iomap.h comment already records. */
void build_reverse() {
    for (uint32_t p = 0; p < kIoPages; p++) g_rev.ea_page[p] = lv2::rsx_io_to_ea(p << 20) & 0xFFF00000u;
    g_rev.built = true;
}

int32_t ea_to_io_impl(uint32_t ea, uint32_t* io) {
    if (!io || !ea) return -1;
    const uint32_t want = ea & 0xFFF00000u;
    /* Zero is "no mapping" in the io table, so a zero entry must never be
     * matched: doing so reported ea 0x00000000 as io 0x00100000 (measured), which
     * hands the guest a ring offset for a null pointer. */
    if (g_rev.built && (ea >> 20) < kIoPages && want && g_rev.ea_page[ea >> 20] == want) {
        *io = ea;   /* identity: the page's base already matches */
        return 0;
    }
    build_reverse();
    for (uint32_t p = 0; p < kIoPages; p++) {
        if (g_rev.ea_page[p] && g_rev.ea_page[p] == want) {
            *io = (p << 20) | (ea & 0xFFFFF);
            return 0;
        }
    }
    return -1;
}

uint32_t io_to_ea_impl(uint32_t io) { return lv2::rsx_io_to_ea(io); }

uint32_t ctrl_put() { return vm::read32(g_ctx + kControlOffset + 0); }
uint32_t ctrl_get() { return vm::read32(g_ctx + kControlOffset + 4); }
void ctrl_set_get(uint32_t v) { vm::write32(g_ctx + kControlOffset + 4, v); }
void ctrl_set_ref(uint32_t v) { vm::write32(g_ctx + kControlOffset + 8, v); }

/* One release. Four bytes, big-endian, at the address the DMA context names --
 * the width every RPCS3 release path uses. A release whose value is already
 * stored is a no-op, as write_gcm_label does. */
void write_release(uint32_t addr, uint32_t value, const char* what) {
    /* The device semaphore at +0x30 is the flip handshake, and a release of 0
     * there RE-ARMS it to 1 rather than clearing it: runtime/src/lv2/rsx.cpp:375
     * has the same rule ("if (a == device_addr + 0x30 && !arg) arg = 1") because
     * the guest's next acquire expects 1. Measured without it: "semaphore acquire
     * never satisfied: 0x00e00030 == 0x1 (now 0x0)" on every frame, and the
     * 0x1D70-style release below then wrote a literal 0 over the handshake. */
    if (addr == g_ctx + 0x30 && value == 0) value = 1;
    if (!addr || !vm::range_mapped(addr, 4)) {
        static std::atomic<int> n{0};
        if (n.fetch_add(1) < 8)
            LOG_WARN("gcmfifo", "%s: address 0x%08x is not mapped", what, addr);
        return;
    }
    const uint32_t before = vm::read32(addr);
    if (before == value) return;
    vm::write32(addr, value);
    g_releases.fetch_add(1, std::memory_order_relaxed);
    if (!g_logged_first_release.exchange(true))
        LOG_INFO("gcmfifo", "first label release: %s addr=0x%08x 0x%08x -> 0x%08x",
                 what, addr, before, value);
    if (addr == label_base() + 0x10 * 255)
        LOG_INFO("gcmfifo", "label[255] <- 0x%08x (%s)", value, what);
}

/* Methods that carry synchronisation. Everything else is counted, not executed:
 * the drawing methods need a GPU and their effect is exactly what this build
 * omits. */
void execute(uint32_t method, uint32_t arg) {
    /* Record the method number BEFORE dispatching, so the histogram is the stream
     * as decoded, not as classified. */
    g_method_hist[(method >> 2) & 2047].fetch_add(1, std::memory_order_relaxed);
    /* H4: hand EVERY method to the ring producer, before the switch interprets it.
     * The producer classifies and copies payloads; it never writes guest memory.
     * This one line is the whole seam -- the decode above stays the authority. */
    gpu::rsxring::on_method(method, arg, g_cur_io);
    switch (method) {
    case 0x0050: /* NV406E_SET_REFERENCE */
        g_regs.last_reference = arg;
        if (g_ctx) ctrl_set_ref(arg);
        g_references.fetch_add(1, std::memory_order_relaxed);
        return;
    case 0x0060: g_regs.sema_ctx_406e = arg; return;
    case 0x0064: g_regs.sema_off_406e = arg; return;
    case 0x0068: { /* NV406E_SEMAPHORE_ACQUIRE */
        const uint32_t addr = decode(g_regs.sema_off_406e, g_regs.sema_ctx_406e);
        if (!addr || !vm::range_mapped(addr, 4)) return;
        /* A bounded wait: this is the only thread that could release the value,
         * so an unbounded spin here would deadlock the machine. RPCS3 waits with
         * a 1 s driver-recovery timeout (nv406e.cpp:60); the same budget, then it
         * gives up and says so. */
        for (uint32_t spins = 0; spins < 200000; spins++) {
            if (vm::read32(addr) == arg) return;
            if ((spins & 0xFFF) == 0xFFF) {}
        }
        g_sema_timeouts.fetch_add(1, std::memory_order_relaxed);
        static std::atomic<int> n{0};
        if (n.fetch_add(1) < 8)
            LOG_WARN("gcmfifo", "semaphore acquire never satisfied: 0x%08x == 0x%x (now 0x%x)",
                     addr, arg, vm::read32(addr));
        return;
    }
    case 0x006C: { /* NV406E_SEMAPHORE_RELEASE */
        const uint32_t addr = decode(g_regs.sema_off_406e, g_regs.sema_ctx_406e);
        write_release(addr, arg, "406E");
        return;
    }
    case 0x01A4: g_regs.sema_ctx_4097 = arg; return;
    case 0x1D6C: g_regs.sema_off_4097 = arg; return;
    case 0x1D70: { /* NV4097_BACK_END_WRITE_SEMAPHORE_RELEASE */
        const uint32_t addr = decode(g_regs.sema_off_4097, g_regs.sema_ctx_4097);
        /* The hardware byte-swaps the label value; libgcm's setWriteBackEndLabel
         * pre-swaps to cancel it, so the stored word is this permutation of the
         * FIFO word (nv4097.cpp:835). */
        const uint32_t val = (arg & 0xff00ff00u) | ((arg & 0xffu) << 16) | ((arg >> 16) & 0xffu);
        write_release(addr, val, "back-end");
        return;
    }
    case 0x1D74: { /* NV4097_TEXTURE_READ_SEMAPHORE_RELEASE */
        const uint32_t addr = decode(g_regs.sema_off_4097, g_regs.sema_ctx_4097);
        write_release(addr, arg, "texture-read");
        return;
    }
    case 0x1808:   /* BEGIN_END */
    case 0x1814:   /* DRAW_ARRAYS */
    case 0x1824:   /* DRAW_INDEX_ARRAY */
    case 0x1818:   /* INLINE_ARRAY */
        g_draws.fetch_add(1, std::memory_order_relaxed);
        return;
    case 0x1D94: g_clears.fetch_add(1, std::memory_order_relaxed); return;
    /* refs/rpcs3/rpcs3/Emu/RSX/gcm_enums.h:826-829:
     *   GCM_FLIP_HEAD    = 0x0000E920 >> 2   // 0xE920:0xE924, flip head 0 or 1
     *   GCM_FLIP_COMMAND = 0x0000FEAC >> 2   // <- THIS is the flip, bound to
     *                                         //    flip_command in rsx_methods.cpp:1773
     * Counting 0xE920/0xE924 counted FLIP_HEAD (which head is next), not the flip
     * itself. That is why flips stayed 0 while the stream was full of activity. */
    case 0xFEAC:
        g_flips.fetch_add(1, std::memory_order_relaxed);
        g_method_hist[(0xFEAC >> 2) & 2047].fetch_add(1, std::memory_order_relaxed);
        return;
    case 0xE920:
    case 0xE924: g_flip_heads.fetch_add(1, std::memory_order_relaxed); return;
    default: return;
    }
}

}  // namespace

/* Periodic report, so the counters reach the log even when the harness kills the
 * run at a timeout -- which it always does, because the guest never exits by
 * itself and there is no display to close. */
void report() {
    static uint64_t last = 0;
    const uint64_t n = g_methods.load(std::memory_order_relaxed);
    /* The title submits a handful of methods per frame, not per millisecond: a
     * 50 000 threshold was measured to stay silent for a whole 70 s run, so the
     * report is keyed to the packet count instead. */
    if (n - last < 2000) return;
    last = n;
    LOG_INFO("gcmfifo", "methods=%llu packets=%llu draws=%llu clears=%llu releases=%llu references=%llu flips=%llu heads=%llu drains=%llu label255=0x%08x",
             (unsigned long long)n, (unsigned long long)g_packets.load(),
             (unsigned long long)g_draws.load(), (unsigned long long)g_clears.load(),
             (unsigned long long)g_releases.load(), (unsigned long long)g_references.load(),
             (unsigned long long)g_flips.load(), (unsigned long long)g_flip_heads.load(), (unsigned long long)g_drains.load(),
                      label_base() ? vm::read32(label_base() + 0x10 * 255) : 0);
    /* Once, at the 40th report, print which method numbers the decoder actually saw. */
    static std::atomic<int> once{0};
    if (once.fetch_add(1, std::memory_order_relaxed) == 40) dump_method_hist();
}

void init(uint32_t context_ea, uint32_t fifo_ea, uint32_t fifo_size) {
    g_ctx = context_ea;
    g_fifo = fifo_ea;
    g_fifo_size = fifo_size;
    /* Both pointers start at the first byte AFTER the reserved 4 KB, not at 0.
     *
     * RPCS3 does exactly this: the default command buffer begins at io 0x1000
     * (cellGcmSys.cpp:479 "4 kb reserved at the beginning"), the guest's own
     * ring builder writes its first packet at buf+0x1000, and the fresh control
     * block reads put == get == 0x1000 (cellGcmSys.cpp:1331 checks for it).
     *
     * Leaving get at 0 was measured to be fatal: put was 0x00001030 while get
     * was 0, so the reader started at the ring base -- 4 KB of zeros the guest
     * never wrote -- and reported "zero packet header at get 0x00000000". */
    vm::write32(g_ctx + kControlOffset + 0, kRingStart);
    vm::write32(g_ctx + kControlOffset + 4, kRingStart);
    vm::write32(g_ctx + kControlOffset + 8, 0);
    /* The flip semaphore, which the title acquires before every flip. RPCS3 sets
     * it to 1 when the context is allocated (lv2/sys_rsx.cpp: sys_rsx_context_
     * allocate -> "if (device_addr) write32(device_addr + 0x30, 1)"), and the
     * release path re-arms it with 1 or 0 (lv2/rsx.cpp:375 "if (a == device_addr
     * + 0x30 && !arg) arg = 1"). Leaving it at 0 makes the acquire wait for a
     * value the release never produces. Measured: "semaphore acquire never
     * satisfied: 0x00e00030 == 0x1 (now 0x0)". */
    vm::write32(g_ctx + 0x30, 1);
    /* H4: bind the ring producer's hooks to this runtime now that the io table and
     * the context exist. Idempotent; the producer keeps no guest state itself. */
    gpu::rsxring::install_runtime_hooks();
    LOG_INFO("gcmfifo", "reader armed: context=0x%08x control=0x%08x ring=0x%08x size=0x%x put=get=0x%x",
             g_ctx, g_ctx + kControlOffset, g_fifo, g_fifo_size, kRingStart);
}

bool inited() { return g_ctx != 0; }

uint32_t control_address() { return g_ctx ? g_ctx + kControlOffset : 0; }

uint32_t label_address() { return label_base(); }

uint32_t label_address(uint32_t index) { return label_base() + 0x10 * index; }

/* Public aliases. The translation helpers live in the anonymous namespace so
 * drain() can call them without a second, shadowing declaration; these two are
 * what the HLE module uses for cellGcmAddressToOffset / cellGcmIoOffsetToAddress. */
int32_t ea_to_io(uint32_t ea, uint32_t* io) { return ea_to_io_impl(ea, io); }

uint32_t io_to_ea(uint32_t io) { return io_to_ea_impl(io); }

void drain() {
    if (!g_ctx) return;
    g_drains.fetch_add(1, std::memory_order_relaxed);
    {
        /* FIRST-DRAIN PROBE. The guest's ring base is the one thing that must be
         * read rather than assumed: put and get are io offsets the guest itself
         * computes, and a mismatch means the ring we think we own is not the ring
         * it writes. Print the CellGcmContextData the guest holds (begin/end/
         * current/callback) together with the control block and the first words
         * of the ring, once, so the mapping is measured. */
        static std::atomic<int> once{0};
        if (once.fetch_add(1) == 0) {
            LOG_INFO("gcmfifo", "probe: put=0x%08x get=0x%08x ctrl=0x%08x fifo=0x%08x",
                     ctrl_put(), ctrl_get(), g_ctx + kControlOffset, g_fifo);
            LOG_INFO("gcmfifo", "probe: ring[0..7]=%08x %08x %08x %08x %08x %08x %08x %08x",
                     vm::read32(g_fifo + 0), vm::read32(g_fifo + 4), vm::read32(g_fifo + 8),
                     vm::read32(g_fifo + 12), vm::read32(g_fifo + 16), vm::read32(g_fifo + 20),
                     vm::read32(g_fifo + 24), vm::read32(g_fifo + 28));
            LOG_INFO("gcmfifo", "probe: ring+0x1000=%08x %08x %08x %08x",
                     vm::read32(g_fifo + 0x1000), vm::read32(g_fifo + 0x1004),
                     vm::read32(g_fifo + 0x1008), vm::read32(g_fifo + 0x100c));
            LOG_INFO("gcmfifo", "probe: ctx=0x%08x mapped=%d labels=0x%08x mapped=%d label255 mapped=%d",
                     g_ctx, vm::range_mapped(g_ctx, 0x300000) ? 1 : 0, label_base(),
                     vm::is_mapped(label_base()) ? 1 : 0, vm::is_mapped(label_base() + 0xFF0) ? 1 : 0);
            LOG_INFO("gcmfifo", "probe: ctx@0xd72800=%08x %08x %08x %08x",
                     vm::read32(0xd72800u), vm::read32(0xd72804u),
                     vm::read32(0xd72808u), vm::read32(0xd7280cu));
            /* Which io pages does the runtime actually translate? This is the
             * measurement that decides whether put/get are in the window the
             * guest thinks it has: the guest computes them as io offsets, so a
             * page that is missing here means the reader and the writer disagree
             * about where the ring lives. */
            {
                char line[512] = {};
                int used = 0;
                int shown = 0;
                for (uint32_t p = 0; p < 0x1000 && shown < 8; p++) {
                    const uint32_t ea = lv2::rsx_io_to_ea(p << 20);
                    if (!ea) continue;
                    used += std::snprintf(line + used, sizeof line - used, " io%03x->ea %08x", p, ea);
                    shown++;
                }
                LOG_INFO("gcmfifo", "probe: io pages mapped:%s", used ? line : " (none)");
            }
        }
    }
    uint32_t guard = 0;
    for (;;) {
        const uint32_t put = ctrl_put();
        uint32_t get = ctrl_get();
        {
            /* Z-DUMP: the raw put/get/ref at the first 10 drains, as read. */
            static std::atomic<int> cn{0};
            const int k = cn.fetch_add(1, std::memory_order_relaxed);
            if (k < 10)
                LOG_INFO("ctrl10", "n=%d put=0x%08x get=0x%08x ref=0x%08x raw@ctrl=%08x %08x %08x",
                         k, put, get, vm::read32(g_ctx + kControlOffset + 8),
                         vm::read32(g_ctx + kControlOffset + 0),
                         vm::read32(g_ctx + kControlOffset + 4),
                         vm::read32(g_ctx + kControlOffset + 8));
        }
        /* The command processor OWNS get; the guest never writes it. A get below
         * the ring's start therefore means the block was just (re)initialised to
         * zero, and reading there is reading 4 KB of zeros the guest never wrote
         * (measured: "zero packet header at get 0x00000000"). Recover to the ring
         * start rather than interpret it. */
        if (get < kRingStart && put >= kRingStart) {
            get = kRingStart;
            ctrl_set_get(get);
        }
        if (get == put) {
            /* H4: the walk ended, so the last state batch is flushed here rather
             * than waiting for the next drain -- the page must not see state one
             * frame late. */
            gpu::rsxring::on_drain_end();
            return;
        }
        const uint32_t ea = io_to_ea_impl(get);
        if (!ea) {
            static std::atomic<int> n{0};
            if (n.fetch_add(1) < 8)
                LOG_WARN("gcmfifo", "ring get 0x%08x is not io-mapped (put 0x%08x)", get, put);
            g_unmapped_stops.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (++guard > 2000000) {
            /* A closed call/ret cycle: get never reaches put. Log the shape once so
             * the decoder bug is visible instead of inferred (track X4 measured 48
             * stalls, get returning to 0x1190/0x1454/0x15e4). */
            static std::atomic<int> n{0};
            if (n.fetch_add(1) < 6)
                LOG_WARN("gcmfifo", "[loop] budget at get 0x%08x (put 0x%08x) call_active=%d "
                         "call_return 0x%08x cmd 0x%08x",
                         get, put, g_regs.call_active ? 1 : 0, g_regs.call_return,
                         vm::read32(ea));
            return;
        }
        const uint32_t cmd = vm::read32(ea);
        g_cur_io = get;   /* H4: the packet's own io offset, for the record */

        {
            /* Z-DUMP: histogram of the 20 most frequent get offsets over the run. */
            static std::atomic<int> gn{0};
            if (gn.fetch_add(1, std::memory_order_relaxed) < 200000)
                g_get_hist[(get >> 2) & 65535].fetch_add(1, std::memory_order_relaxed);
        }
        if (cmd == 0 && get != put) {
            /* A zero header cannot be a packet: stop rather than interpret it,
             * and say where, because that is a producer bug. */
            static std::atomic<int> n{0};
            if (n.fetch_add(1) < 8)
                LOG_WARN("gcmfifo", "zero packet header at get 0x%08x (ea 0x%08x, put 0x%08x) -- stopping",
                         get, ea, put);
            return;
        }
        g_packets.fetch_add(1, std::memory_order_relaxed);
        /* Jump / call / return, decoded as runtime/src/lv2/rsx.cpp:560-600 does. */
        if ((cmd & 0xE0000003u) == 0x20000000u || (cmd & 3u) == 1u) {
            const uint32_t target = (cmd & 3u) == 1u ? (cmd & 0xFFFFFFFCu) : (cmd & 0x1FFFFFFCu);
            if (target == get) {
                /* A self-jump is the guest's idle marker, NOT the end of the ring:
                 * the reference (runtime/src/lv2/rsx.cpp:566-570) waits and
                 * CONTINUES, so work queued after it is still consumed. Returning
                 * here left put permanently ahead of get -- measured 48 times per
                 * run, get stuck at 0x1190/0x1454/0x15e4 while put grew 0x1218 per
                 * cycle. That is why nothing after the marker was ever decoded. */
                g_idle_selfjumps.fetch_add(1, std::memory_order_relaxed);
                return;   /* the caller re-drains on the next put change */
            }
            ctrl_set_get(target);
            continue;
        }
        if ((cmd & 3u) == 2u) {
            g_regs.call_active = true;
            g_regs.call_return = get + 4;
            ctrl_set_get(cmd & 0x1FFFFFFCu);
            continue;
        }
        if ((cmd & 0xFFFF0003u) == 0x00020000u) {
            if (!g_regs.call_active) {
                ctrl_set_get(get + 4);
                continue;
            }
            g_regs.call_active = false;
            ctrl_set_get(g_regs.call_return);
            continue;
        }
        const uint32_t count = (cmd >> 18) & 0x7FFu;
        const uint32_t method0 = cmd & 0xFFFCu;
        const bool noninc = (cmd & 0xE0030003u) == 0x40000000u;
        {
            /* Z-DUMP: the first 200 headers exactly as the loop will consume them. */
            static std::atomic<int> hn{0};
            const int k = hn.fetch_add(1, std::memory_order_relaxed);
            if (k < 200) {
                uint32_t a0 = 0, a1 = 0, a2 = 0;
                if (count > 0) a0 = vm::read32(ea + 4);
                if (count > 1) a1 = vm::read32(ea + 8);
                if (count > 2) a2 = vm::read32(ea + 12);
                LOG_INFO("hdr200",
                         "get=0x%08x raw=0x%08x reg=0x%04x count=%u args=%08x %08x %08x",
                         get, cmd, method0, count, a0, a1, a2);
            }
        }
        for (uint32_t i = 0; i < count; i++) {
            const uint32_t reg = noninc ? method0 : (method0 + i * 4u);
            const uint32_t arg = vm::read32(ea + 4 + i * 4);
            execute(reg, arg);
        }
        g_methods.fetch_add(count, std::memory_order_relaxed);
        report();
        const uint32_t next = get + 4 + count * 4;
        if (next == get) return;
        ctrl_set_get(next);
    }
}

/* Top method numbers seen, printed once at the end of a run: the measurement Z1 asks
 * for -- does GCM_FLIP_COMMAND (0xFEAC >> 2 = 14955) appear in this stream at all? */
void dump_method_hist() {
    std::vector<std::pair<uint64_t, uint32_t>> v;
    for (uint32_t i = 0; i < 2048; i++) {
        const uint64_t n = g_method_hist[i].load(std::memory_order_relaxed);
        if (n) v.push_back({n, i});
    }
    std::sort(v.rbegin(), v.rend());
    char line[512] = {0};
    size_t pos = 0;
    for (size_t i = 0; i < v.size() && i < 24 && pos < sizeof(line) - 24; i++) {
        pos += std::snprintf(line + pos, sizeof(line) - pos, "0x%04x:%llu ",
                             v[i].second << 2, static_cast<unsigned long long>(v[i].first));
    }
    LOG_INFO("gcmfifo", "[methods] distinct=%zu top: %s", v.size(), line);
    {
        /* Z-DUMP: the 20 most frequent get offsets over the whole run. */
        std::vector<std::pair<uint32_t, uint32_t>> g;
        for (uint32_t i = 0; i < 65536; i++) {
            const uint32_t n2 = g_get_hist[i].load(std::memory_order_relaxed);
            if (n2) g.push_back({n2, i << 2});
        }
        std::sort(g.rbegin(), g.rend());
        char gl[512] = {0};
        size_t gp = 0;
        for (size_t i = 0; i < g.size() && i < 20 && gp < sizeof(gl) - 24; i++)
            gp += std::snprintf(gl + gp, sizeof(gl) - gp, "0x%08x:%u ",
                                g[i].second, g[i].first);
        LOG_INFO("gcmfifo", "[gethist] distinct=%zu top20: %s", g.size(), gl);
        LOG_INFO("gcmfifo", "[ctrl] first reads: put=0x%08x get=0x%08x ref=0x%08x",
                 vm::read32(g_ctx + kControlOffset + 0), vm::read32(g_ctx + kControlOffset + 4),
                 vm::read32(g_ctx + kControlOffset + 8));
    }
    LOG_INFO("gcmfifo", "[methods] FLIP_COMMAND 0xFEAC seen: %llu   FLIP_HEAD 0xE920: %llu",
             static_cast<unsigned long long>(g_method_hist[(0xFEAC >> 2) & 2047].load()),
             static_cast<unsigned long long>(g_method_hist[(0xE920 >> 2) & 2047].load()));
}

void stats(Stats* out) {
    if (!out) return;
    out->drains = g_drains.load(std::memory_order_relaxed);
    out->methods = g_methods.load(std::memory_order_relaxed);
    out->draws = g_draws.load(std::memory_order_relaxed);
    out->clears = g_clears.load(std::memory_order_relaxed);
    out->releases = g_releases.load(std::memory_order_relaxed);
    out->references = g_references.load(std::memory_order_relaxed);
    out->flips = g_flips.load(std::memory_order_relaxed);
    out->packets = g_packets.load(std::memory_order_relaxed);
    out->label255 = label_base() ? vm::read32(label_base() + 0x10 * 255) : 0;
}

}  // namespace gcm
}  // namespace gpu