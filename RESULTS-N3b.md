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
