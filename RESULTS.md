# T3 -- SPU channels and MFC conformance vs RPCS3

## Measurements

### Baseline at 70daf19, my own build-T3, DISPLAY=:918

Run reproduces the reported abort. Raw tail of /tmp/T3.log:

```
I  86.015405 [1683294] tty: [0]       system workload
I  86.015419 [1683294] tty: [0]   Workloads:
I  86.018281 [1683294] tty: [0]   policy module  0x32023680
I  86.018298 [1683294] tty: [0]   workload ID    1
I  86.018319 [1683294] tty: [0] Call Stack Trace:	0x3200cb24
I  86.018401 [1683294] tty: [0] The SPURS is aborted.
I  86.018418 [1683294] tty: [0] process: _sys_process_exit(0x8001000c)
I  86.018709 [1683294] ppu: process exit -2147418100 from thread 0x1000004 lr=0x3000c7e8 (not lifted)
```

flip count at abort: 2341.
### The abort is a null DMA-list pointer, not channel semantics

Dumped by the runtime itself at the fault (PS3BASE writes spu_fault_<id>_<pc>.ls):
/root/Dev/ps3base2/spu_fault_00000200_06424.ls and _0650c.ls and _1a100.ls.

Disassembly of the faulting code, verbatim from tools/spu_dis.py:

```
06400: 0400018a  ori r10,r3,0
06404: 21a00904  wrch ch18,r4
06408: 40800004  il r4,0x0
0640c: 21a00986  wrch ch19,r6
06410: 4020007f  nop r127,r0,r0
06414: 24ff4081  stqd r1,-48(r1)
06418: 1cf40081  ai r1,r1,-48
0641c: 21a00a04  wrch ch20,r4
06420: 12000309  hbrr r9,0x6
06424: 21a00a85  wrch ch21,r5
06428: 33a74d03  lqr r3,0x19e90
0642c: 1c200182  ai r2,r3,128
06430: 23a74c02  stqr r2,0x19e90
06434: 21a00b84  wrch ch23,r4
06438: 01e00b89  rchcnt r9,ch23
0643c: 7c004488  ceqi r8,r9,1
06440: 4020007f  nop r127,r0,r0
06444: 207ffe88  brz r8,0x06438
06448: 427fff9e  ila r30,0xffff
0644c: 01a00c03  rdch r3,ch24
```

The bug is at 0x06420/0x06424: `hbrr r9,0x6` then `wrch ch21,r5`. ch21 is
MFC_Cmd and ch20 is MFC_TagID, written at 0x0641c from r4 which 0x06408 set to
0. LSA/EAL/Size for that command are the wrch ch18/ch19 at 0x06404/0x0640c.

The destination lsa 0x1a100 is not SPU code at all. From the same dump:

```
$ python3 -c "d=open('spu_fault_00000200_1a100.ls','rb').read(); ..."
0x1a000 region: 0000000000000000000000000000000000000000000000000000000000000000
0x1b000 ALL ZERO
0x1c000 ALL ZERO
0x1d000 ALL ZERO
```

and the word at the stop pc:

```
1a0f0: 00000000  stop 0x0
1a0fc: 00000000  stop 0x0
1a100: 00000000  stop 0x0   <- "unknown stop code 0x0 at 0x1a100"
1a104: 00000000  stop 0x0
```

So the SPU executed a zero word: the SPU PC reached 0x1a100, which is unwritten
LS (all zero), and word 0x00000000 decodes to STOP with code 0. That is the
"unknown stop code 0x0 at 0x1a100" line: not an unsupported stop code, but a
jump to zeroed memory.

The pointer it jumped through is the bump pointer at 0x19e90, read at 0x06428 and
advanced by 128 at 0x0642c..0x06430:

```
spu_fault_00000200_06424.ls: [0x19e90] = 0x00020af0   [0x19e94] = 0x00020730
spu_fault_00000200_1a100.ls: [0x19e90] = 0x00023800   [0x19e94] = 0x00021c30
```

0x23800 is in the all-zero run (0x1e000 nonzero, 0x1f000..0x22fff zero,
0x23000..0x28fff zero). The list built in that region has null EAs, so the MFC
list command issues a GET from ea 0x00000000:

```
E  95.095526 spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET cmd 0x40 unmapped ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428
E  95.095675 spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET cmd 0x40 unmapped ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06510
E  95.095733 spu: thread 0x200 unknown stop code 0x0 at 0x1a100
```

Verdict on the question posed: guest address 0x0 is NOT legal on this runtime
because vm::map starts the main region at 0x00010000 (runtime/src/memory.cpp:42,
"t[0] = {0x00010000, 0x0FFF0000, 0x10000}"), and RPCS3's main area is likewise
mapped from 0x00010000 (rpcs3/Emu/Memory/vm.cpp:2174 special-cases addr ==
0x00010000). A DMA to 0x0 is a genuine guest fault in both, so our "unmapped"
diagnosis is correct and must not be papered over.

### Fixes landed (each with the RPCS3 line that is the specification)

1. STOPD was dispatched to op_stop (interpreter.cpp "set(0x140, 11, op_stop)").
   RPCS3 has its own STOPD handler that always signals 0x3fff
   (rpcs3/Emu/Cell/SPUInterpreter.cpp:579); STOP signals op.opcode & 0x3fff
   (SPUInterpreter.cpp:159). Sharing the handler made stopd signal bits 13..0 of
   its word. Fixed: op_stopd added, 0x140 routed to it.
2. SPU_WrEventAck recomputed the count as "(events & mask) != 0" after the ack,
   dropping an outstanding count. RPCS3's atomic_op returns "!!events.count"
   without writing it (SPUThread.cpp:6596). Fixed: event_ack keeps the count.
3. SPU_WrEventMask cleared/ignored an unread count. RPCS3 preserves it
   (SPUThread.cpp:6562). Fixed: event_write_mask.
4. The decrementer freeze condition was tested before the ack but used the
   post-ack mask; RPCS3 captures read_dec() at the ack
   (SPUThread.cpp:6588-6595). Fixed: event_ack_freezes_decrementer, called before
   the ack.
5. SPU_EVENT_MB (0x10) was never raised. RPCS3 sets it when the inbound mailbox
   goes from empty to non-empty (SPUThread.cpp:2094). Fixed: push_in_mbox.
6. MFC_WrTagUpdate ignored its argument's meaning: it set tag_stat = tag_mask
   unconditionally, so MFC_TAG_UPDATE_ALL resolved like ANY and an
   out-of-range value was accepted as ALL. RPCS3 distinguishes IMMEDIATE / ANY /
   ALL and re-tests on command completion and mask writes
   (SPUThread.cpp:6446-6478, 6420-6440, 3965-3980). Fixed: TagState in
   runtime/include/spu/channel_state.h, wired into thread.cpp and the four
   host adapters (jobsys/mfc.h, shadow.cpp, verify.cpp, edge_exec.cpp).
7. A stalled DMA list was not tracked as an outstanding command: the tag was
   reported complete while half the elements were pending. RPCS3 keeps the
   command in mfc_queue with the stall bit and clears the tag from
   get_mfc_completed until MFC_WrListStallAck (SPUThread.cpp:3465-3480, 4202).
   Fixed: mfc_queue counter and TagState::fence, decremented/resolved in ch26.
8. RCHCNT(MFC_Cmd) returned a constant 16. RPCS3 returns 16 - mfc_size
   (SPUThread.cpp:5529). Fixed: mfc_queued_count().
9. A mask naming an undeliverable event was silently accepted. RPCS3 throws
   (SPUThread.cpp:5406). Fixed: event_unimplemented() plus a named error.

### Unit test: one test per deviation

tests/spu_channel_conformance.cpp runs each instruction through spu::step, i.e.
through the real dispatch table, so the stopd routing defect fails the test
rather than passing it. Raw output:

```
$ ./build-T3/spu_channel_conformance
spu_channel_conformance: 64 checks, 0 failures
exit=0
```

Existing suites, same build:

```
$ for t in spu_channel_conformance spurs memory_map memspace spu_aot_variants lv2_enosys reservation_xproc; do ./$t; done
spu_channel_conformance exit=0  spu_channel_conformance: 64 checks, 0 failures
spurs exit=0  spurs: 12 checks, 0 failures
memory_map exit=0  memory: 22 checks passed (0 failed)
memspace exit=0  memspace: 2 checks, 0 failures
spu_aot_variants exit=0  spu_aot_variants: 9 checks, 0 failures
lv2_enosys exit=0  lv2: 451 listed, 180 implemented (39.9%), 3 checks
reservation_xproc exit=0  4 checks, 0 failures
```

### The abort still reproduces with all nine fixes (this is the measurement)

```
$ PS3BASE_LOG=info timeout 130 ./ps3base --no-launcher
E  95.095526 spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET cmd 0x40 unmapped ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428
E  95.095675 spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET cmd 0x40 unmapped ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06510
E  95.095733 spu: thread 0x200 unknown stop code 0x0 at 0x1a100
I  95.105498 tty: [0] The SPURS is aborted.
I  95.105527 tty: [0] process: _sys_process_exit(0x8001000c)
I  95.105894 ppu: process exit -2147418100 from thread 0x1000004 lr=0x3000c7e8 (not lifted)
--- flips
I  91.843128 rsx: flip 2641 (buffer 0) worst 38.1 ms
I  95.008395 rsx: flip 2701 (buffer 0) worst 661.1 ms
```

Flips rose from 2341 (baseline, abort at 86 s) to 2701, and the abort moved from
86 s to 95 s, but the failing path is unchanged. The channel fixes are real and
tested; they are not the leaf. The leaf is that something advances the SPU's
frame-allocator bump pointer at 0x19e90 into a region that was never initialised
(0x23800), so the DMA list it builds has null EAs and the SPU jumps to zeroed
LS. That is outside the channel layer and needs the allocator's initialisation
and the list-build caller traced, which T1's block dump names directly.
### Second run of the same binary (timing varies, signature does not)

```
E  81.732703 spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET cmd 0x40 unmapped ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428
E  81.732770 spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET cmd 0x40 unmapped ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06510
E  81.732837 spu: thread 0x200 unknown stop code 0x0 at 0x1a100
I  81.735276 tty: [0] The SPURS is aborted.
I  80.593093 rsx: flip 2281 (buffer 0)
I  81.735292 process: _sys_process_exit(0x8001000c)
I  81.735330 ppu: process exit -2147418100 from thread 0x1000004 lr=0x3000c7e8 (not lifted)
```

Three runs of the fixed binary abort at 95.1 s (flips 2701), 81.7 s (flips 2281),
and the pre-fix binary at 86.0 s (flips 2341). Same pc, same lsa, same null ea
every time; only the wall-clock point varies. That is a timing-dependent guest
program reaching the same bad state, not a race in the channel layer.

### Conformance table -- every channel the SPURS kernel and libsre's policies use

Legend: OK = matches RPCS3; FIXED = deviated, corrected here with a test;
NAMED = deviates by design and logs its name. RPCS3 line numbers are from
refs/rpcs3 (symlinked from /root/Dev/rpcs3).

| ch | name                    | RPCS3 spec                                    | ps3base before        | now      |
|----|-------------------------|-----------------------------------------------|-----------------------|----------|
| 0  | RdEventStat             | :5717 masked bits, clears count               | OK                    | OK       |
| 1  | WrEventMask             | :6549 preserve unread count                   | dropped it            | FIXED    |
| 2  | WrEventAck              | :6581 keep count; TM-freeze at ack            | recomputed, dropped   | FIXED    |
| 3  | RdSigNotify1            | :5664 pop_wait                                | OK                    | OK       |
| 4  | RdSigNotify2            | :5669 pop_wait                                | OK                    | OK       |
| 7  | WrDec                   | :6540 re-base start, unfreeze                 | OK                    | OK       |
| 8  | RdDec                   | :5698 value - elapsed                         | OK                    | OK       |
| 9  | WrMSSyncReq             | :5518 count 1, write ignored                  | OK                    | OK       |
| 11 | RdEventMask             | :5712 mask                                    | OK                    | OK       |
| 12 | RdTagMask               | :5656 ch_tag_mask                             | OK                    | OK       |
| 13 | RdMachStat              | :6179 ie | (isolated<<1)                      | OK (ie only)          | OK       |
| 14 | WrSRR0                  | :6195 value & 0x3fffc                         | OK                    | OK       |
| 15 | RdSRR0                  | :5591 srr0                                    | OK                    | OK       |
| 16 | MFC_LSA                 | :6480 raw                                     | OK                    | OK       |
| 17 | MFC_EAH                 | :6485 stored, unused (32-bit EA)              | OK                    | OK       |
| 18 | MFC_EAL                 | :6490 raw                                     | OK                    | OK       |
| 19 | MFC_Size                | :6495 min(value,0xffff)                       | OK                    | OK       |
| 20 | MFC_TagID               | :6500 & 0x1f in RPCS3 process path           | OK                    | OK       |
| 21 | MFC_Cmd                 | :4970 dispatch; RCHCNT 16-queued :5529        | count constant 16     | FIXED    |
| 22 | MFC_WrTagMask           | :6420 re-test armed update                    | no re-test            | FIXED    |
| 23 | MFC_WrTagUpdate         | :6446 IMMEDIATE/ANY/ALL distinct              | all behaved as IMMED. | FIXED    |
| 24 | MFC_RdTagStat           | :5652 pop + clear count                       | OK                    | OK       |
| 25 | MFC_RdListStallStat     | :3472 rotl(1,tag) | stat                    | OK                    | OK       |
| 26 | MFC_WrListStallAck      | :4965 clear stall, resume, complete           | resumed, tag already  |
|    |                         |                                               | reported done         | FIXED    |
| 27 | MFC_RdAtomicStat        | :5675 0/1/2/4 per MFC.h:38-45                 | OK                    | OK       |
| 28 | WrOutMbox               | :6402 push_wait                               | OK                    | OK       |
| 29 | RdInMbox                | :5595 pop_wait                                | OK                    | OK       |
| 30 | WrOutIntrMbox           | :6201 code<64 send, <128 throw, 128/192 flag  | OK                    | OK       |
| 69 | Set_Bkmk_Tag             | :6610 no-op                                   | OK                    | OK       |
| 70 | PM_Start_Ev              | :6610 no-op                                   | OK                    | OK       |
| 71 | PM_Stop_Ev               | :6610 no-op                                   | OK                    | OK       |

DMA lists (commands 0x24/25/26, 0x34/35/36, 0x44/45/46): element parse, stall bit
0x80, ts mask 0x7fff, lsa advance align(ts,16), stall-and-notify event SN and
stall stat all match RPCS3 do_list_transfer (:2927-3480). The FIXED item is the
completion bookkeeping only (row 26).

Atomics: GETLLAR 0xD0 -> stat 4, PUTLLC 0xB4 -> 0 or 1 plus LR event on failure,
PUTLLUC 0xB0 -> stat 2, PUTQLLUC 0xB8 -> no stat. Matches MFC.h:38-45 and
SPUThread.cpp:4849/4901/4929.

Instructions:

| op    | RPCS3 spec                          | ps3base before      | now   |
|-------|-------------------------------------|---------------------|-------|
| iret  | SPUInterpreter.cpp:615 srr0 + e/d   | OK                  | OK    |
| bisled| :618 unconditional link, branch on count | OK            | OK    |
| stop  | :159 word & 0x3fff                  | OK                  | OK    |
| stopd | :579 always 0x3fff                  | routed to op_stop   | FIXED |

The e/d bits are 18 and 19 of the word (SPUOpcodes.h:15-16), which the
interpreter already used; the test now pins them.
