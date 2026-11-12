# T4 -- visibility: lift 0x3000c7e8 (the SPURS abort path)

Branch explore/t4, worktree /root/Dev/wt-T4, base commit b1ba5ee.

## Measurements

### M1 -- the abort at HEAD, reproduced on this worktree's build (b1ba5ee + build-T4)

Command:

    cmake -S /root/Dev/wt-T4 -B /root/Dev/wt-T4/build-T4 -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      -DPS3BASE_GENERATED_DIR=/root/Dev/ps3base2/out-rdr/ppu \
      -DGOWA_SPU_GENERATED_DIR=/root/Dev/ps3base2/out-rdr/spu \
      -DPS3BASE_SPU_NATIVE_GEN_DIR=/root/Dev/ps3base2/out-rdr/gen/spu
    cmake --build /root/Dev/wt-T4/build-T4 -j 8 --target ps3base        # done=0
    nohup setsid env DISPLAY=:919 XDG_RUNTIME_DIR=/run/user/0 \
      PS3BASE_DISC_DIR=/root/Dev/rdr-port/vfs/dev_bdvd PS3BASE_LOG=info \
      timeout 110 /root/Dev/wt-T4/build-T4/ps3base --no-launcher > /tmp/T4.log 2>&1 < /dev/null & disown

Raw log, /tmp/T4.log:1823-1950 (verbatim):

    E 108.585986 [1702359] spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET cmd 0x40 unmapped ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428
    E 108.586030 [1702359] spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET cmd 0x40 unmapped ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06510
    I 108.586113 [1702359] spu: thread 0x200 'SecondaryCellSpursKernel0' stopped at 0x1a104
    ...
    I 108.589527 [1702358] tty: [0]   SPURS instance 0x01646400
    I 108.589549 [1702358] tty: [0]   SPU number     0
    I 108.589570 [1702358] tty: [0]   policy module  0x32023680
    I 108.589605 [1702358] tty: [0]   workload ID    1
    I 108.589637 [1702358] tty: [0] Call Stack Trace:	0x3200cb24
    I 108.589662 [1702358] tty: [0]                  	0x32009754
    I 108.589680 [1702358] tty: [0]                  	0x0
    I 108.589704 [1702358] tty: [0] The SPURS is aborted.
    I 108.589720 [1702358] process: _sys_process_exit(0x8001000c)
    I 108.590207 [1702358] ppu: process exit -2147418100 from thread 0x1000004 lr=0x3000c7e8 (not lifted)

51 "rsx: flip" lines, last flip 2761 at 97.3 s (the run was still alive at the
110 s timeout on this pass; the 2161-flip/77.9 s figure in the task text is the
timing variability of the same state).

### M2 -- the abort path resolves, and 0x3000c7e8 is INSIDE a lifted function

The report said "(not lifted)". It is not unlifted; it is not an *entry*. The
lifter's own discover() puts 0x3000c7e8 inside f_3000c790's span:

    $ python3 /tmp/T4-resolve.py            # analysis.discover() on the relocated modules
    liblv2.elf funcs 330 text 0x30000000 0x30013b68
      3000c7e8 -> inside f_3000c790 span 3000c790-3000c820
      3000c790 -> inside f_3000c790 span 3000c790-3000c820
      3000c808 -> inside f_3000c790 span 3000c790-3000c820
    libsre.elf funcs 352 text 0x32000000 0x3202e000
      3200cb24 -> inside f_3200cab8 span 3200cab8-3200cb3c
      32009754 -> inside f_3200930c span 3200930c-32009758
      3200cb40 -> inside f_3200cb40 span 3200cb40-3200cbf0
      3200975c -> inside f_3200975c span 3200975c-32009798

and the unmodified lift already contains the function:

    $ grep -n 'f_3000c790' /root/Dev/ps3base2/out-rdr/ppu/table.cpp
    74668:    {0x3000c790, f_3000c790},

The word at 0x3000c7e8 is the instruction AFTER `bl 0x3000fad0` at 0x3000c7e4:
the return address. has_code_at() is keyed by function ENTRY, so it answered
false for every return address in the guest. That is the whole "not lifted"
misreport -- not a missing function.

The two Call Stack Trace frames are the same shape: 0x3200cb24 is
f_3200cab8+0x6c (the instruction after `bl 0x3200e5c4` at 0x3200cb20), and
0x32009754 is f_3200930c+0x448. Both functions are lifted at HEAD.

### M3 -- the seed form: (image, offset), resolved at lift time

An absolute seed is a fact about ONE layout. layout_for.py recomputes every
module base from the title's text extent, so liblv2 moved from 0x21000000 to
0x30000000 in this tree's history and every absolute seed in the file then named
the wrong image. seed_is_stale() could only drop them and ask for a re-run.

The new form is `<image>+<offset>`, resolved by the lifter against the base THIS
lift actually gave that image:

    out-T4rdr/gen/landmarks.txt:
    liblv2+0xc7e8 spurs-abort-tail
    libsre+0xcb24 spurs-abort-frame0
    libsre+0x9754 spurs-abort-frame1

Lift output (out-T4rdr/lift.log), verbatim:

    landmarks: 3 from /root/Dev/wt-T4/out-T4rdr/gen/landmarks.txt
    liblv2           landmark liblv2+0xc7e8 -> 0x3000c7e8 in f_3000c790+0x58
    libsre           landmark libsre+0xcb24 -> 0x3200cb24 in f_3200cab8+0x6c
    libsre           landmark libsre+0x9754 -> 0x32009754 in f_3200930c+0x448

The lift is otherwise BYTE-IDENTICAL to the tree at HEAD -- only table.cpp
differs, because the landmark and span tables live there:

    $ diff -rq /root/Dev/ps3base2/out-rdr/ppu /root/Dev/wt-T4/out-T4rdr/ppu
    Files /root/Dev/ps3base2/out-rdr/ppu/table.cpp and /root/Dev/wt-T4/out-T4rdr/ppu/table.cpp differ

    76793 functions both sides (function_count, and the FuncEntry diff is empty)

### M4 -- the process-exit line now says what the abort path is

Build with -DPS3BASE_GENERATED_DIR=/root/Dev/wt-T4/out-T4rdr/ppu, run on
display :919, same disc, same env. Raw, /tmp/T4-run2.log:2224-2230:

    I  89.539820 [1777965] tty: [0] Call Stack Trace:	0x3200cb24
    I  89.539837 [1777965] tty: [0]                  	0x32009754
    I  89.539855 [1777965] tty: [0]                  	0x0
    I  89.539878 [1777965] tty: [0] The SPURS is aborted.
    I  89.539888 [1777965] tty: [0] 
    I  89.539894 [1777965] process: _sys_process_exit(0x8001000c)
    I  89.540201 [1777965] ppu: process exit -2147418100 from thread 0x1000004 lr=0x3000c7e8 liblv2+0xc7e8 spurs-abort-tail +0x3000c790

Before (same build, HEAD tables):

    I 108.590207 [1702358] ppu: process exit -2147418100 from thread 0x1000004 lr=0x3000c7e8 (not lifted)

The "+0x3000c790" tail is a format bug (the entry was printed with a "+0x%x"
prefix instead of as f_%08x); fixed in the next build, which also prints the
export symbol name.

