#pragma once

#include <cstdint>

/* SPU channel state machines, written from RPCS3's implementation so they can be
 * unit-tested without a SpuThread, guest memory or a host thread.
 *
 * Every rule here cites the RPCS3 line it comes from. The runtime's SpuThread
 * drives these instead of keeping its own copy of the logic, because a second
 * copy is a second specification and this project has already paid for that.
 *
 * Sources (refs/):
 *   rpcs3/rpcs3/Emu/Cell/SPUThread.cpp:6549  SPU_WrEventMask
 *   rpcs3/rpcs3/Emu/Cell/SPUThread.cpp:6581  SPU_WrEventAck
 *   rpcs3/rpcs3/Emu/Cell/SPUThread.cpp:6446  MFC_WrTagUpdate
 *   rpcs3/rpcs3/Emu/Cell/SPUThread.cpp:6420  MFC_WrTagMask
 *   rpcs3/rpcs3/Emu/Cell/SPUThread.cpp:4202  get_mfc_completed
 *   rpcs3/rpcs3/Emu/Cell/SPUThread.cpp:5518  get_ch_count
 *   rpcs3/rpcs3/Emu/Cell/SPUThread.cpp:5406  get_events mask validation
 *   rpcs3/rpcs3/Emu/Cell/SPUInterpreter.cpp:579 STOPD
 *   rpcs3/rpcs3/Emu/Cell/SPUInterpreter.cpp:618 BISLED
 *   rpcs3/rpcs3/Emu/Cell/MFC.h             event and tag-update constants
 */

namespace spu {

/* SPU Events -- rpcs3/Emu/Cell/SPUThread.h:96-111. */
enum : uint32_t {
    EVENT_MS = 0x1000,
    EVENT_A = 0x800,
    EVENT_LR = 0x400,
    EVENT_S1 = 0x200,
    EVENT_S2 = 0x100,
    EVENT_LE = 0x80,
    EVENT_ME = 0x40,
    EVENT_TM = 0x20,
    EVENT_MB = 0x10,
    EVENT_QV = 0x8,
    EVENT_SN = 0x2,
    EVENT_TG = 0x1,
};

/* SPU_EVENT_IMPLEMENTED, SPUThread.h:113. A mask with any other bit set is a
 * request this runtime cannot honour, and RPCS3 throws on it rather than
 * pretending the event will arrive (get_events, SPUThread.cpp:5406). */
constexpr uint32_t EVENT_IMPLEMENTED = EVENT_LR | EVENT_TM | EVENT_SN | EVENT_S1 | EVENT_S2 | EVENT_MB;

/* MFC_TAG_UPDATE_*, rpcs3/Emu/Cell/MFC.h:56-61. */
enum : uint32_t {
    TAG_UPDATE_IMMEDIATE = 0,
    TAG_UPDATE_ANY = 1,
    TAG_UPDATE_ALL = 2,
};

/* The three event fields live in SpuThread as plain words because
 * interrupt_pending() and check_interrupt() read them with atomic_ref outside
 * the channel lock. These helpers are the only place that decides what a write
 * does, so the unit test exercises exactly the shipped rule. */
inline void event_set(uint32_t& events, uint32_t mask, bool& count, uint32_t bits) {
    events |= bits;
    if (mask & bits) count = true;
}

/* SPU_WrEventMask, SPUThread.cpp:6549. An already-set masked event raises the
 * count. The count is NOT cleared: RPCS3's atomic_op returns "!!events.count"
 * on the no-new-event path, i.e. it preserves it. */
inline void event_write_mask(uint32_t& events, uint32_t& mask, bool& count, uint32_t value) {
    mask = value;
    if (events & mask) count = true;
}

/* SPU_WrEventAck, SPUThread.cpp:6581. Acknowledged bits are dropped and the
 * count is preserved unless a still-masked event re-raises it. Clearing the
 * count here loses a notification the guest has not read yet: the guest acks
 * one event, and a different one that had already fired stops being visible.
 * That is the lost-event bug on the SPU wake path. */
inline void event_ack(uint32_t& events, uint32_t mask, bool& count, uint32_t value) {
    events &= ~value;
    if (events & mask) count = true;
}

/* The decrementer freezes when TM is acknowledged while TM is not masked
 * (SPUThread.cpp:6588: freeze_dec = !!((value & EVENT_TM) & ~events.mask)). */
inline bool event_ack_freezes_decrementer(uint32_t value, uint32_t mask) {
    return (value & EVENT_TM) && !(mask & EVENT_TM);
}

/* RdEventStat, SPUThread.cpp:5717 with get_events(reading=true): return the
 * masked bits and clear the count. */
inline uint32_t event_take(uint32_t events, uint32_t mask, bool& count) {
    count = false;
    return events & mask;
}

/* A mask naming an event this runtime cannot deliver is a program asking for
 * something that will never wake it. RPCS3 throws (SPUThread.cpp:5406); naming
 * it in the log is the honest equivalent and does not poison the run. */
inline uint32_t event_unimplemented(uint32_t mask) { return mask & ~EVENT_IMPLEMENTED; }

/* MFC tag-completion state, SPUThread.cpp:4202 and 6446. ps3base performs DMA
 * synchronously, so "fence" would always be empty -- except for a DMA list that
 * stalled, whose remaining elements are still in flight. That is the one case
 * where the difference is observable, and it is the case a SPURS kernel hits. */
struct TagState {
    uint32_t tag_mask = 0;
    uint32_t fence = 0;     /* tags with a command still in the queue */
    uint32_t update = TAG_UPDATE_IMMEDIATE;
    uint32_t stat = 0;
    bool stat_count = false;

    /* get_mfc_completed: ch_tag_mask & ~mfc_fence. */
    uint32_t completed() const { return tag_mask & ~fence; }

    /* MFC_WrTagUpdate, SPUThread.cpp:6446. A value above ALL is ignored by the
     * caller (RPCS3 breaks out to its unknown-channel throw); it must not be
     * treated as ALL. */
    void write_update(uint32_t value) {
        if (value > TAG_UPDATE_ALL) return;
        const uint32_t done = completed();
        if (value == TAG_UPDATE_IMMEDIATE) {
            update = TAG_UPDATE_IMMEDIATE;
            set_stat(done);
        } else if (done && value == TAG_UPDATE_ANY) {
            update = TAG_UPDATE_IMMEDIATE;
            set_stat(done);
        } else if (done == tag_mask && value == TAG_UPDATE_ALL) {
            update = TAG_UPDATE_IMMEDIATE;
            set_stat(done);
        } else {
            update = value;
        }
    }

    /* MFC_WrTagMask, SPUThread.cpp:6420: a write re-evaluates a deferred update. */
    void write_mask(uint32_t value) {
        tag_mask = value;
        if (update == TAG_UPDATE_IMMEDIATE) return;
        const uint32_t done = completed();
        if (done && update == TAG_UPDATE_ANY) {
            update = TAG_UPDATE_IMMEDIATE;
            set_stat(done);
        } else if (done == value && update == TAG_UPDATE_ALL) {
            update = TAG_UPDATE_IMMEDIATE;
            set_stat(done);
        }
    }

    /* do_mfc's completion hook, SPUThread.cpp:3965-3980: when a command leaves
     * the queue, a deferred update is re-tested. */
    void command_completed() {
        if (update == TAG_UPDATE_IMMEDIATE) return;
        const uint32_t done = completed();
        if (done && update == TAG_UPDATE_ANY) {
            update = TAG_UPDATE_IMMEDIATE;
            set_stat(done);
        } else if (done == tag_mask && update == TAG_UPDATE_ALL) {
            update = TAG_UPDATE_IMMEDIATE;
            set_stat(done);
        }
    }

    /* MFC_RdTagStat pops the value and clears the count. */
    uint32_t take_stat() {
        stat_count = false;
        return stat;
    }

    void set_stat(uint32_t value) {
        stat = value;
        stat_count = true;
    }
};

/* STOP / STOPD signal codes. STOP carries its code in the low 14 bits of the
 * instruction word (SPUInterpreter.cpp:159, "op.opcode & 0x3fff"); STOPD has no
 * code field and always signals 0x3fff (SPUInterpreter.cpp:579). Dispatching
 * STOPD through the STOP path signals whatever the rt/ra fields happen to hold,
 * which for the usual stopd encoding is 0 -- an unknown code that stops the
 * thread with an error instead of a clean stop. */
constexpr uint32_t STOPD_SIGNAL = 0x3fff;
inline uint32_t stop_signal(uint32_t word) { return word & 0x3fff; }
inline uint32_t stopd_signal() { return STOPD_SIGNAL; }

/* RCHCNT for the MFC command channel: 16 - queued commands (SPUThread.cpp:5529).
 * A constant 16 tells the guest the queue is always empty. */
inline uint32_t mfc_queue_count(uint32_t queued) { return queued < 16 ? 16 - queued : 0; }

/* ---- DMA error -------------------------------------------------------- */

/* A DMA that the MFC refuses must not touch memory. The old behaviour
 * zero-filled the local-store destination of a refused GET, which writes 128
 * bytes the guest never asked for. On Red Dead Redemption it landed in a
 * scratch buffer that the SPURS kernel was executing through: the all-zero word
 * decodes as a stop instruction, and the guest aborted its own workload.
 *
 * The refusal is decided BEFORE any translation, from the command alone, so
 * there is no window in which a partly-executed transfer leaves bytes behind.
 * This is the pure part, unit-tested in tests/spu_channel_conformance.cpp. */

/* Effective addresses at or above this are the SPU thread window and MMIO,
 * which the runtime maps separately; everything below it is guest memory. */
constexpr uint32_t DMA_MMIO_BASE = 0xF0000000u;

/* Why a transfer was refused. */
enum class DmaRefusal : uint32_t {
    None = 0,
    /* MFC_EAL/MFC_Size do not describe a transfer: the size reaches past the
     * end of the 32-bit effective address space. Hardware refuses these
     * instead of wrapping or splitting them. */
    Range = 1,
};

/* Hardware refuses a transfer that runs off the end of the effective address
 * space rather than truncating it. 0xFFFFFFC0 + 0x80 ends at 0x100000040, and a
 * 32-bit wrap would have written 0x00000000..0x00000040 -- the address at which
 * the failing RDR transfer also pointed, for an unrelated reason. A wrap must
 * not silently become a legal transfer. */
inline DmaRefusal dma_check_range(uint32_t ea, uint32_t size) {
    if (!size) return DmaRefusal::None;
    if (uint64_t(ea) + size > 0x100000000ull) return DmaRefusal::Range;
    return DmaRefusal::None;
}

inline const char* dma_refusal_name(DmaRefusal r) {
    switch (r) {
    case DmaRefusal::None: return "none";
    case DmaRefusal::Range: return "range";
    }
    return "?";
}

/* Whether a transfer reads local store (GET) or writes it (PUT), from the
 * command byte alone. A list command shares the class of its base command. */
inline bool dma_is_put(uint32_t cmd) {
    const uint32_t cls = cmd & 0xF0;
    return cls == 0x20 || cls == 0x30 || cls == 0xA0;
}

/* ---- the decrementer ------------------------------------------------- */

/* SPU_RdDec and the TD/TM event, written as RPCS3's arithmetic.
 *
 *   rpcs3/Emu/Cell/SPUThread.cpp:5398  std::pair<u32,u32> read_dec()
 *       const u64 res = ch_dec_value - (is_dec_frozen ? 0
 *                                       : (get_timebased_time() - ch_dec_start_timestamp));
 *       return { u32(res), u32(res >> 32) };
 *   rpcs3/Emu/Cell/SPUThread.cpp:5425  the TM arm of get_events()
 *       if (mask_hint & SPU_EVENT_TM)
 *           if (const u64 res = read_dec().second)
 *           {
 *               ch_dec_start_timestamp -= res << 32;
 *               collect |= SPU_EVENT_TM;
 *           }
 *
 * Two things follow, and BOTH were wrong in this runtime before this was
 * written down:
 *
 *  1. The decrementer is a 32-BIT counter. The READ returns the low word, and
 *     the EVENT fires on the underflow of that 32-bit counter, which is exactly
 *     'the high word of the 64-bit difference is non-zero'. Comparing
 *     'elapsed > dec_value' asks a different question: with dec_value = 0 (the
 *     guest never wrote SPU_WrDec) it is true on every tick, so the event fired
 *     on every single channel read. Measured in the RDR run: ch0 was read 5581
 *     times and the event state word stayed 0x400 because the guest only masks
 *     LR -- the storm was invisible there, which is why it survived.
 *
 *  2. After the underflow the counter is NOT re-armed to 'now'. RPCS3 advances
 *     the start timestamp by (high << 32), i.e. by the whole 2^32 period the
 *     counter ran past, so the next event is one full 32-bit period later.
 *     Re-arming to 'now' makes a dec_value = 1000 countdown fire every 1000
 *     ticks instead of every 2^32.
 *
 * Both rules are pure integer arithmetic, so the unit test drives them here and
 * SpuThread calls them; there is no second copy of the specification. */

/* read_dec(): the 64-bit difference, then the two words the guest and the event
 * logic see. 'frozen' means the counter stopped (TM acknowledged while TM is
 * unmasked, SPUThread.cpp:6603). */
inline uint64_t dec_residual(uint32_t dec_value, uint64_t now, uint64_t start, bool frozen) {
    return uint64_t(dec_value) - (frozen ? 0 : (now - start));
}

/* What SPU_RdDec (ch8) returns. */
inline uint32_t dec_read(uint32_t dec_value, uint64_t now, uint64_t start, bool frozen) {
    return static_cast<uint32_t>(dec_residual(dec_value, now, start, frozen));
}

/* Has the 32-bit counter underflowed since 'start'? This is the TD/TM event, and
 * it is read_dec().second != 0 -- the high word, not a comparison of elapsed
 * against the value. */
inline bool dec_underflowed(uint32_t dec_value, uint64_t now, uint64_t start, bool frozen) {
    return (dec_residual(dec_value, now, start, frozen) >> 32) != 0;
}

/* The start timestamp after an underflow was reported: advance it by the whole
 * 2^32 periods that have elapsed (SPUThread.cpp:5430,
 * 'ch_dec_start_timestamp -= res << 32'). */
inline uint64_t dec_rearm(uint64_t start, uint32_t dec_value, uint64_t now) {
    const uint64_t high = dec_residual(dec_value, now, start, false) >> 32;
    return start - (high << 32);
}

/* ---- channel trace: the distinct-value set --------------------------- */

/* The channel audit asks "what values did this channel carry", not "what was
 * the first one". A SET is the only form of that answer which can be checked:
 * a channel whose first and last value are both legal can still have carried a
 * wrong one in between, and a mean or a range would hide it.
 *
 * Eight slots per direction is the cap because every real SPU pattern fits:
 * 16 tag groups are expressed as a bitmask (one word, not 16), the event masks
 * are 0x400/0x20/0x10, and the channel numbers are fixed. A counter channel
 * such as RdDec takes a new value every read, so the excess is COUNTED rather
 * than stored -- an unbounded set would be a memory leak driven by the guest.
 *
 * This is the pure part; SpuThread::ch_note feeds it. */
struct ChVal {
    uint32_t value = 0;
    uint32_t n = 0;
};

struct ChHist {
    ChVal v[8]{};
    uint8_t n = 0;
    uint32_t other = 0;
};

/* Add one observation. A repeat increments the existing slot (so the set stays
 * small for the channels that matter), a new value takes the next free slot,
 * and everything past the cap is counted as "other" with its value dropped. */
inline void ch_hist_add(ChHist& h, uint32_t value) {
    for (uint8_t i = 0; i < h.n; i++) {
        if (h.v[i].value == value) {
            h.v[i].n++;
            return;
        }
    }
    if (h.n < 8) {
        h.v[h.n].value = value;
        h.v[h.n].n = 1;
        h.n++;
        return;
    }
    h.other++;
}

/* Total observations the histogram holds, "other" included. Used by the test to
 * assert nothing is lost or double-counted. */
inline uint64_t ch_hist_total(const ChHist& h) {
    uint64_t t = h.other;
    for (uint8_t i = 0; i < h.n; i++) t += h.v[i].n;
    return t;
}

/* ---- SPU thread-group exception event -------------------------------- */

/* The event source a group's exception queue receives.
 *
 * This is SYS_SPU_THREAD_GROUP_EVENT_EXCEPTION_KEY 0xFFFFFFFF53505503
 * (rpcs3/Emu/Cell/lv2/sys_spu.h:43) and it is the SAME value as
 * SYS_SPU_THREAD_EVENT_EXCEPTION_KEY 0xFFFFFFFF53505503
 * (rpcs3/Emu/Cell/lv2/sys_event.h:55) -- the two names differ because one is the
 * dequeue source and the other the connect type, but the guest compares against
 * the number. cellSpurs' event helper compares exactly this and nothing else
 * (rpcs3/Emu/Cell/Modules/cellSpurs.cpp:837):
 *
 *     if (event_src == SYS_SPU_THREAD_EVENT_EXCEPTION_KEY) {
 *         spurs->exception = 1;
 *         events[0].source = event_src;
 *         events[0].data1 = event_data1;   // stashed, not interpreted
 *         events[0].data2 = event_data2;
 *         events[0].data3 = event_data3;
 *
 * so the payload below is ours to define as long as the source matches and the
 * fields are self-describing. RPCS3 itself never sends this event -- its SPU
 * access-violation path is a TODO and it pauses instead
 * (Utilities/Thread.cpp:2202-2205) -- so there is no RPCS3 payload to copy, and
 * inventing a hardware layout from memory is exactly what AGENTS.md forbids. */
constexpr uint64_t SPU_EXCEPTION_KEY = 0xFFFFFFFF53505503ull;

/* The cause bits, read out of libsre's own exception dump. The payload in
 * RESULTS.md M4 was recovered by varying each field and reading the guest's
 * printout back; this table is that measurement written down.
 *
 *   source: "cause: DMA alignment error"        <- bit 0
 *           "cause: DMA command error"          <- bit 1
 *           "cause: SPU error"                  <- bit 2
 *           "cause: MFC FIR"                    <- bit 3
 *           "cause: MFC segment  MFC_DAR: ..."  <- bit 4
 *           "cause: MFC storage  MFC_DAR: ..."  <- bit 5
 *           "cause: Stop break"                 <- bit 6
 *           "cause: Halt"                       <- bit 7
 *           "cause: Unknown signal  SPU_Status"  <- bit 8
 *           "cause: MAT  MFC_DAR: ..."          <- bit 9
 *           "cause: Unknown exception"          <- no bit set
 *   rpcs3/Emu/Cell/SPUThread.h:121-127 for the class-0 interrupt equivalents. */
enum : uint32_t {
    SPU_EXC_DMA_ALIGNMENT = 1u << 0,
    SPU_EXC_DMA_COMMAND = 1u << 1,
    SPU_EXC_SPU_ERROR = 1u << 2,
    SPU_EXC_MFC_FIR = 1u << 3,
    SPU_EXC_MFC_SEGMENT = 1u << 4,
    /* A transfer whose effective address names no storage. This is the cause a
     * refused DMA raises; it is the one that carries an MFC_DAR, which is the
     * address the handler needs. */
    SPU_EXC_MFC_STORAGE = 1u << 5,
    SPU_EXC_STOP_BREAK = 1u << 6,
    SPU_EXC_HALT = 1u << 7,
    SPU_EXC_UNKNOWN_SIGNAL = 1u << 8,
    SPU_EXC_MAT = 1u << 9,
};

/* The cause a refused transfer raises. A guest address that is inside the
 * 32-bit space but not mapped names no storage; a transfer that runs off the
 * end of the space is the same fault from the handler's point of view, and
 * reporting it as a different bit would need a cause the guest's table does not
 * have. */
inline uint32_t dma_exception_cause(DmaRefusal refusal) {
    (void)refusal;
    return SPU_EXC_MFC_STORAGE;
}

/* data1 is the faulting SPU thread id, printed by the guest as 0x%08x.
 * data2 is NPC in the high word and the cause bits in the low word; the guest
 * prints NPC as 0x%05x and selects its cause line from the low word.
 * data3 is MFC_DAR, the effective address, printed by the causes that have one. */
inline uint64_t spu_exception_data1(uint32_t thread_id) { return thread_id; }
inline uint64_t spu_exception_data2(uint32_t npc, uint32_t cause) {
    return (uint64_t(npc & 0x3FFFC) << 32) | uint64_t(cause);
}
inline uint64_t spu_exception_data3(uint32_t ea) { return ea; }

}