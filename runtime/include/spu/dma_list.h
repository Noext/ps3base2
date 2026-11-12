#pragma once

#include <cstdint>
#include <cstring>

/* The DMA list walker for MFC_PUTL/PUTLB/PUTLF (0x24/0x25/0x26) and
 * MFC_GETL/GETLB/GETLF (0x44/0x45/0x46), written as a state machine so the
 * unit test drives the same code the runtime does.
 *
 * Every rule cites the RPCS3 line that is the specification:
 *   rpcs3/Emu/Cell/SPUThread.cpp:2927  do_list_transfer
 *   rpcs3/Emu/Cell/SPUThread.cpp:2939  struct alignas(8) list_element
 *   rpcs3/Emu/Cell/SPUThread.cpp:2957  item_ptr = args.eal & 0x3fff8
 *   rpcs3/Emu/Cell/SPUThread.cpp:2958  arg_lsa = args.lsa & 0x3fff0
 *   rpcs3/Emu/Cell/SPUThread.cpp:2970  ts_mask = 0x7fff
 *   rpcs3/Emu/Cell/SPUThread.cpp:3279  size = ts & ts_mask
 *   rpcs3/Emu/Cell/SPUThread.cpp:3451  arg_size -= 8
 *   rpcs3/Emu/Cell/SPUThread.cpp:3455  if (!arg_size) break;  -- no stall on the last element
 *   rpcs3/Emu/Cell/SPUThread.cpp:3460  if (items[index].sb & 0x80) -> stall
 *   rpcs3/Emu/Cell/SPUThread.cpp:5463  WrListStallAck resumes from the stored cursor
 *
 * The element is 8 bytes, big-endian, in local storage:
 *
 *   word0: notify:1 (bit 31, i.e. the high bit of the first byte)
 *          reserved:16
 *          size:15  (bits 14..0 of the low half)
 *   word1: eal:32
 *
 * The list base is 8-byte aligned, the local address starts at the command's
 * ls and advances by each element's size rounded up to 16.
 */

namespace spu {

/* The cursor is the ENTIRE resume state of a stalled list. RPCS3 stores the
 * same three values into the queue entry (args.eal, args.lsa, args.size) plus
 * the tag, so a resumed list continues from exactly here and re-reads the
 * following elements from local storage at that moment. */
struct ListCursor {
    uint32_t lsa = 0;       /* next local storage address, low nibble of eal ORed in */
    uint32_t list = 0;      /* next element address inside local storage */
    uint32_t remaining = 0; /* bytes of list still to consume */
};

struct ListElement {
    bool notify = false; /* the stall-and-notify bit */
    uint32_t size = 0;   /* transfer size, 15 bits */
    uint32_t eal = 0;    /* external address */
};

/* The command's own fields seed the cursor. RPCS3:2957-2958. */
inline ListCursor list_start(uint32_t lsa, uint32_t list, uint32_t size) {
    ListCursor c;
    c.lsa = lsa & 0x3FFF0u;
    c.list = list & 0x3FFF8u;
    c.remaining = size;
    return c;
}

/* Decode the element at the cursor and advance the cursor past it.
 * Returns false when the list is exhausted (fewer than 8 bytes left). */
inline bool list_next(const uint8_t* ls, ListCursor& c, ListElement& e) {
    if (c.remaining < 8) return false;
    const uint8_t* p = ls + (c.list & 0x3FFFFu);
    /* Big-endian: word0 = p[0..3], high bit of p[0] is the notify bit, and the
     * 15-bit size is the low 15 bits of the 32-bit word0 (RPCS3:2970,3279). */
    e.notify = (p[0] & 0x80u) != 0;
    e.size = ((uint32_t(p[2]) << 8) | uint32_t(p[3])) & 0x7FFFu;
    e.eal = (uint32_t(p[4]) << 24) | (uint32_t(p[5]) << 16) | (uint32_t(p[6]) << 8) | uint32_t(p[7]);
    c.list = (c.list + 8u) & 0x3FFF8u;
    c.remaining -= 8u;
    return true;
}

/* The transfer the element describes. The local address carries the low nibble
 * of the external address (the MFC copies an unaligned tail). */
inline uint32_t list_transfer_lsa(const ListCursor& c, const ListElement& e) {
    return (c.lsa | (e.eal & 0xFu)) & 0x3FFFFu;
}

/* Advance the local address and the external address by the transfer size,
 * both rounded up to 16. RPCS3 does this after the transfer (arg_lsa +=
 * utils::align<u32>(size, 16)) so the NEXT element's lsa is already correct
 * whether or not that element has been written yet. */
inline uint32_t list_advance_lsa(uint32_t lsa, uint32_t size) {
    return (lsa + ((size + 15u) & ~15u)) & 0x3FFF0u;
}

/* Stop after the element that carries the notify bit, UNLESS it was the last
 * element of the list: RPCS3 tests arg_size after decrementing it and breaks
 * out on zero before the notify check (3451-3460), so a notify bit on the final
 * element does not raise MFC_LIST_STALL_NOTIFY. */
inline bool list_should_stall(const ListElement& e, const ListCursor& after) {
    return e.notify && after.remaining >= 8;
}

/* The queue and stall bookkeeping for list commands, as one object so the unit
 * test exercises the shipped rules instead of a paraphrase.
 *
 * RPCS3 keeps ONE entry in mfc_queue for a list command for its whole life,
 * including across any number of stall/resume cycles: do_list_transfer returns
 * false with args.tag |= 0x80 and MFC_WrListStallAck (SPUThread.cpp:5463) only
 * clears that bit and calls do_mfc again on the SAME entry. A walker that
 * enqueues on every stall leaks queue slots, and RCHCNT(MFC_Cmd) is
 * 16 - mfc_size (SPUThread.cpp:5529) -- the number the SPURS kernel reads
 * before issuing its next command.
 */
struct ListStallTable {
    struct Slot {
        bool active = false;  /* stopped at a stall-and-notify element */
        bool queued = false;  /* occupies an mfc_queue entry */
        uint32_t cmd = 0;
        ListCursor cursor;
    };
    Slot slot[32];
    uint32_t queued = 0; /* mfc_queue */

    void reset() {
        for (auto& s : slot) s = Slot{};
        queued = 0;
    }

    /* A list stopped at a notify element. Only the first stop for a given
     * command enters the queue; a later stop resumes the same entry. */
    void stall(uint32_t tag, uint32_t cmd, const ListCursor& c) {
        Slot& s = slot[tag & 0x1F];
        if (!s.queued) {
            s.queued = true;
            queued++;
        }
        s.active = true;
        s.cmd = cmd;
        s.cursor = c;
    }

    /* MFC_WrListStallAck: take the resume state, leaving the entry queued.
     * Returns false when that tag has nothing stalled. */
    bool take(uint32_t tag, uint32_t& cmd, ListCursor& c) {
        Slot& s = slot[tag & 0x1F];
        if (!s.active) return false;
        s.active = false;
        cmd = s.cmd;
        c = s.cursor;
        return true;
    }

    /* The command finished: its entry leaves the queue. */
    void complete(uint32_t tag) {
        Slot& s = slot[tag & 0x1F];
        s.active = false;
        if (s.queued) {
            s.queued = false;
            if (queued) queued--;
        }
    }

    /* RCHCNT(MFC_Cmd), SPUThread.cpp:5529. */
    uint32_t count() const { return queued < 16 ? 16 - queued : 0; }
};

/* The stall status channel's value: one bit per stalled tag
 * (SPUThread.cpp:3472, ch_stall_stat.set_value(rotl(1, tag) | get_value())). */
inline uint32_t list_stall_bit(uint32_t tag) { return 1u << (tag & 0x1F); }

} /* namespace spu */
