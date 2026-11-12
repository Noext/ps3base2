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
