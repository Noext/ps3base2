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
