/* The DMA list walker, tested: PUTL/PUTLB/PUTLF and GETL/GETLB/GETLF.
 *
 * The specification is RPCS3's do_list_transfer (refs/rpcs3/rpcs3/Emu/Cell/
 * SPUThread.cpp:2927). Every check below is named after the line it enforces,
 * and drives spu/dma_list.h -- the same header SpuThread::run_list calls -- so a
 * defect cannot be fixed in the runtime and left here, or fixed here and left in
 * the runtime.
 *
 * The test that matters is the last one: a list whose SECOND element is only
 * filled AFTER the stall event. A walker that reads elements ahead of time, or
 * that ignores stall-and-notify, passes every other test and fails that one.
 * That is the failure mode that matters, because the SPURS frame allocator
 * builds its list in local storage and fills the next element from the value the
 * previous transfer returned.
 */
#include "spu/dma_list.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

static int failures = 0;
static int checks = 0;

static void check(bool ok, const char* what) {
    checks++;
    if (!ok) {
        failures++;
        std::printf("FAIL: %s\n", what);
    }
}

namespace {

constexpr uint32_t LS_SIZE = 0x40000;

struct Ls {
    std::unique_ptr<uint8_t[]> buf{new uint8_t[LS_SIZE]()};
    uint8_t* get() { return buf.get(); }
};

/* Write one 8-byte list element in the guest's big-endian layout:
 * word0 = notify<<31 | size, word1 = eal. */
void put_elem(uint8_t* ls, uint32_t at, bool notify, uint32_t size, uint32_t eal) {
    const uint32_t w0 = (notify ? 0x80000000u : 0u) | (size & 0x7FFFu);
    ls[at + 0] = uint8_t(w0 >> 24);
    ls[at + 1] = uint8_t(w0 >> 16);
    ls[at + 2] = uint8_t(w0 >> 8);
    ls[at + 3] = uint8_t(w0);
    ls[at + 4] = uint8_t(eal >> 24);
    ls[at + 5] = uint8_t(eal >> 16);
    ls[at + 6] = uint8_t(eal >> 8);
    ls[at + 7] = uint8_t(eal);
}

/* One transfer the walker asked for, in the order it asked. */
struct Xfer {
    uint32_t lsa, eal, size;
};

/* Walk a list to exhaustion, recording the transfers. Returns the transfer count
 * and how many times the walk stopped on a stall-and-notify element. */
struct Walk {
    std::vector<Xfer> xfers;
    int stalls = 0;
    spu::ListStallTable* table = nullptr;

    void run(uint8_t* ls, uint32_t tag, uint32_t cmd, spu::ListCursor c) {
        spu::ListElement e;
        while (spu::list_next(ls, c, e)) {
            xfers.push_back({spu::list_transfer_lsa(c, e), e.eal, e.size});
            c.lsa = spu::list_advance_lsa(c.lsa, e.size);
            if (spu::list_should_stall(e, c)) {
                stalls++;
                if (table) table->stall(tag, cmd, c);
                return;
            }
        }
    }

    uint32_t bytes() const {
        uint32_t n = 0;
        for (const auto& x : xfers) n += x.size;
        return n;
    }
};

} // namespace

int main() {
    /* ======================================================================
     * The element layout. RPCS3:2939 struct alignas(8) list_element: one byte
     * of stall bit + padding, a big-endian 16-bit ts, a big-endian 32-bit ea.
     * RPCS3:2970 masks ts with 0x7fff, so bit 15 of ts is not part of the size.
     * ==================================================================== */
    {
        Ls l;
        put_elem(l.get(), 0x1000, true, 0x1234, 0xDEADBEEF);
        spu::ListElement e;
        spu::ListCursor c = spu::list_start(0, 0x1000, 8);
        check(spu::list_next(l.get(), c, e), "a single 8-byte element is a list");
        check(e.notify, "bit 31 of word0 is the stall-and-notify bit");
        check(e.size == 0x1234, "size is the low 15 bits of word0");
        check(e.eal == 0xDEADBEEF, "eal is word1, big-endian");
        check(c.remaining == 0, "the cursor consumes 8 bytes");
        check(!spu::list_next(l.get(), c, e), "an exhausted list yields no element");

        /* A size with bit 15 set: RPCS3 masks it off (2970). Treating the whole
         * 16 bits as the size asks the MFC for 0x9234 bytes, three times the
         * element's real length, and every following element's local address
         * lands in the wrong place. */
        Ls l2;
        put_elem(l2.get(), 0x1000, false, 0x9234, 0x1000);
        spu::ListCursor c2 = spu::list_start(0, 0x1000, 8);
        spu::ListElement e2;
        spu::list_next(l2.get(), c2, e2);
        check(e2.size == 0x1234, "bit 15 of the size field is masked off (2970)");

        /* The notify bit is the HIGH bit of word0, not of ts. */
        Ls l3;
        put_elem(l3.get(), 0x1000, false, 0x7000, 0);
        spu::ListCursor c3 = spu::list_start(0, 0x1000, 8);
        spu::ListElement e3;
        spu::list_next(l3.get(), c3, e3);
        check(!e3.notify, "a large size does not look like a notify bit");
    }

    /* ======================================================================
     * The command's fields seed the walk. RPCS3:2957 item_ptr = eal & 0x3fff8,
     * RPCS3:2958 arg_lsa = lsa & 0x3fff0. A list at an unaligned address is
     * rounded DOWN; reading from the unrounded address decodes a mixture of
     * two elements and produces a nonsense external address.
     * ==================================================================== */
    {
        spu::ListCursor c = spu::list_start(0x12347, 0x2ABC6, 0x20);
        check(c.lsa == 0x12340, "list lsa is masked to 16 bytes (2958)");
        check(c.list == 0x2ABC0, "list address is masked to 8 bytes (2957)");
        check(c.remaining == 0x20, "the remaining count is the command's size");

        /* The local address carries the low nibble of the external address:
         * the MFC copies an unaligned tail there. RPCS3:3390 lsa = arg_lsa |
         * (addr & 0xf). */
        spu::ListElement e;
        e.eal = 0x1000123D;
        e.size = 16;
        spu::ListCursor c2 = spu::list_start(0x800, 0, 8);
        check(spu::list_transfer_lsa(c2, e) == 0x80D, "the eal's low nibble lands in the lsa");
        check(spu::list_advance_lsa(c2.lsa, e.size) == 0x810, "the lsa advances by the size rounded to 16");
        check(spu::list_advance_lsa(0x800, 1) == 0x810, "a 1-byte transfer still advances 16");
        check(spu::list_advance_lsa(0x800, 17) == 0x820, "a 17-byte transfer advances 32");
        check(spu::list_advance_lsa(0x3FFF0, 16) == 0, "the lsa wraps inside local storage");
    }

    /* ======================================================================
     * A list of three elements: the local addresses are cumulative, the
     * external addresses independent, and nothing stalls.
     * ==================================================================== */
    {
        Ls l;
        put_elem(l.get(), 0x100, false, 0x80, 0x00100000);
        put_elem(l.get(), 0x108, false, 0x40, 0x00200000);
        put_elem(l.get(), 0x110, false, 0x10, 0x00300000);
        Walk w;
        w.run(l.get(), 1, 0x44, spu::list_start(0x2000, 0x100, 24));
        check(w.xfers.size() == 3, "three elements are three transfers");
        check(w.stalls == 0, "a list without a notify bit does not stall");
        check(w.xfers[0].lsa == 0x2000 && w.xfers[0].eal == 0x00100000 && w.xfers[0].size == 0x80, "element 0 transfer");
        check(w.xfers[1].lsa == 0x2080 && w.xfers[1].eal == 0x00200000 && w.xfers[1].size == 0x40, "element 1 lsa is cumulative");
        check(w.xfers[2].lsa == 0x20C0 && w.xfers[2].eal == 0x00300000 && w.xfers[2].size == 0x10, "element 2 lsa is cumulative");
        check(w.bytes() == 0xD0, "all bytes transferred");
    }

    /* ======================================================================
     * The pause itself. RPCS3:3460 raises MFC_LIST_STALL_NOTIFY_EVENT (0x2)
     * and stops the command at the element carrying the bit; RPCS3:3455 breaks
     * out BEFORE the notify test when the list is exhausted, so a notify bit on
     * the LAST element does not stall. Stalling there leaves the tag permanently
     * fenced: the guest waits for a resume it has already performed.
     * ==================================================================== */
    {
        Ls l;
        put_elem(l.get(), 0x0, false, 0x10, 0x1000);
        put_elem(l.get(), 0x8, true, 0x20, 0x2000);  /* notify here */
        put_elem(l.get(), 0x10, false, 0x30, 0x3000);
        Walk w;
        w.run(l.get(), 3, 0x44, spu::list_start(0x0, 0x0, 24));
        check(w.xfers.size() == 2, "the walk stops after the notify element (3460)");
        check(w.stalls == 1, "one stall-and-notify event (EVENT_SN = 0x2)");
        check(w.xfers[1].size == 0x20, "the notify element's own transfer still happens");

        /* Notify on the final element: no stall. */
        Ls l2;
        put_elem(l2.get(), 0x0, false, 0x10, 0x1000);
        put_elem(l2.get(), 0x8, true, 0x20, 0x2000);
        Walk w2;
        w2.run(l2.get(), 3, 0x44, spu::list_start(0x0, 0x0, 16));
        check(w2.xfers.size() == 2, "a notify bit on the last element still transfers");
        check(w2.stalls == 0, "a notify bit on the last element does not stall (3451-3455)");

        /* A size of zero is legal and consumes its element. */
        Ls l3;
        put_elem(l3.get(), 0x0, false, 0, 0x1000);
        put_elem(l3.get(), 0x8, false, 0x10, 0x2000);
        Walk w3;
        w3.run(l3.get(), 3, 0x44, spu::list_start(0x0, 0x0, 16));
        check(w3.xfers.size() == 2, "a zero-size element is still an element");
        check(w3.xfers[1].lsa == 0x0, "a zero-size element does not advance the lsa");
    }

    /* ======================================================================
     * The list maximum. 0x4000 bytes of list is 2048 elements of 8 bytes, and
     * RPCS3 refuses a list command whose size exceeds 0x4000 (SPUThread.cpp:5041
     * "if (ch_mfc_cmd.size <= 0x4000)"). Walking further reads local storage
     * the guest never intended to be a list.
     * ==================================================================== */
    {
        Ls l;
        for (uint32_t i = 0; i < 2048; i++) put_elem(l.get(), i * 8, false, 8, 0x10000 + i * 8);
        Walk w;
        w.run(l.get(), 1, 0x44, spu::list_start(0, 0, 0x4000));
        check(w.xfers.size() == 2048, "0x4000 bytes is exactly 2048 elements");
        /* 2047 transfers of 8 bytes, each advancing 16, plus the final eal's low
         * nibble (0x10000 + 2047*8 = 0x13FF8, nibble 8). */
        check(w.xfers.back().lsa == 2047 * 16 + 8, "the last element's lsa is 2047 transfers of 16 bytes in, plus the eal nibble");
    }

    /* ======================================================================
     * Queue and stall-status accounting (SPUThread.cpp:5529, 5463).
     *
     * RPCS3 keeps ONE mfc_queue entry for a list command across every stall it
     * takes, and RCHCNT(MFC_Cmd) is 16 - mfc_size. A walker that enqueues on
     * every stall runs the queue to 16 and the SPURS kernel, which reads that
     * count before issuing its next command, stops issuing.
     * ==================================================================== */
    {
        spu::ListStallTable t;
        t.reset();
        check(t.count() == 16, "an idle queue reports 16 free slots");
        spu::ListCursor c;
        c.lsa = 0x400;
        c.list = 0x800;
        c.remaining = 0x18;
        t.stall(1, 0x44, c);
        check(t.count() == 15, "a stalled list occupies one queue slot");
        check(spu::list_stall_bit(1) == 0x2, "tag 1's stall status bit is 0x2");

        uint32_t cmd = 0;
        spu::ListCursor got;
        check(t.take(1, cmd, got), "the stalled tag resumes");
        check(cmd == 0x44, "the resume carries the original command");
        check(got.list == 0x800 && got.remaining == 0x18, "the resume carries the cursor");
        check(t.count() == 15, "resuming does not free the queue slot (5463)");

        /* Stall again on a later element of the same list: still one slot. */
        got.list = 0x808;
        got.remaining = 0x10;
        t.stall(1, 0x44, got);
        check(t.count() == 15, "a second stall of the same list reuses its slot");
        check(t.take(1, cmd, got), "the second stall resumes too");
        check(!t.take(1, cmd, got), "a tag with nothing stalled cannot resume");

        t.complete(1);
        check(t.count() == 16, "the finished list frees its slot");
        t.complete(1);
        check(t.count() == 16, "completing twice does not free a slot twice");

        /* Two lists on different tags occupy two slots. */
        t.stall(2, 0x44, c);
        t.stall(5, 0x24, c);
        check(t.count() == 14, "two stalled lists occupy two slots");
        check(spu::list_stall_bit(5) == 0x20, "tag 5's stall status bit is 0x20");
    }

    /* ======================================================================
     * THE TEST. A list whose second element is only filled AFTER the stall
     * event.
     *
     * This is how the guest uses the feature: element 0's transfer result is
     * read, the notify bit on element 0 stops the list, the guest builds element
     * 1 from what it just received, writes it into local storage, and
     * acknowledges. The following elements must be read from local storage AT
     * THAT MOMENT (RPCS3:2957 reads item_ptr on every refill, and
     * MFC_WrListStallAck re-enters with args.eal pointing at the next element).
     *
     * A walker that decodes the whole list up front -- or that caches the
     * elements it read while walking -- sends element 1 to whatever was in local
     * storage at the time of the first read. In the guest's real use that is
     * zero: the frame allocator's buffer is unused local storage, so every
     * element after the first stall has eal 0 and size 0, and the MFC is asked
     * to fetch from guest address 0. That is the observed fault:
     *
     *   DMA GET cmd 0x40 unmapped ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428
     *
     * with the frame allocator's bump pointer at LS 0x19e90 holding 0x23800
     * inside a region that was never written.
     * ==================================================================== */
    {
        Ls l;
        /* Element 0: 0x80 bytes to 0x00100000, WITH the notify bit.
         * Element 1: deliberately all zero -- this is what a guest's scratch
         *            buffer holds before it is filled. */
        put_elem(l.get(), 0x1A100, true, 0x80, 0x00100000);
        std::memset(l.get() + 0x1A108, 0, 16);

        Walk w;
        spu::ListStallTable t;
        t.reset();
        const spu::ListCursor start = spu::list_start(0x2000, 0x1A100, 0x18);
        w.table = &t;
        w.run(l.get(), 4, 0x44, start);

        check(w.xfers.size() == 1, "only the first element transfers before the stall");
        check(w.stalls == 1, "element 0 carries the notify bit and stops the list");
        check(w.xfers[0].eal == 0x00100000, "element 0's eal is the one the guest wrote");
        check(w.xfers[0].size == 0x80, "element 0's size is the one the guest wrote");
        check(t.count() == 15, "the stalled command holds the queue slot");

        /* What the guest does between the stall event and the acknowledgement:
         * it inspects the 0x80 bytes element 0 just wrote, derives the next
         * external address from them, and writes element 1 into local storage.
         *
         * This is the whole point: element 1 does not exist at the time of the
         * stall, and the value it carries is only knowable after element 0's
         * transfer completed. */
        std::memset(l.get() + 0x2000, 0xAB, 0x80);   /* element 0's payload */
        const uint32_t next_eal = 0x00100080;        /* derived from what arrived */
        put_elem(l.get(), 0x1A108, false, 0x40, next_eal); /* element 1, filled NOW */

        /* Wire the ack. */
        uint32_t cmd = 0;
        spu::ListCursor resumed;
        check(t.take(4, cmd, resumed), "the acknowledgement finds the stalled tag");
        check(cmd == 0x44, "the resume carries the original command");
        check(resumed.list == 0x1A108, "the resume re-reads from the NEXT element (2957)");
        check(resumed.lsa == 0x2080, "the resume continues the lsa past element 0 (2958)");
        check(resumed.remaining == 0x10, "the resume continues with the elements left over");

        /* Resume the walk from the acknowledged cursor. */
        Walk w2;
        w2.run(l.get(), 4, cmd, resumed);

        check(w2.xfers.size() == 2, "the resumed walk transfers elements 1 and 2");
        check(w2.xfers[0].eal == next_eal, "element 1 is read from local storage AT RESUME, not at stall");
        check(w2.xfers[0].size == 0x40, "element 1's size is the one written after the stall");
        check(w2.xfers[0].lsa == 0x2080, "element 1's lsa continues element 0's");
        check(w2.xfers[1].eal == 0x0 && w2.xfers[1].size == 0x0,
              "element 2 was never written: the walk sees zero, as the guest's does");

        /* The defect stated as a property: had the walker decoded element 1
         * before the stall, it would have produced size 0 and eal 0 for it --
         * the unmapped-0 fault -- and the guest's real element 1 would never be
         * seen. Assert that the pre-stall content differs from the post-stall
         * content, so this test cannot pass vacuously. */
        Ls pre;
        put_elem(pre.get(), 0x1A100, true, 0x80, 0x00100000);
        std::memset(pre.get() + 0x1A108, 0, 16);
        spu::ListElement stale;
        spu::ListCursor peek = spu::list_start(0x2000, 0x1A100, 0x18);
        spu::list_next(pre.get(), peek, stale);   /* element 0 */
        spu::list_next(pre.get(), peek, stale);   /* element 1, as it was */
        check(stale.eal == 0x0 && stale.size == 0x0,
              "element 1 read before the stall is eal 0 / size 0: the unmapped-0 transfer");
        check(stale.eal != w2.xfers[0].eal, "the post-stall read differs: the test is not vacuous");

        t.complete(4);
        check(t.count() == 16, "the finished list frees its queue slot");
    }

    std::printf("%d checks, %d failure(s)\n", checks, failures);
    return failures ? 1 : 0;
}
