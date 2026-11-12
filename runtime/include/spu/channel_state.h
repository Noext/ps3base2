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

}