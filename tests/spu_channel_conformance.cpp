/* Unit tests for the SPU channel state machines and the control instructions
 * that carry them. One test per deviation from RPCS3, named after the RPCS3
 * line that is the specification.
 *
 * Each instruction is executed through spu::step, i.e. through the real dispatch
 * table in interpreter.cpp. Calling a handler directly would pass even when the
 * table still routes the opcode to the wrong handler, and that routing is one of
 * the defects: 0x140 (stopd) was dispatched to op_stop.
 *
 * The channel state helpers are the ones SpuThread::read_channel, write_channel
 * and channel_count call, so the test drives shipped code rather than a copy.
 */
#include "spu/channel_state.h"
#include "spu/context.h"

#include <cstdio>
#include <cstring>
#include <memory>

namespace spu {
/* interpreter.cpp's entry point; declared here because the test links the
 * interpreter without dragging in the SpuThread that normally sits above it. */
bool step(Context& c, Host& host);
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

namespace {

/* Records what the instruction handed to the host, so a test can assert the
 * signal code and the channel number rather than a return value alone. */
struct RecHost : spu::Host {
    uint32_t last_signal = 0xFFFFFFFFu;
    int stops = 0;
    uint32_t last_write_ch = 0, last_write_value = 0;
    uint32_t read_chs[8] = {};
    int read_n = 0;
    uint32_t count_chs[8] = {};
    int count_n = 0;
    uint32_t count0 = 0;
    bool advance = true;

    uint32_t read_channel(uint32_t ch) override {
        if (read_n < 8) read_chs[read_n++] = ch;
        return 0x100 + ch;
    }
    void write_channel(uint32_t ch, uint32_t v) override { last_write_ch = ch; last_write_value = v; }
    uint32_t channel_count(uint32_t ch) override {
        if (count_n < 8) count_chs[count_n++] = ch;
        return ch == 0 ? count0 : 0;
    }
    bool stop_and_signal(uint32_t code) override {
        last_signal = code;
        stops++;
        return advance;
    }
    void halt(uint32_t, uint32_t) override {}
};

/* The encoded form the interpreter decodes: 11-bit opcode in the top bits. */
constexpr uint32_t enc11(uint32_t op11, uint32_t rest = 0) { return (op11 << 21) | rest; }

/* Run one instruction through the real dispatch table. */
bool run1(spu::Context& c, spu::Host& h, uint32_t pc, uint32_t word) {
    const uint32_t be = __builtin_bswap32(word);
    std::memcpy(c.ls + (pc & spu::PC_MASK), &be, 4);
    c.pc = pc;
    return spu::step(c, h);
}

struct Ls {
    std::unique_ptr<uint8_t[]> buf{new uint8_t[spu::LS_SIZE]()};
    spu::Context c;
    Ls() { c.ls = buf.get(); }
};

}

int main() {
    /* ======================================================================
     * STOPD signals 0x3fff; STOP signals its own 14-bit field.
     *   rpcs3/Emu/Cell/SPUInterpreter.cpp:579  return spu.stop_and_signal(0x3fff);
     *   rpcs3/Emu/Cell/SPUInterpreter.cpp:159  stop_and_signal(op.opcode & 0x3fff);
     *
     * The deviation: opcode 0x140 was dispatched to op_stop. A stopd word has no
     * code field, so "word & 0x3fff" is whatever bits 13..0 happen to hold -- 0
     * for the common encoding -- and the kernel answered "unknown stop code
     * 0x0" and killed the thread instead of stopping it cleanly. That is the
     * reported failure at spu pc 0x1a100 in SecondaryCellSpursKernel0.
     * ==================================================================== */
    {
        Ls l;
        RecHost h;

        const uint32_t stopd = enc11(0x140);
        check(run1(l.c, h, 0x1A100, stopd), "stopd does not halt the machine");
        check(h.stops == 1, "stopd reaches stop_and_signal");
        check(h.last_signal == 0x3FFF, "stopd signals 0x3fff (SPUInterpreter.cpp:579)");
        check(l.c.pc == 0x1A104, "stopd advances the pc past itself");

        Ls l2;
        RecHost h2;
        check(run1(l2.c, h2, 0x10000, enc11(0x000, 0x001)), "stop 0x001 does not halt");
        check(h2.last_signal == 0x001, "stop signals its own field (SPUInterpreter.cpp:159)");

        Ls l3;
        RecHost h3;
        run1(l3.c, h3, 0x10004, enc11(0x000, 0x102));
        check(h3.last_signal == 0x102, "stop 0x102 is the thread-exit code");

        /* The defect stated as a property: the two opcodes must not produce the
         * same signal for the same low bits. Under the old table they did. */
        Ls l4, l5;
        RecHost h4, h5;
        run1(l4.c, h4, 0x2000, enc11(0x140, 0x001));
        run1(l5.c, h5, 0x2000, enc11(0x000, 0x001));
        check(h4.last_signal == 0x3FFF && h5.last_signal == 0x001,
              "stopd and stop are different opcodes with different signals");
    }

    /* ======================================================================
     * RdEventStat returns masked bits only and clears the count.
     *   rpcs3/Emu/Cell/SPUThread.cpp:5717 with get_events(reading=true), :5445
     * ==================================================================== */
    {
        uint32_t events = 0, mask = 0;
        bool count = false;
        spu::event_write_mask(events, mask, count, spu::EVENT_S1 | spu::EVENT_S2);
        check(!count, "a fresh mask with no events does not raise the count");

        spu::event_set(events, mask, count, spu::EVENT_S1 | spu::EVENT_TM);
        check(count, "a masked event raises the count");
        check(spu::event_take(events, mask, count) == spu::EVENT_S1,
              "RdEventStat returns masked bits only");
        check(!count, "RdEventStat clears the count");
        check((events & spu::EVENT_TM) != 0, "an unmasked event stays pending for a later mask");
    }

    /* ======================================================================
     * WrEventAck preserves a count it did not consume.
     *   rpcs3/Emu/Cell/SPUThread.cpp:6581-6600 -- the atomic_op returns
     *   "!!events.count" on the no-new-event path.
     *
     * The deviation: the old code recomputed the count as
     * "(events & mask) != 0" after the ack, which drops an outstanding count for
     * an event that is no longer in the events word. The guest acks one event
     * and a second that had already fired stops being visible, so a thread that
     * was ready to run parks again.
     * ==================================================================== */
    {
        uint32_t events = 0, mask = spu::EVENT_S1 | spu::EVENT_S2;
        bool count = false;
        spu::event_set(events, mask, count, spu::EVENT_S1);
        check(count, "S1 raises the count");
        spu::event_ack(events, mask, count, spu::EVENT_S1);
        /* RPCS3 never clears the count on an ack: the atomic_op returns
         * "!!events.count" without writing it (SPUThread.cpp:6596). Only
         * RdEventStat clears the count. Clearing it on the ack would lose the
         * wakeup for an event the guest has not read yet. */
        check(count, "an ack does not clear the count (SPUThread.cpp:6596)");
        check((events & spu::EVENT_S1) == 0, "the acked bit is gone");
        check(spu::event_take(events, mask, count) == 0 && !count,
              "RdEventStat after the ack reports nothing and clears the count");

        spu::event_set(events, mask, count, spu::EVENT_S1 | spu::EVENT_S2);
        check(count, "S1|S2 raise the count");
        spu::event_ack(events, mask, count, spu::EVENT_S1);
        check(count, "acking S1 keeps the count S2 raised (SPUThread.cpp:6596)");
        check(spu::event_take(events, mask, count) == spu::EVENT_S2,
              "S2 is still reported after S1 was acked");

        /* The literal old bug: ack a bit that was never masked, while a masked
         * event is pending. Recomputing the count drops it. */
        events = 0; mask = spu::EVENT_LR; count = false;
        spu::event_set(events, mask, count, spu::EVENT_LR);
        const bool before = count;
        spu::event_ack(events, mask, count, spu::EVENT_SN); /* ack something else */
        check(before && count, "acking an unrelated event does not lose the LR count");
    }

    /* ======================================================================
     * WrEventMask does not drop an unread count.
     *   rpcs3/Emu/Cell/SPUThread.cpp:6552-6566
     * ==================================================================== */
    {
        uint32_t events = 0, mask = 0;
        bool count = false;
        spu::event_set(events, spu::EVENT_SN, count, spu::EVENT_SN);
        check(count, "SN raises the count");
        spu::event_write_mask(events, mask, count, 0);
        check(count, "a mask write keeps an outstanding count (SPUThread.cpp:6562)");

        count = false;
        events = 0;
        mask = 0;
        spu::event_set(events, spu::EVENT_LR, count, spu::EVENT_LR);
        spu::event_write_mask(events, mask, count, spu::EVENT_LR);
        check(count, "masking an already-set event raises the count");
        check(mask == spu::EVENT_LR, "the mask is stored");
    }

    /* ======================================================================
     * WrEventAck freezes the decrementer only for TM outside the mask.
     *   rpcs3/Emu/Cell/SPUThread.cpp:6588
     * ==================================================================== */
    {
        check(spu::event_ack_freezes_decrementer(spu::EVENT_TM, 0),
              "TM acked while unmasked freezes the decrementer");
        check(!spu::event_ack_freezes_decrementer(spu::EVENT_TM, spu::EVENT_TM),
              "TM acked while masked does not freeze");
        check(!spu::event_ack_freezes_decrementer(spu::EVENT_S1, 0),
              "acking S1 does not freeze");
    }

    /* ======================================================================
     * A mask naming an undeliverable event is reported, not ignored.
     *   rpcs3/Emu/Cell/SPUThread.cpp:5406 throws on such a mask.
     * ==================================================================== */
    {
        check(spu::event_unimplemented(spu::EVENT_MS | spu::EVENT_A) ==
                  (spu::EVENT_MS | spu::EVENT_A),
              "MS and A are not implementable");
        check(spu::event_unimplemented(spu::EVENT_IMPLEMENTED) == 0,
              "every implemented event passes validation");
        check(spu::event_unimplemented(0) == 0, "an empty mask is legal");
        check(spu::event_unimplemented(spu::EVENT_LR | spu::EVENT_MS) == spu::EVENT_MS,
              "a mixed mask reports only the undeliverable bits");
    }

    /* ======================================================================
     * MFC_WrTagUpdate has three distinct modes; ALL is not ANY.
     *   rpcs3/Emu/Cell/SPUThread.cpp:6446-6478
     *
     * The deviation: the old code set tag_stat = tag_mask unconditionally, so
     * ALL resolved immediately. A guest that transfers several tagged buffers
     * then proceeds to read data that has not landed.
     * ==================================================================== */
    {
        spu::TagState t;
        t.tag_mask = 0x3;
        t.fence = 0x2; /* tag 1 still in flight */
        check(t.completed() == 0x1, "completed = tag_mask & ~fence (SPUThread.cpp:4202)");

        t.write_update(spu::TAG_UPDATE_IMMEDIATE);
        check(t.stat_count && t.stat == 0x1, "IMMEDIATE reports the completed tags");
        check(t.take_stat() == 0x1 && !t.stat_count, "RdTagStat pops the value and clears the count");

        t.write_update(spu::TAG_UPDATE_ANY);
        check(t.update == spu::TAG_UPDATE_IMMEDIATE && t.stat_count && t.stat == 0x1,
              "ANY resolves at once when any tag has completed");

        t = spu::TagState{};
        t.tag_mask = 0x3;
        t.fence = 0x2;
        t.write_update(spu::TAG_UPDATE_ALL);
        check(t.update == spu::TAG_UPDATE_ALL, "ALL stays armed while a tag is still fenced");
        check(!t.stat_count, "ALL does not report a partial completion");

        t.fence = 0;
        t.command_completed();
        check(t.update == spu::TAG_UPDATE_IMMEDIATE, "a completing command resolves an armed ALL");
        check(t.stat_count && t.stat == 0x3, "ALL reports the full mask once complete");

        spu::TagState big;
        big.tag_mask = 0xff;
        big.write_update(7);
        check(big.update == spu::TAG_UPDATE_IMMEDIATE && !big.stat_count,
              "an out-of-range tag-update request is ignored, not treated as ALL");
    }

    /* ======================================================================
     * MFC_WrTagMask re-evaluates an armed update.
     *   rpcs3/Emu/Cell/SPUThread.cpp:6420-6440
     * ==================================================================== */
    {
        spu::TagState t;
        t.tag_mask = 0x3;
        t.fence = 0x2;
        t.write_update(spu::TAG_UPDATE_ALL);
        check(t.update == spu::TAG_UPDATE_ALL, "ALL armed");
        t.write_mask(0x1);
        check(t.update == spu::TAG_UPDATE_IMMEDIATE,
              "a mask write resolves ALL when completed == mask");
        check(t.stat == 0x1, "the resolved status is the new completed set");

        spu::TagState any;
        any.tag_mask = 0x3;
        any.fence = 0x3;
        any.write_update(spu::TAG_UPDATE_ANY);
        check(any.update == spu::TAG_UPDATE_ANY, "ANY stays armed when nothing has completed");
        any.write_mask(0x2);
        check(any.update == spu::TAG_UPDATE_ANY, "a mask write does not invent a completion");
    }

    /* ======================================================================
     * RCHCNT(MFC_Cmd) is 16 - queued, not a constant 16.
     *   rpcs3/Emu/Cell/SPUThread.cpp:5529  case MFC_Cmd: return 16 - mfc_size;
     *
     * The deviation: a constant 16 says the queue is always empty. A guest that
     * polls it before issuing a command over-fills the queue; one that waits for
     * the count to fall never sees it move.
     * ==================================================================== */
    {
        check(spu::mfc_queue_count(0) == 16, "an empty queue reports 16 slots");
        check(spu::mfc_queue_count(1) == 15, "one queued command reports 15 slots");
        check(spu::mfc_queue_count(16) == 0, "a full queue reports 0 slots");
        check(spu::mfc_queue_count(40) == 0, "an over-full queue does not underflow");
        check(spu::mfc_queue_count(0) != spu::mfc_queue_count(1), "the count tracks the queue");
    }

    /* ======================================================================
     * BISLED writes its link register unconditionally and branches only on a
     * pending event count.
     *   rpcs3/Emu/Cell/SPUInterpreter.cpp:618-632
     * ==================================================================== */
    {
        Ls l;
        RecHost h;
        const uint32_t bisled = enc11(0x1AB) | (3u << 0) | (4u << 7); /* rt=3 ra=4 */
        l.c.gpr[4].u32[3] = 0x2000;

        h.count0 = 0;
        run1(l.c, h, 0x1000, bisled);
        check(l.c.gpr[3].u32[3] == 0x1004,
              "bisled writes the link register even when it does not branch (SPUInterpreter.cpp:622)");
        check(l.c.pc == 0x1004, "bisled without a pending event falls through");
        check(h.count_chs[0] == 0, "bisled consults the event channel (ch0)");

        l.c.pc = 0;
        l.c.gpr[3] = spu::v128{};
        h.count0 = 1;
        run1(l.c, h, 0x1000, bisled);
        check(l.c.pc == 0x2000, "bisled branches on a pending event");
        check(l.c.gpr[3].u32[3] == 0x1004, "bisled still links when it branches");
    }

    /* ======================================================================
     * IRET returns to srr0 and honours the e/d interrupt bits.
     *   rpcs3/Emu/Cell/SPUInterpreter.cpp:615; :135 for e/d
     * ==================================================================== */
    {
        Ls l;
        RecHost h;
        l.c.srr0 = 0x1234;
        l.c.interrupts_enabled = false;
        /* e is bit 18, d is bit 19 of the instruction word: SPUOpcodes.h:15-16
         * ("bf_t<u32, 18, 1> e; bf_t<u32, 19, 1> d;"). */
        run1(l.c, h, 0x5000, enc11(0x1AA) | (1u << 18)); /* e = 1 */
        check(l.c.pc == 0x1234, "iret returns to srr0");
        check(l.c.interrupts_enabled, "iret with e=1 enables interrupts");

        l.c.pc = 0;
        l.c.srr0 = 0x4444;
        l.c.interrupts_enabled = true;
        run1(l.c, h, 0x6000, enc11(0x1AA) | (1u << 19)); /* d = 1 */
        check(l.c.pc == 0x4444, "iret to a second srr0");
        check(!l.c.interrupts_enabled, "iret with d=1 disables interrupts");
    }

    /* ======================================================================
     * RDCH / WRCH decode rt in bits 0..6 and ra in 7..13, and use the preferred
     * word.
     *   rpcs3/Emu/Cell/SPUInterpreter.cpp RDCH/WRCH; channel numbers from
     *   rpcs3/Emu/Cell/SPUThread.h:41-76
     *
     * A swapped decode makes every channel access address the wrong channel,
     * which presents as a merely-wrong value rather than as a crash.
     * ==================================================================== */
    {
        Ls l;
        RecHost h;
        run1(l.c, h, 0x100, enc11(0x00D) | (3u << 0) | (5u << 7));
        check(h.read_n > 0 && h.read_chs[0] == 5, "rdch reads the channel in ra");
        check(l.c.gpr[3].u32[3] == 0x105,
              "rdch writes the value into the preferred word of rt");

        Ls l2;
        RecHost h2;
        l2.c.gpr[6].u32[3] = 0xABCD;
        l2.c.gpr[6].u32[0] = 0x1111;
        run1(l2.c, h2, 0x200, enc11(0x10D) | (6u << 0) | (7u << 7));
        check(h2.last_write_ch == 7, "wrch writes the channel in ra");
        check(h2.last_write_value == 0xABCD, "wrch sends the preferred word of rt");
    }

    std::printf("spu_channel_conformance: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}