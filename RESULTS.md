# U1 -- SPU DMA list conformance (GETL/GETLB/GETLF, PUTL/PUTLB/PUTLF)

Branch explore/u1, worktree /root/Dev/wt-U1, from main HEAD f70a133.

## Measurements

### M1 -- the build and a baseline run on this worktree (from f70a133 + build-20)

Command:

    cmd bash -c "ln -sfn /root/Dev/ps3base2/bin bin; ln -sfn /root/Dev/ps3base2/fw fw; ..."
    cmake -S /root/Dev/wt-U1 -B /root/Dev/wt-U1/build-20 -G Ninja
    cmake --build /root/Dev/wt-U1/build-20 -j 8 --target ps3base        # EXIT=0
    nohup setsid env DISPLAY=:920 XDG_RUNTIME_DIR=/run/user/0 PS3BASE_DISC_DIR=/root/Dev/rdr-port/vfs/dev_bdvd PS3BASE_LOG=info timeout 120 /root/Dev/wt-U1/build-20/ps3base --no-launcher > /tmp/U1-base.log 2>&1 < /dev/null & disown

Raw tail:

    I 119.127252 [2132684] rsx: flip 3301 (buffer 0) worst 49.6 ms, >50ms 0, >100ms 0

    $ grep -c 'rsx: flip' /tmp/U1-base.log
    3301
    $ grep -c '^E ' /tmp/U1-base.log
    0

The fault needs a longer window than 120 s on this pass; M3 is the run that
reaches it. The worktree also needed an out-rdr symlink, without which:

    E 1.041477 [2124716] loader: cannot read ./out-rdr/gen/reloc/liblv2.elf

### M2 -- the title's list traffic, measured: 32791 lists, 122478 elements, ZERO notify bits

The walker is instrumented (PS3BASE_SPU_LIST_TRACE=1) and logs every list
command and every element verbatim:

    env PS3BASE_LOG=info PS3BASE_SPU_LIST_TRACE=1 timeout 120 ./build-20/ps3base --no-launcher > /tmp/U1-list.log

Raw counts:

    $ grep -c 'ENTER' /tmp/U1-list.log
    32791
    $ grep -c '] @0x' /tmp/U1-list.log
    122478
    $ grep -c 'notify=1' /tmp/U1-list.log
    0
    $ grep -c 'STALL after' /tmp/U1-list.log
    0
    $ grep -c 'ACK tag' /tmp/U1-list.log
    0

Lists issued, by opcode and thread:

    $ grep -o 'ENTER thread 0x[0-9a-f]* cmd 0x[0-9a-f]*' /tmp/U1-list.log | awk '{print $3,$5}' | sort | uniq -c | sort -rn
      14223 0x4000100 0x44
      10037 0x4000100 0x46
       9784 0x2000100 0x46
       9744 0x1000100 0x46
       9621 0x3000100 0x46
       4289 0x100     0x46

    0x44 = MFC_GETL, 0x46 = MFC_GETLF (MFC.h:14). No PUTL/PUTLB/PUTLF from this
    title, and no list command at all on thread 0x200 (the thread that faults).

Element size and external-address distribution:

    $ grep -o 'size=0x[0-9a-f]*' /tmp/U1-list.log | sort | uniq -c | sort -rn | head -5
      65140 size=0x0
      14474 size=0x4000
      14166 size=0x20
       3267 size=0x28c0
       3266 size=0x60
    $ grep -o 'eal=0x[0-9a-f]*' /tmp/U1-list.log | sort | uniq -c | sort -rn | head -3
      65322 eal=0x00000000
       5512 eal=0x703830b0
       3281 eal=0x7077df00

**Fact 1: the notify bit is never set.** Zero of 122478 elements carry it, over
32791 list commands on every SPU thread that runs. Stall-and-notify is not
exercised by this title, so its correctness cannot be the fault. The decode is
checked against the raw bytes, not against itself: in the local-storage dump

     list 0x3f730 element 0: 01 02 04 08 10 20 40 80  -> notify=0, size 0x408, eal 0x10204080
     list 0x2add0 element 0: 00 00 00 00 00 00 00 00  -> notify=0, size 0x0,   eal 0x0

and re-scanning every 8-byte element of all 151 distinct (base,size) pairs the
trace names, inside the dumped local storage, finds 0 elements with the bit set:

    distinct list (base,size) from the trace: 151  elements with the notify bit in the final LS dump: 0

**Fact 2: the zero-EAL elements are the guest's own padding, not a stale
ahead-of-time read.** 65140 of 122478 elements are size 0x0 / eal 0x0, and 6183
lists contain a zero-size element FOLLOWED by a non-zero one:

    (51648, [(0, 0, 0), (1, 3440, 1955418080)])
    (51648, [(6, 0, 0), (7, 8720, 1955421520)])
    (51648, [(12, 0, 0), (13, 16, 1955430240)])
    (51649, [(0, 0, 0), (1, 64, 1955409024)])

A walker that decoded elements ahead of time would produce the opposite shape:
valid elements first and zeros only for elements written after a stall. There is
no stall to be ahead of, and the zeros are interleaved with valid elements
throughout the list. The guest issues a longer list than it fills and relies on
the unfilled elements being zero -- which is what RPCS3 does and what this
walker does.

No element anywhere in the run has a non-zero size with an eal below 0x10000:

    $ python3 - ... # every 'size=0x.. eal=0x..' line, count size>0 and eal<0x10000
    count 0

### M3 -- who wrote the faulting EAL: the fault is a PLAIN GET, not a list

The MFC parameter channels are now recorded (PS3BASE_SPU_MFC_HIST=1): the last
8 writes to ch16/ch17/ch18/ch19/ch21 with their pc, dumped at a refused DMA.

    env PS3BASE_LOG=info PS3BASE_SPU_MFC_HIST=1 timeout 120 ./build-20/ps3base --no-launcher > /tmp/U1-hist.log
    $ grep -c 'rsx: flip' /tmp/U1-hist.log
    55

Raw log, /tmp/U1-hist.log:3157-3175 (verbatim):

    E 116.644196 [2173754] spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET cmd 0x40 unmapped ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428
    E 116.644328 [2173754] spu:   -1: ch19 <- 0x00000080 at pc 0x06410
    E 116.644336 [2173754] spu:   -2: ch18 <- 0x00000000 at pc 0x06408
    E 116.644342 [2173754] spu:   -3: ch16 <- 0x0001a100 at pc 0x06400
    E 116.644349 [2173754] spu:   -4: ch19 <- 0x00000930 at pc 0x068ec
    E 116.644356 [2173754] spu:   -5: ch18 <- 0x70c89400 at pc 0x068e8
    E 116.644362 [2173754] spu:   -6: ch16 <- 0x0003b100 at pc 0x068bc
    E 116.644368 [2173754] spu:   -7: ch19 <- 0x00000490 at pc 0x068b0
    E 116.644451 [2173754] spu:   -8: ch18 <- 0x70c6de00 at pc 0x068ac
    E 116.644462 [2173754] spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET cmd 0x40 unmapped ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06510
    E 116.644468 [2173754] spu:   -1: ch19 <- 0x00000080 at pc 0x06508
    E 116.644474 [2173754] spu:   -2: ch18 <- 0x00000000 at pc 0x06500
    E 116.644481 [2173754] spu:   -3: ch16 <- 0x0001a100 at pc 0x064f0
    E 116.644487 [2173754] spu:   -4: ch19 <- 0x00000080 at pc 0x06410
    E 116.644493 [2173754] spu:   -5: ch18 <- 0x00000000 at pc 0x06408
    E 116.644499 [2173754] spu:   -6: ch16 <- 0x0001a100 at pc 0x06400
    E 116.644505 [2173754] spu:   -7: ch19 <- 0x00000930 at pc 0x068ec
    E 116.644513 [2173754] spu:   -8: ch18 <- 0x70c89400 at pc 0x068e8
    E 116.644654 [2173754] spu: thread 0x200 unknown stop code 0x0 at 0x1a100

The command byte is 0x40 -- MFC_GET, a single transfer -- and the channel
write immediately before it is ch21 <- 0x00000040 at that pc. It is NOT a list
command (0x44/0x45/0x46), so the list walker is not in this path at all. The
sequence is fully determined by the guest:

    pc 0x06400  ch16 <- 0x0001a100      # LSA = the scratch buffer
    pc 0x06408  ch18 <- 0x00000000      # EAL = 0
    pc 0x06410  ch19 <- 0x00000080      # size = 0x80
    pc 0x06424  ch21 <- 0x00000040      # GET

Disassembly of the issuing code, from the local-storage dump at the refused DMA
(spu_unmapped_00000200.ls):

    063f0: 40804006  il r6,0x80
    063f4: 24ffc0d0  stqd r80,-16(r1)
    063f8: 40802005  il r5,0x40
    063fc: 21a00807  wrch ch16,r7
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

r4 is the EAL and r7 the LSA: they are the function's ARGUMENTS. The il r4,0x0
at 0x06408 is r4 reused as the size register for the NEXT command; the EAL is
whatever the caller passed, and the raw local storage shows r4 = 0 entering the
function. 0x06428 onward continues into the frame-allocator bump pointer, which
is why the fault and the bump pointer look related: the routine fetches, then
bumps.

The bump pointer at the fault, from the same dump:

    LS size 262144
    0x19e90 (bump ptr) = 0x0001af90
    0x19e80..0x19ea0: 0x00007fe0 0x00007fe0 0x00007fe0 0x00007fe0 0x0001af90 0x0001abd0 0x19da7bd0 0x0001abd0
    0x1a100..0x1a140: 0x00000000 ... (all zero)
    nonzero 128-byte lines: 28
    first 40 runs: [('0x80','0x880'), ('0xa00','0x19d00'), ('0x19e80','0x19f00'), ('0x1a080','0x1a100'), ('0x1a300','0x1a380'), ('0x1e100','0x1e200'), ('0x1e900','0x1ea80'), ...]

0x1af90 is inside a written region (0x1a300..0x1a380 is non-zero in the dump),
unlike the 0x23800 in the task text. Local storage in this run has only 28
non-zero 128-byte lines and 0x1a100, the fault's LSA, is not one of them: the
destination was zero and stayed zero.

### M4 -- the caller passed the null EAL: pc 0x06400 enters with r4 = 0

PS3BASE_SPU_PC_TRAP=<pc> records the argument registers at one instruction, in
a ring dumped at a refused DMA. Trapping the faulting routine's entry
(pc 0x06400):

    env PS3BASE_LOG=info PS3BASE_SPU_PC_TRAP=0x6400 PS3BASE_SPU_MFC_HIST=1 timeout 150 ./build-20/ps3base --no-launcher > /tmp/U1-trap3.log
    $ grep -c 'rsx: flip' /tmp/U1-trap3.log
    57

Raw log, /tmp/U1-trap3.log:3146-3152:

    E 126.027583 [2224541] spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET cmd 0x40 unmapped ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428
    E 126.027645 [2224541] spu:   pc 0x06400 ch16 <- 0x0001a100 r3 0x1a100 r4 0x00000000 r0 0x06b34
    E 126.027653 [2224541] spu:   -1: ch19 <- 0x00000080 at pc 0x06410
    E 126.027660 [2224541] spu:   -2: ch18 <- 0x00000000 at pc 0x06408
    E 126.027666 [2224541] spu:   -3: ch16 <- 0x0001a100 at pc 0x06400
    E 126.027672 [2224541] spu:   -4: ch19 <- 0x00000930 at pc 0x068ec
    E 126.027679 [2224541] spu:   -5: ch18 <- 0x70c90b70 at pc 0x068e8
    E 126.027685 [2224541] spu:   -6: ch16 <- 0x0003b100 at pc 0x068bc

r3 = 0x1a100 (the LSA argument) and r4 = 0x00000000 (the EAL argument), with
r0 = 0x6b34, the return address. The null external address is not produced
inside the routine: it is PASSED IN.

The call site, from the disassembly:

    06b20: 42d08003  ila r3,0x1a100       # LSA
    06b24: 33dee7b5  lqr r53,0x36260      # a 16-byte table entry
    06b28: 408000e8  il r104,0x1
    06b2c: 3f811a84  rotqbyi r4,r53,4     # r4 = the entry's 3rd word
    06b30: 337f1700  brsl r0,0x063e8      # call the fetch-and-bump routine
    06b34: 327fba00  br r0,0x06904

rotqbyi r4,r53,4 moves word 2 of the entry into r4, so the EAL is
ls32[0x36268]. In the dump taken at the fault:

    0x36260: 0x00000000 0x00000000 0x70dc9000 0x70dd8000
    0x36270: 0x00000005 0x00000000 0x0177cbc0 0x00000000

Word 2 is 0x70dc9000, NOT zero -- and no store to 0x362xx appears anywhere in
the 26 KB of disassembly:

    $ grep -nE 'stqr .*0x362|stqd .*0x362|stqa .*0x362' /tmp/U1-dis.txt
    (no output)

The guest reads a table entry that this runtime's disassembled code never
writes, and passes whatever is there as its EAL. The register carried 0 while
the same address in the dump holds 0x70dc9000 at a different moment, so the
entry is filled by a transfer the local-storage dump cannot see (an SPU-to-SPU
or PPU-to-SPU write). That is where the null EAL comes from -- not the list
walker.

No element anywhere in the run carries a non-zero size with an eal below
0x10000, so the faulting transfer is not a list element either.

### M5 -- the audit result: the walker matches RPCS3 on every clause, and it is not on the fault path

The walker was rewritten to the specification and moved into
runtime/include/spu/dma_list.h so the unit test drives the shipped code. Each
clause, against RPCS3 do_list_transfer (refs/rpcs3/rpcs3/Emu/Cell/SPUThread.cpp:2927):

| clause | specification | RPCS3 | before | after |
|---|---|---|---|---|
| element is 8 bytes, big-endian {notify:1,reserved:16,size:15,eal:32} | SPU ISA | 2939 struct alignas(8) list_element | correct | correct |
| size = word0 & 0x7FFF, notify = high bit of word0 | SPU ISA | 2970 ts_mask = 0x7fff; 3460 sb & 0x80 | correct | correct |
| list base 8-byte aligned, masked | SPU ISA | 2957 item_ptr = eal & 0x3fff8 | correct | correct |
| lsa masked to 16 | SPU ISA | 2958 arg_lsa = lsa & 0x3fff0 | correct | correct |
| lsa advances by each element's size, rounded to 16 | SPU ISA | 3390 lsa = arg_lsa + (eal & 0xf); arg_lsa += align(size,16) | correct | correct |
| a notify bit on the LAST element does not stall | 3455 breaks on !arg_size BEFORE 3460 | 3451-3460 | correct | correct |
| stall resumes by RE-READING the following elements at that moment | SPU ISA | 2957 reads item_ptr every refill; 5463 re-enters | correct | correct |
| one queue entry per list across every stall | 5463 does not re-enqueue | 3460-3478, 5529 | **WRONG** | fixed |
| stall status is one bit per tag | SPU ISA | 3472 set_value(rotl(1, tag) | get_value()) | correct | correct |
| list size above 0x4000 refused | 5041 `if (ch_mfc_cmd.size <= 0x4000)` | 5041 | **missing** | fixed |
| MFC_RdListStallStat returns the tag bits and clears the count | SPU ISA | 5688-5695 | correct | correct |

Two real deviations, both fixed:

1. **The queue slot was taken on every stall, not once per command.** The old
   code did `mfc_queue++` on each stall and `mfc_queue--` only when a resume
   finished the list, so a list that stalls N times holds N slots. RCHCNT on
   MFC_Cmd is 16 - mfc_size and the SPURS kernel reads it before issuing its
   next command, so a leaked slot eventually stops the kernel issuing at all.
   The list that needs this most is the one that stalls repeatedly, which is
   exactly the list a frame allocator builds.
2. **A list command larger than 0x4000 bytes was walked anyway.** RPCS3
   refuses it (5041). 0x4000 bytes is the 2048-element maximum; walking past it
   reads local storage the guest never intended as a list, and each such
   element is a transfer the guest never asked for.

Neither deviation is on the fault path: the faulting command is 0x40 (MFC_GET),
not 0x44/0x45/0x46, and no notify bit is ever set (M2), so the stall path never
runs in this title.

### M6 -- the unit test, including the required case: element 1 filled AFTER the stall

tests/spu_dma_list.cpp, built as the spu_dma_list CTest target:

    $ ./build-20/spu_dma_list
    64 checks, 0 failure(s)

The decisive case builds a list at LS 0x1a100 whose element 0 carries the
notify bit and whose element 1 is all zero at the time of the walk -- the same
addresses and the same shape as the fault. It then asserts, in order:

* only element 0 transfers before the stall, and it is the one the guest wrote;
* the stalled command holds exactly one queue slot;
* the guest then fills element 1 (== the 0x80 bytes element 0 wrote are
  inspected, element 1 is written at 0x1a108 with a NEW eal derived from them);
* the acknowledgement resumes with list = 0x1a108 and lsa = 0x2080;
* the resumed walk transfers element 1 with the eal written AFTER the stall;
* element 2, never written, is seen as eal 0 / size 0;
* and, so the test cannot pass vacuously, element 1 read BEFORE the stall is
  asserted to be eal 0 / size 0 -- the unmapped-0 transfer -- and asserted to
  differ from what the resume returns.

A walker that decoded ahead of time or cached elements passes every other check
and fails this one.

### M4 CORRECTION -- the misleading disassembly decode, corrected by measurement

The paragraph below the call site in M4 originally claimed rotqbyi r4,r53,4
moves word 2 of the entry into r4 and that the EAL therefore came from
ls32[0x36268]. That is wrong, and the correction matters because it is the
difference between "the table holds a valid pointer and the runtime lost it"
and "the table holds zero". SPU rotqbyi n is a rotate LEFT by n bytes:
result byte i = source byte (i + n) mod 16. Checked against the interpreter's
own helper (runtime/src/spu/ops_impl.h:330, qrot_bytes(a, n) = pshufb(a, iota-n)):

    $ clang++ -O1 /tmp/rot3.cpp -o /tmp/rot3 && /tmp/rot3
    rotqbyi  0: element3 (bytes 12..15) = 0x70dd8000
    rotqbyi  4: element3 (bytes 12..15) = 0x00000000
    rotqbyi  8: element3 (bytes 12..15) = 0x00000000
    rotqbyi 12: element3 (bytes 12..15) = 0x70dc9000

wrch reads GPR element 3, so rotqbyi r4,r53,4 yields the entry's word 0,
ls32[0x36260] -- which in the dump at the fault is:

    0x36260: 0x00000000 0x00000000 0x70dc9000 0x70dd8000

That is 0x00000000, exactly the r4 the trap measured. The decode is consistent
with the register, and there is no lost pointer: the table entry the guest read
held zero.

### M5 -- the table entry is zero, and no code in the image writes it

Nothing in the 26 KB disassembly of the thread's local storage stores to
0x3626x:

    $ grep -nE 'stqr .*0x362|stqd .*0x362|stqa .*0x362' /tmp/U1-dis.txt
    (no output)

and no list element writes there either (the trace records every element's
destination lsa, and none is in that range):

    $ grep -c 'lsa 0x362' /tmp/U1-list.log
    0

So 0x36260 is filled by a transfer the list trace does not cover: a single GET
or an SPU-to-SPU PUT. The next measurement targets exactly that.

### M7 -- the LS table entry is a faithful copy of guest memory, and guest memory holds zero

PS3BASE_SPU_LS_WATCH=<hex> logs every transfer whose LS destination overlays a
watched local address, which is what names the filler of the table:

    $ grep 'overlays 0x36260' /tmp/U1-lsw.log | head -6
    E   5.178269 [2320934] spu: thread 0x200 pc 0x025d0 PUT cmd 0x20 ea 0x017b2e00 -> lsa 0x33000 size 0x4000 overlays 0x36260
    E   5.189750 [2320934] spu: thread 0x200 pc 0x025d0 GET cmd 0x40 ea 0x017b2e00 -> lsa 0x33000 size 0x4000 overlays 0x36260
    E   5.189804 [2320934] spu: thread 0x200 pc 0x076c4 GET cmd 0x40 ea 0x0177cc00 -> lsa 0x36200 size 0x90 overlays 0x36260
    E   5.192338 [2320934] spu: thread 0x200 pc 0x025d0 PUT cmd 0x20 ea 0x017b2e00 -> lsa 0x33000 size 0x4000 overlays 0x36260
    E   5.198962 [2320934] spu: thread 0x200 pc 0x076c4 GET cmd 0x40 ea 0x0177cc00 -> lsa 0x36200 size 0x90 overlays 0x36260

The entry is copied by a plain GET: `pc 0x076c4 GET cmd 0x40 ea 0x0177cc00 ->
lsa 0x36200 size 0x90`. LS 0x36260 is at offset 0x60 into that 0x90-byte copy,
so the EAL comes from guest memory at 0x0177cc60. Dumped at the fault (the
fault handler now prints both sides):

    E 142.726969 [2330803] spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET cmd 0x40 unmapped ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428
    E 142.727385 [2330803] spu:   guest 0x0177cc00: 70ccfa80 017bfe80 00000060 70c6de00  00000000 70d68f80 70c89400 70d68b80
    E 142.727393 [2330803] spu:   guest 0x0177cc60: 00000000 00000000 70dc9000 70dd8000
    E 142.727411 [2330803] spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET cmd 0x40 unmapped ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06510
    E 142.727419 [2330803] spu:   guest 0x0177cc00: 70ccfa80 017bfe80 00000060 70c6de00  00000000 70d68f80 70c89400 70d68b80
    E 142.727425 [2330803] spu:   guest 0x0177cc60: 00000000 00000000 70dc9000 70dd8000

Guest memory at 0x0177cc60 is 00000000 00000000 70dc9000 70dd8000 -- the same
as the local-storage copy, so the copy is faithful and nothing is lost in
transit. Word 0 of that guest entry is 0x00000000, and rotqbyi r4,r53,4 puts
word 0 into r4 (M4 correction). The null really is the guest's own table
entry.

The neighbours in that table are valid (0x70ccfa80, 0x017bfe80, 0x70c6de00,
0x70d68f80, 0x70c89400, 0x70d68b80), so the entry is not uninitialised garbage:
it is a specific slot the guest left zero. Whether the guest should have filled
it before this read is a different question from the DMA list walker, and the
evidence here does not support answering it from the list path.

### M8 -- the two list fixes change no measured behaviour on this title

Final build, both fixes in, no instrumentation switches:

    $ ./build-20/spu_dma_list
    64 checks, 0 failure(s)
    $ ./build-20/spu_channel_conformance
    spu_channel_conformance: 64 checks, 0 failures

and the run reaches the same point as the unmodified HEAD build (M1: 3301
flips, no fault inside 120 s; the fault at 95-143 s depending on the added
inline cost of whichever diagnostic is enabled). The list fixes are
correctness fixes for a path this title does not take, which is exactly why
M2's zero notify-bit count matters: they cannot be the cause of the abort, and
they cannot be its cure either.
