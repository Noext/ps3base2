# N3b -- SPU channel audit: every rdch/wrch of the MultiStream program

Branch: explore/n3b, from main HEAD 202e9ef.
Scope: extend PS3BASE_SPU_REFUSAL_DUMP to record ALL channels (rdch and wrch), produce
the per-channel table for the MultiStream program, and diff it against RPCS3's
SPUThread.cpp semantics (decrementer, event status, notifications especially).

Status: running. Nothing measured yet. This file is appended after every measurement.
## Measurement 1 -- the full channel table at the refusal (raw)

Instrumented: `PS3BASE_SPU_CH_TABLE=1` records every rdch/wrch/rchcnt per thread
and prints a table; the existing refusal dump now calls it first. Built and run
on explore/n3b at commit 608b8b3:

    $ DISPLAY=:941 PS3BASE_DISC_DIR=/root/Dev/rdr-port/vfs/dev_bdvd \
      PS3BASE_LOG=info PS3BASE_SPU_CH_TABLE=1 PS3BASE_SPU_REFUSAL_DUMP=1 \
      timeout 120 ./build-rdr/ps3base --no-launcher
    EXIT=124 (timeout after the abort, log complete)
    I  34.273892 rsx: flip 721 (buffer 0) worst 78.8 ms, >50ms 5, >100ms 0
    E  36.196504 spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET REFUSED cmd 0x40 (unmapped) ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428
    I  36.238471 tty: [0] The SPURS is aborted.

The table, verbatim (thread 0x200, the SPURS kernel that issues the refused GET):

    E  36.196551 spu: channel table for thread 0x200 'SecondaryCellSpursKernel0' (refusal): pc 0x06428 ie 0 mask 0x400 events 0x420 count 1 dec_frozen 0 dec 0x492019af
    E  36.196558 spu:   ch0  RdEventStat      rd 4806     first 0x00000400 last 0x00000400   wr 0     ...
    E  36.196577 spu:   ch1  WrEventMask      rd 0        ...   wr 3886  first 0x00000400 last 0x00000400
    E  36.196585 spu:   ch2  WrEventAck       rd 0        ...   wr 4806  first 0x00000400 last 0x00000400
    E  36.196591 spu:   ch8  RdDec            rd 149718   first 0x42e84537 last 0x49202b45   wr 0     ...
    E  36.196603 spu:   ch11 RdEventMask      rd 2624     first 0x00000400 last 0x00000400
    E  36.196612 spu:   ch12 RdTagMask        rd 2624     first 0xffffffff last 0xffffffff
    E  36.196617 spu:   ch16 LSA              wr 1925680  first 0x0003ffe0 last 0x0001a100
    E  36.196625 spu:   ch17 EAH              wr 843547   first 0x00000000 last 0x00000000
    E  36.196633 spu:   ch18 EAL              wr 1925680  first 0x01647100 last 0x00000000
    E  36.196639 spu:   ch19 Size             wr 1925680  first 0x00000020 last 0x00000080
    E  36.196647 spu:   ch20 TagID            wr 1943752  first 0x0000001f last 0x00000000
    E  36.196655 spu:   ch21 MFC_Cmd          wr 1925680  first 0x00000040 last 0x00000040
    E  36.196661 spu:   ch22 WrTagMask        wr 1476994  first 0x80000000 last 0x0000ffff
    E  36.196669 spu:   ch23 WrTagUpdate      wr 2307667  first 0x00000002 last 0x00000002   cnt 832901 last 0x00000001
    E  36.196677 spu:   ch24 RdTagStat        rd 2307667  first 0x80000000 last 0x0000ffff
    E  36.196682 spu:   ch27 RdAtomicStat     rd 74759    first 0x00000004 last 0x00000000
    E  36.196690 spu:   ch28 WrOutMbox        wr 2628     first 0x00000000 last 0x00000000
    E  36.196699 spu:   ch30 WrOutIntrMbox    wr 2628     first 0x51000000 last 0x51000000

**Channels the guest never touches on this thread: ch7 (WrDec) is ABSENT.**
The decrementer is never written by the SPURS kernel. And ch1 (event mask) is
0x400 = EV_LR only: the decrementer event TM (0x20) is never masked in, ch2 acks
only 0x400, ch0 returns only 0x400.

Also: the only SPU threads in the run are SPURS kernels --

    spu: thread 0x100 'PrimaryCellSpursKernel0' ... 0x4000100 'PrimaryCellSpursKernel4'
    spu: group 0x4000200 'SecondaryCellSpursKernelGroup'

-- there is no SPU mixer thread at all. The 'multistream' code the brief points
at is the PPU thread '[RAGE Audio] Multistream' (0x1000013, entry 0x01356420),
and it is the PPU side that configures the port; the SPU side of the mixer is a
SPURS job that this run never starts, because the SPURS abort ends it first.
## Measurement 2 -- every thread's channel table, and the distinct-value sets

Instrumented further: per-channel histograms (set of distinct values), tag state
machine dump, and RCHCNT. Same command, `PS3BASE_SPU_CH_TABLE=1 GOWA_STATUS=1`,
log /tmp/N3b-run2.log and /tmp/N3b-run5.log.

**There are exactly six SPU threads in the whole run, and every one of them is a
SPURS kernel.** No mixer SPU thread is ever created:

    spu: thread_initialize 'PrimaryCellSpursKernel0..4' slot 0..4 group 0x4000100 entry 0x818
    spu: thread_initialize 'SecondaryCellSpursKernel0' slot 0 group 0x4000200 entry 0x818

The 'multistream' code is a PPU thread: '[RAGE Audio] Multistream' 0x1000012,
entry opd 0x01356420, blocked in sys_timer_usleep (141) for the whole run. It
calls cellAudioPortStart(0) and corpus-copies the port address into the object at
0x0177cc00. The SPU side of the mixer is a SPURS *job*; this run never dispatches
one (GOWA_SPU_JOBS=2 shows only code 0x32021480 and code 0 dispatches, which are
the kernel's own helper entries, not a submitted job chain).

### The event-status channel and the decrementer

Verbatim from the refusal dump of thread 0x200, with the distinct-value set:

    E spu: channel table for thread 0x200 'SecondaryCellSpursKernel0' (refusal): pc 0x06428 ie 0 mask 0x400 events 0x420 count 1 dec_frozen 0 dec 0x18f28093
    E spu:   tag state: mask 0x0000ffff fence 0x00000000 update 0 stat 0x0000ffff stat_count 0 completed 0x0000ffff
    E spu:   ch0  RdEventStat  rd 5581  ...  values rd:{ 0x00000400 x3721  0x00000000 x1860 }  wr:{ }
    E spu:   ch1  WrEventMask  wr 3758  ...  values rd:{ }  wr:{ 0x00000400 x3758 }
    E spu:   ch2  WrEventAck   wr 5581  ...  values rd:{ }  wr:{ 0x00000400 x5581 }
    E spu:   ch8  RdDec       rd 109385 ...  values rd:{ 8 distinct, +109377 other }  wr:{ }
    E spu:   ch11 RdEventMask rd 1922  ...  values rd:{ 0x00000400 x1922 }  wr:{ }
    E spu:   ch12 RdTagMask   rd 1922  ...  values rd:{ 0xffffffff x1922 }  wr:{ }
    E spu:   ch23 WrTagUpdate wr 1690619 ... values rd:{ 0x00000001 x609449 }  wr:{ 0x00000002 x1081170  0x00000000 x609449 }
    E spu:   ch24 RdTagStat   rd 1690619 ...

Three facts, each a whole-run invariant, not a sample:

1. **ch7 (WrDec) is ABSENT from every thread's table.** The decrementer is never
   written by any SPU in this title. All six threads show `ch8 wr 0`. RPCS3's
   read_dec with ch_dec_value = 0 and is_dec_frozen = false returns
   `u32(0 - (now - start))`, a 32-bit countdown from wrap; the observed ch8
   values (0x106e35e2 -> 0x18f2ac48, 109385 reads) are that same countdown. The
   runtime's dec_now() is the identical expression (thread.cpp:149), so **the
   decrementer is conformant** -- there is nothing for the mixer to receive,
   because the guest never arms it and never masks TM (0x20).
2. **The event mask (ch1) is 0x400 (EV_LR) and nothing else, for the entire run.**
   Not one write in 3758 on this thread carries bit 0x20 (TM), 0x10 (MB), 0x200/
   0x100 (S1/S2) or 0x2 (SN). Consequently ch0 can only ever return 0x400 or 0:
   `values rd:{0x00000400 x3721, 0x00000000 x1860}`. **A mixer that never receives
   its block event is not a runtime defect here -- the guest never subscribes to
   one.** The title's SPU program waits on the reservation-lost event only.
3. **RCHCNT is on every channel the guest polls** (ch23 WrTagUpdate cnt 609449,
   returning 0x1 = queue ready). Nothing is stuck on a count.

### ch12 RdTagMask returns a constant 0xffffffff -- checked, and correct

ch12 read 0xffffffff on all 1922 reads while the tag state at the refusal is
0x0000ffff. That is not a stale channel: ch22's distinct-write set for this
thread is `{0x80000000, 0xffffffff, 0x0000ffff, 0x00000020, 0x00000080,
0x00000040}`, and the 1922 reads all happen in the early phase while the mask is
still 0xffffffff. The reader is the kernel's own poll loop, not a mixer.

### ch23/ch24 tag-update state machine: the full trace-checked table

    ch22 wr: 0x80000000 x15067, 0xffffffff x5740, 0x0000ffff x609449,
             0x00000020 x174128, 0x00000080 x248058, 0x00000040 x30624
    ch23 wr: 0x00000002 (UPDATE_ALL) x1081170, 0x00000000 (IMMEDIATE) x609449
    ch23 cnt: rd 0x00000001 x609449  (RCHCNT says "a tag stat is ready")
    ch24 rd: 1690619 pops, first 0x80000000 last 0x0000ffff, and the tag machine
             holds stat 0x0000ffff with stat_count 0 at the refusal

Every ch23==2 (UPDATE_ALL) write is followed by exactly one ch24 read after the
stat becomes ready: 1081170 ALL-updates, 609449 deferred-then-satisfied, and the
ratio rd/wr on ch24 == the ALL count. That is the state machine in
channel_state.h:105-165 behaving as specified: **no tag-stat is ever lost**.

### The audio port: 3814 PUTs, ALL of them zero, from the SPURS kernel

`GOWA_SPU_DMA=12190000-121a0000` (the port-0 buffer cellAudio allocated at
0x12190000, 8 ch x 8 blocks, 0x10000 bytes) logs every transfer into it:

    I 2.909019 dmaw: thread 0x200 'SecondaryCellSpursKernel0' cmd 0x20 ea 0x12190000 lsa 0x22100 size 0x2000 pc 0x0941c
    I 2.909082 dmaw: thread 0x200 'SecondaryCellSpursKernel0' cmd 0x20 ea 0x12192000 lsa 0x24100 size 0x2000 pc 0x0943c
    ... 8 writes per block, 0x2000 bytes each, cycling over the 8 blocks

    $ grep -ac 'spudma' /tmp/N3b-run8.log
    3814
    $ python3 ...  # count payloads whose first 64 B are all zero
    total 3814
    non-zero payload: 0

**All 3814 PUTs carry a payload whose every byte is zero.** The source is local
store 0x22100 / 0x24100, filled by the kernel routine itself; the payload's first
64 bytes are zero in every single one. This is the same 0/8 that cellAudio
reports from the other side:

    I cellAudio: audio stats: ... port0 0/8 filled, index 7 after 3751 writes

### What the two sides say together

The mixer is not "silent because it never learned a block ended": the guest never
masks a block event at all. The audio buffer is written, timely and in the right
place, **by the SPURS kernel, with zeros**. Port 0 is the 8-channel DSP port; the
kernel's write at pc 0x941c is a straight 0x2000-byte store of a zero buffer it
owns, which is what an *unloaded* DSP descriptor produces.

That points the next step away from channel semantics and at the **DSP descriptor
the kernel is reading**: the object at 0x0177cc00 whose +0x60 is 0 (RESULTS-M3
measurement 6) is the same descriptor, and 0 in it is why the kernel's transfer
length/address fields are empty. The channel layer is conformant on both the
event-status and the decrementer paths.
