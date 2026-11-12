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


### M5 -- what the abort path DOES (the lifted function, and the four calls it makes)

The lifted function at 0x3000c790 is liblv2's `_sys_panic`
(sysPrxForUser, NID 0x4232B0DB, OPD 0x30014200 -> code 0x3000c790; found by
scanning liblv2's data segment for {code, toc} pairs, 330 of them):

    $ python3 /tmp/T4-opd.py
    OPD count 330
    opd 30014200 -> code 3000c790
    opd 30014208 -> code 3000c824
    opd 30014210 -> code 3000c8ec
    opd 30014350 -> code 3000fad0
    opd 30014358 -> code 3000fb64

and the export table names it:

    $ grep -n '0x30014200' /root/Dev/ps3base2/out-rdr/ppu/table.cpp
    77337:    {"sysPrxForUser", 0x4232B0DBu, "_sys_panic", 1, 0x30014200},

Guest disassembly (capstone, from liblv2.elf at 0x3000c790):

    3000c790  stdu     r1, -0xa0(r1)          ; prologue, 0xa0-byte frame
    3000c794  mflr     r0
    3000c798  std      r28, 0x80(r1)
    3000c79c  std      r4, 0xd8(r1)           ; spill the varargs register save area
    3000c7a0  mr       r28, r3                ; r28 = code (the panic code)
    3000c7a4  std      r29, 0x88(r1)
    3000c7a8  std      r0, 0xb0(r1)
    3000c7ac  std      r5, 0xe0(r1)           ; ... r5..r10 spilled to 0xd8..0x108
    3000c7b0  std      r6, 0xe8(r1)
    3000c7b4  std      r7, 0xf0(r1)
    3000c7b8  std      r8, 0xf8(r1)
    3000c7bc  std      r9, 0x100(r1)
    3000c7c0  std      r10, 0x108(r1)
    3000c7c4  li       r4, 0
    3000c7c8  li       r3, 8                  ; syscall 8 = sys_tty_write? no: r3 is arg 0
    3000c7cc  li       r11, 0x3dc             ; syscall 988
    3000c7d0  sc                              ; <-- sys_dbg_register_ppu_exception_handler
    3000c7d4  addi     r29, r1, 0xd8          ; r29 = &register save area
    3000c7d8  clrldi   r3, r28, 0x20          ; arg0 = panic code
    3000c7dc  mr       r4, r29                ; arg1 = &save area
    3000c7e0  stw      r29, 0x70(r1)
    3000c7e4  bl       0x3000fad0             ; <-- the call whose return is 0x3000c7e8
    3000c7e8  nop                             ; <-- the reported "not lifted" address
    3000c7ec  lwz      r9, -0x7e7c(r2)        ; r2 = TOC; load the panic-lock
    3000c7f0  ld       r0, 0(r9)
    3000c7f4  clrlwi   r0, r0, 0x1f           ; bit 0
    3000c7f8  cmpwi    cr7, r0, 0
    3000c7fc  beq      cr7, 0x3000c808
    3000c800  trap                            ; already panicking: die immediately
    3000c804  b        0x3000c804             ; <-- infinite self-branch (hang), not a return
    3000c808  lis      r3, -0x7fff            ; r3 = 0x80010000
    3000c80c  li       r4, 0
    3000c810  ori      r3, r3, 0xc            ; r3 = 0x8001000c = CELL_EABORT
    3000c814  li       r5, 0
    3000c818  li       r11, 0x16              ; syscall 22 = _sys_process_exit
    3000c81c  sc                              ; <-- _sys_process_exit(0x8001000c)
    3000c820  b        0x3000c804             ; unreachable; the exit throws

So the abort path is:

  1. `_sys_panic(code, va_list)` spills the varargs save area and registers an
     exception handler with syscall 988 (which this runtime stubs to CELL_OK --
     correct, RPCS3's sys_lv2dbg.cpp:104 only logs).
  2. It calls 0x3000fad0 with (code, &save_area), which is liblv2's internal
     fprintf-to-tty: 0x3000fad0 -> 0x3000eccc (a handle lookup), then
     0x3000edc0 (the formatted write) with r3 = the tty FILE*, r4 = code,
     r5 = &save_area, r6 = 0. That call is what produces every "tty: [0] ..."
     line of the dump.
  3. On return it reads a global panic flag at TOC-0x7e7c. Set: `trap` and hang.
     Clear: it builds CELL_EABORT = 0x8001000c and calls _sys_process_exit(22).
  4. The self-branch at 0x3000c804 is why the run never returns from either arm:
     the process leaves through the syscall, and the runtime reports it as a
     ProcessExit.

The report's "0x8001000c is CELL_SPURS_TASK_ERROR_FATAL" is a coincidence of
values, not the same constant: RPCS3 defines CELL_SPURS_TASK_ERROR_FATAL as
0x80410914 (refs/rpcs3/rpcs3/Emu/Cell/Modules/cellSpurs.h:64), and 0x8001000C
is CELL_EABORT ("The operation is aborted", ErrorCodes.h:115). liblv2's
_sys_panic is what puts that value into r3; the SPURS code path reaches
_sys_panic, and _sys_panic exits with EABORT.

The lifted form is already correct -- it is in the tree at HEAD, table entry and
all. What was missing was only the NAME, which is what this task fixed.

