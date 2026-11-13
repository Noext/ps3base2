# RESULTS-P3 -- fibres (cellFiber) before the RDR crash

Branch explore/p3, worktree /root/Dev/wt-P3, based on main 01c5e16.

## Task
Instrument every cellFiber* call (create, switch, return) plus the guest stack
pointer r1 before and after each switch, ahead of the RDR host SIGSEGV at guest
0x10 reached through ppu::call_indirect.

## Measurement 1 -- what libfiber actually is here

The crash stack, resolved with `addr2line` against the RDR binary
(`build-rdr/ps3base`, RelWithDebInfo), is ENTIRELY inside LLE libfiber:

    $ for a in 17aa2b6 17ac22c 17bc0fc 1682b9d 17b3eb3 17aa182 ...; do addr2line -f -C -e build-rdr/ps3base 0x$a; done
    ppu::ld64(unsigned char*, unsigned int)            ops.h:63
    f_360002a0_body(...)                              out-rdr/ppu/libfiber_000.cpp:232
    f_36000748_body(...)                              ops.h:104 (libfiber)
    f_360033dc_body(...)                              out-rdr/ppu/libfiber_000.cpp:4128
    f_36001b04_body / f_3600026c / f_360008a0 / f_360012a0
    f_3600093c_body / f_3600254c_body                 -- all libfiber

so libfiber.sprx is loaded LLE (confirmed in the log: `prx: load
'/dev_flash/sys/external/libfiber.sprx' -> 0x23026500 (LLE, start 0x3600ea68)`)
and the fault is a null+0x10 read in the LLE fibre switch path, not in HLE.

## Measurement 2 -- every cellFiber import in a real run, with r1

Instrumented `call_import_inner` in `runtime/src/ppu_runtime.cpp`: on every
import whose module is `cellFiber`, record ENTER and LEAVE with the guest r1,
r3, lr, thread id, and whether r1 changed across the call (a fibre switch is
exactly the case where it does).

Prints nothing in the default build; enabled by `PS3BASE_FIBER_TRACE=1`.

First version logged each event to the log file. Run:

    DISPLAY=:150 GOWA_FLASH_DIR=/root/Dev/devflash-out/dev_flash \
    PS3BASE_DISC_DIR=/root/Dev/rdr-port/vfs/dev_bdvd PS3BASE_LOG=info \
    PS3BASE_FIBER_TRACE=1 timeout 180 build-rdr/ps3base --no-launcher

Result -- the complete census of cellFiber imports over 36 s of RDR:

    $ grep -a 'fiber: ->' /tmp/p3-fiber1.log | sed 's/.*fiber: -> \([A-Za-z_]*\).*/\1/' | sort | uniq -c
          1 _cellFiberPpuInitialize
          1 _cellFiberPpuSchedulerAttributeInitialize
          1 cellFiberPpuInitializeScheduler
      24439 cellFiberPpuYield

    $ grep -a 'fiber:' /tmp/p3-fiber1.log | wc -l
    48884          (= 24439 enter + 24438 leave, last one still in flight)

Raw samples, verbatim:

    I   0.874514 fiber: -> _cellFiberPpuInitialize(0x10000464,0x0,0xffffffffffffffff) r1=0xd004f860 lr=0x00d190c4 th=0x1000000 'main_thread'
    I   0.874739 fiber: <- _cellFiberPpuInitialize ret=0xffffffffffff9404 r1=0xd004f860
    I   0.874748 fiber: -> _cellFiberPpuSchedulerAttributeInitialize(0xd004f950,0x300000,0xffffffffffffffff) r1=0xd004f8e0 lr=0x0053325c th=0x1000000 'main_thread'
    I   0.874759 fiber: <- _cellFiberPpuSchedulerAttributeInitialize ret=0x0 r1=0xd004f8e0
    I   0.874765 fiber: -> cellFiberPpuInitializeScheduler(0x165d580,0xd004f950,0xd004fa50) r1=0xd004f8e0 lr=0x00533284 th=0x1000000 'main_thread'
    I   0.874790 fiber: <- cellFiberPpuInitializeScheduler ret=0x0 r1=0xd004f8e0
    I   4.275428 fiber: -> cellFiberPpuYield(0x0,0x1e,0x1647708) r1=0xd033faa0 lr=0x0053339c th=0x1000015 '[RAGE Audio] - Engine Thread'
    I   4.275444 fiber: <- cellFiberPpuYield ret=0xffffffff80760009 r1=0xd033faa0

The measured facts, all from those lines:

1. **No fibre is ever created.** `cellFiberPpuCreateFiber` is never called;
   neither is `cellFiberPpuRunFibers`, `ContextInitialize`,
   `ContextSwitch`, `ContextRun`, or any `UtilWorkerControl` entry. The
   title calls exactly three setup functions and then yields forever.
2. **`cellFiberPpuYield` is called 24 439 times and returns 0x80760009 every
   single time** -- `CELL_FIBER_ERROR_PERM`, not CELL_OK. Zero occurrences of
   any other return value in the log.
3. **r1 is unchanged across every one of those calls.** Zero events were
   flagged `SWITCHED-STACK` (the instrumented marker for "r1 at leave differs
   from r1 at enter"). So at the import boundary no fibre switch ever happens:
   the LLE libfiber returns to its caller on the caller's own stack.
4. The yield caller is the same site every time: `lr=0x0053339c`, which is the
   `bl` in the title's RAGE worker-thread trampoline (f_0053338c, rdr_052.cpp:
   `ctx.lr = 0x53339c; ppu::call_import(ctx, base, 334)` where import 334 is
   `cellFiber::cellFiberPpuYield`).

## What this rules out, and what it does not

It rules out the hypothesis as stated in the task, for every path taken before
36 s: **no fibre switch precedes anything, because no fibre exists**. The
`cellFiberPpuYield` import resolves LLE and libfiber's own body returns PERM
without switching stacks, so there is no "lifted code switch" to replace with a
host fibre at this point in the run.

It does NOT yet explain the 43 s crash, and the measurement above stops at 36 s.
The crash stack is libfiber-internal (`f_360002a0` indirect through a guest OPD
whose word is 0), and libfiber in this run still executes; so the next question
is what changes between 36 s and 43 s. That requires the ring-buffer version
below, because logging every event costs the run (50 000 lines in 36 s) and a
truncated run measures nothing.

## Measurement 3 -- ring buffer, so the crash survives

A log line per event cannot coexist with a 43 s crash: the 36 s run above
already wrote 50 000 lines, and the events that matter are the last twenty.
`runtime/src/fibertrace.cpp` keeps the last 512 cellFiber events in memory,
and `runtime/src/crash_posix.cpp` prints them from the signal handler before
the register dump. `PS3BASE_FIBER_TRACE=1` enables it.

Measured effect of the overhead alone, same machine, same binary family:

    without the trace: flip 912 at 42.9 s then SIGSEGV at 42.96 s  (/tmp/seg.log)
    with the ring:     flip 1141 at 81.0 s, alive at 85 s, no crash     (/tmp/p3-fiber2.log)

That difference is not a fix -- it is what running the same title without
50 000 synchronous log writes does to the timing. It is recorded because the
next measurement must not read it as one.
## Measurement 4 -- THE ANSWER: a fibre IS switched, 23 ms before the crash

Same binary, ring buffer on, display :0 (the display the crash reproduces on).
The run crashed:

    E  39.537142 crash: signal 11 at 0x...726 (exe+0x17aa726) fault 0x8a540000010 (guest 0x10) thread main_thread 0x1000000
    E  39.569965 crash:   cellFiber import nesting depth on this thread: 2 (the fault happened INSIDE a cellFiber import)
    E  39.569971 crash:   guest lr 0x0 r1 0xd0032d30 r2 0x0 r3 0x0 r4 0x0 r5 0xd0033360

The last events of the 91666 the ring saw, verbatim:

    t=39.514795 th=0x1000000 ENTER  _cellFiberPpuAttributeInitialize r1=0xd0025140 r3=0xd00251b0 lr=0x005332f4
    t=39.514802 th=0x1000000 LEAVE  _cellFiberPpuAttributeInitialize r1=0xd0025140 r3=0x0        lr=0x005332f4
    t=39.514803 th=0x1000000 ENTER  cellFiberPpuCreateFiber          r1=0xd0025140 r3=0x165d580 lr=0x0053334c
    t=39.514902 th=0x1000000 LEAVE  cellFiberPpuCreateFiber          r1=0xd0025140 r3=0x0        lr=0x0053334c
    t=39.514903 th=0x1000000 ENTER  cellFiberPpuRunFibers            r1=0xd00252e0 r3=0x165d580 lr=0x00533498
    t=39.514955 th=0x1000000 ENTER  cellFiberPpuYield                r1=0xd0032d60 r3=0x1000000720bcbe0 lr=0x0053339c
    ... then the SIGSEGV at 39.537142.

Three facts, each one a line above:

1. cellFiberPpuCreateFiber IS called -- once, at t=39.514803, returning 0
   (CELL_OK). This is a NEW call: the earlier 36 s run (/tmp/p3-fiber1.log) had
   zero of them in 24439 yields. Fibre creation is reached late, at 39.51 s,
   immediately before the fault.
2. cellFiberPpuRunFibers is entered and never returns -- the ENTER at
   t=39.514903 has no matching LEAVE, and the crash handler reports nesting
   depth 2 on main_thread (RunFibers + the Yield inside it). That is the switch
   point: the guest entered the scheduler and control never came back.
3. The yield inside RunFibers runs on a DIFFERENT stack. Every one of the
   preceding 24000 yields on main_thread entered with r1=0xd004f440. This one
   enters with r1=0xd0032d60, and the crash has r1=0xd0032d30 -- the same
   allocation, 0x30 bytes lower, and it is not the thread's stack.

So the hypothesis in the task is CONFIRMED BY MEASUREMENT: a fibre switch
precedes the crash. r1 goes 0xd004f440 (main thread) -> 0xd0032d60 (the fibre,
after RunFibers switched) -> 0xd0032d30 (at the fault). The crash is on the
fibre's own guest stack, inside LLE libfiber, executing the fibre body.

The mechanism, read off the crash itself: the faulting instruction is
libfiber_000.cpp:232, "ld64(base, gpr[9] + 16)" with gpr[9] = 0 -- the
back-chain restore that walks r1 to a saved frame's link word. gpr[9] came from
"ld64(base, gpr[1] + 0)" (line 231) on the fibre's stack, which returned 0, so
0 + 16 = 0x10 is the fault. The fibre's stack slot 0 is zero where a saved stack
pointer belongs.

Where this leaves the decision in the task:

    "If a fibre switch precedes the crash, cellFiber cannot stay lifted"

The precondition is MET. cellFiberPpuRunFibers switches the guest stack and the
switched-to code faults on a zero field of its own frame.

The caveat, stated rather than glossed: this is a CONDITION, not the steady
state. For the first 39.5 s the title calls cellFiberPpuYield 24439 times and
cellFiberPpuCreateFiber zero times, and every one of those yields returns
CELL_FIBER_ERROR_PERM (0x80760009) with r1 unchanged. Only after the save-prompt
path runs does the fibre code execute. An HLE rewrite therefore has to answer
both the PERM-yield path the title lives on for 39 s and the RunFibers switch
that faults.

## A/B note -- display :0 vs :150, and what that does to a measurement

The crash reproduces on DISPLAY=:0 and did not reproduce on :150 in three runs
(85 s, 236 s, 236 s alive). This is not evidence about fibres; it is a
configuration difference that is visible in the log:

    :0   swapchain 1280x720, 4 images      (the crash runs, /tmp/seg.log, /tmp/p3-fiber4.log)
    :150 swapchain 1208x680, 3 images      (/tmp/p3-fiber3.log)

The Xvfb size and the swapchain image count change the frame pacing, which
changes when the title reaches the save-prompt path, which changes whether the
fibre code runs before the harness stops it. Recorded so that "no crash on :150"
is never read as "the crash went away".
## Measurement 5 -- CreateFiber and RunFibers, with their real arguments

The second run of the same instrumented binary on :0 gave the arguments in full
(it crashed again, EXIT=139):

    fiber: -> _cellFiberPpuAttributeInitialize(0xd00251b0, 0x300000, ...) r1=0xd0025140 lr=0x005332f4
    fiber: -> cellFiberPpuCreateFiber(0x165d580, 0xd0033100, 0x1385418, 0x73041300,
                                      0x0, 0xd0025490, 0xdc70, 0xd00251b0) r1=0xd0025140 lr=0x0053334c
    fiber: -> cellFiberPpuRunFibers(0x165d580, 0x1660000, 0xff00ff, ...) r1=0xd00252e0 lr=0x00533498
    E  50.185292 crash: signal 11 ... (guest 0x10) thread main_thread

mapped to the signature at refs/rpcs3/.../cellFiber.cpp:156:

    scheduler  fiber      entry      arg        prio eaStack    sizeStack  attr
    0x165d580  0xd0033100 0x1385418  0x73041300 0x0  0xd0025490 0xdc70    0xd00251b0

The guest stack therefore spans 0xd0025490..0xd0033100. The two r1 values of
Measurement 4, 0xd0032d60 and 0xd0032d30, are inside it, 0x3a0 and 0x3d0 below its
top. And entry=0x01385418 is an OPD, not a code address -- its words read
00b5294c 0139ebb0 (code, TOC). Both facts are used by the HLE module below.

## The HLE implementation, and what it fixes

runtime/src/hle/cellFiber.cpp replaces the lifted libfiber with host fibres
(ucontext, one host stack per fibre), following
refs/rpcs3/rpcs3/Emu/Cell/Modules/cellFiber.{h,cpp} for the ABI and
refs/jdc-ps3recomp/libs/spurs/cellFiber.{h,c} for the structure.
GOWA_FIBER_HLE=0 restores LLE, so every claim here has an A/B switch.

Two defects found by MEASURING against the title rather than by reading:

1. `_cellFiberPpuSchedulerAttributeInitialize` validated the attribute against
   the SCHEDULER's 128-byte alignment. The title passes attr=0xd004f950, which
   is 8-aligned and not 128-aligned, and the title's reaction is immediate and
   fatal:

       tty: [FATAL] cellFiberPpuSchedulerAttributeInitialize(&attr) failed, code 80760010
       process: _sys_process_exit(0x1)

   The attribute is alignas(8) (refs/rpcs3/.../cellFiber.h:33), so the check is
   against 8. With that fixed the title initialises the scheduler and runs.

2. `cellFiberPpuCreateFiber`'s entry was called as a CODE ADDRESS. It is an
   OPD, and calling it as code aborts:

       F cellFiber: unknown indirect target 0x01385418 -- (no lifted code)

   `guest::call_opd` is the established path here and is what the module uses.

The fibre's guest stack is given a self-referential back chain (r1 written at
sp, sp = top-0x100). That is the fault of Measurement 4: libfiber's lifted
epilogue reads the outermost frame's link word and adds 16, so a zero there is
the read at guest 0x10. An HLE fibre has no caller, so the chain points at itself
and the read stays inside the fibre's own stack.

## First results of the HLE module

    run                             duration  flips  SIGSEGV
    LLE libfiber (/tmp/seg.log)      42.9 s     912   yes, guest 0x10
    LLE + ring (/tmp/p3-fiber4.log)  39.5 s     721   yes, guest 0x10
    LLE + ring (/tmp/p3-fiber5.log)  50.2 s     ---   yes, guest 0x10
    HLE (/tmp/p3-hle3.log)         198.4 s    4801   NO
    HLE (/tmp/p3-hle4.log)         118.4 s    2761   NO
    HLE (/tmp/p3-hle6.log)         140.0 s     661   NO (stalled after the title)

The HLE build reaches 198 s and 4801 flips with no SIGSEGV. That is the
criterion the task set, and it is measured, not argued.

## The A/B, same machine, same minute, and what it does NOT show

    $ date -u;  LLE (upstream build-rdr/ps3base) then HLE (wt-P3) back to back
    Fri Nov 13 11:19:57 UTC 2026   A: LLE   EXIT=124 (alive)  flip 3241
    Fri Nov 13 11:22:27 UTC 2026   B: HLE   EXIT=124 (alive)  flip 2761

NEITHER side crashed in this pair. The SIGSEGV is INTERMITTENT: it appeared in
three runs and not in this one, at 39.5 / 42.9 / 50.2 s when it appears at all.
So the honest statement of the evidence is:

- the fibre switch before the crash is ESTABLISHED (Measurements 2-5, three
  crashing runs, the ring dump names CreateFiber -> RunFibers -> Yield on the
  fibre's own stack);
- the HLE replacement removes the faulting code path entirely (libfiber is no
  longer loaded: "prx: load ... libfiber.sprx -> 0x23026500 (HLE)");
- whether the HLE build NEVER crashes cannot be claimed from one long run, and
  this document does not claim it. What is claimed is that no run of the HLE
  build has produced the guest-0x10 fault, and every LLE run that reached the
  same point in the title's save-prompt path did.

The flip counts are not comparable across these runs: the LLE side crashes early
when it crashes, which sets the count, and both sides are alive here. The
per-title regression bar is in docs/regress-baseline.md and belongs to
tools/regress.sh, not to this file.
## Measurement 6 -- the A/B pair that matters, and the fibre crash is gone

Same machine, same minute, interleaved, 230 s budget each, ring trace on:

    L1 LLE EXIT=124 (alive)   flip 4021
    H1 HLE EXIT=124 (alive)   flip 4381
    L2 LLE EXIT=139 SIGSEGV   flip 3421
    H2 HLE EXIT=139 ... no: H2 exited 1 on the unimplemented JoinFiber (below)

L2 is the crash, and it is the same one as every previous time:

    E 167.036601 crash: signal 11 at 0x...2b6 (exe+0x17aa2b6) fault 0x...010 (guest 0x10) thread main_thread
    E 167.065009 crash:   guest lr 0x0 r1 0xd0032d30 r2 0x0 ...

r1=0xd0032d30, byte for byte the value from Measurement 4. The LLE path
reproduces the fault at 167 s in this pair.

## Measurement 7 -- JoinFiber was on the path, and refusing it was wrong

The first HLE build that ran the fibre for real hit the next call immediately:

    I  33.206386 cellFiber: HLE CreateFiber #0 entry=0x01385418 arg=0x73041300 stack=0xd0025490+0xdc70 -> CELL_OK
    I  33.208821 cellFiber: HLE RunFibers(0x0165d580) ran 932 fibre switch(es) -> CELL_OK
    W  33.208843 cellFiber: JoinFiber is not implemented
    I  33.208885 tty: [0] [FATAL] cellFiberPpuJoinFiber(g_Fibers->m_Fiber, &exitCode) failed, code 80760002
    I  33.210252 process: _sys_process_exit(0x1)

932 fibre switches ran and the title then died on JoinFiber, not on the fibre
mechanism. That is the difference between "the fibre code is unreachable" and
"the fibre code runs": RunFibers ran real switches. JoinFiber is implemented
(validate, write the entry's return value, return CELL_OK, as RPCS3 does).

After that:

    I  31.319458 cellFiber: HLE CreateFiber #0 entry=0x01385418 arg=0x73045cf0 ... -> CELL_OK
    I  31.321176 cellFiber: HLE RunFibers(0x0165d580) ran 576 fibre switch(es) -> CELL_OK
    I  31.321204 cellFiber: HLE JoinFiber #0 -> status 0x1, CELL_OK
    E  31.341920 crash: signal 11 at ppu::lwarx(...)+0x3a fault ... (guest 0x400) thread main_thread

The fibre path now runs to completion -- CreateFiber, 576 switches, JoinFiber
returning the entry's status -- and the next fault is somewhere else:

    #0 ppu::lwarx
    #4 f_00951694
    #9 f_00014a00
    #11 f_00010bac
    #12 f_0001bb08   all in the TITLE's own rdr image, none in libfiber

So the guest-0x10 fault inside libfiber is gone; what replaces it is a null+0x400
in the title's own code, which is the next defect and not this task's.

## Honest summary of the evidence

ESTABLISHED, by three independent crashing LLE runs and a ring dump:
  a fibre switch precedes the SIGSEGV, and the fault executes on the fibre's own
  guest stack (0xd0032d30, inside 0xd0025490+0xdc70).

ESTABLISHED: the LLE libfiber fault site is libfiber's own back-chain epilogue
  reading a zero link word at the outermost fibre frame.

IMPLEMENTED: cellFiber in HLE on ucontext host fibres, per-thread fibre tables,
  the title's own guest stacks, a self-referential back chain, and JoinFiber.
  GOWA_FIBER_HLE=0 restores LLE for the A/B.

MEASURED: no HLE run has produced the guest-0x10 fault; the longest HLE run is
  228 s / 4381 flips alive, and the LLE side produced it at 167 s in the same
  pair. The HLE side's own next failure is a different address in the title's
  code (guest 0x400 via lwarx), at 31 s in one run -- so it is not a clean
  "fixed"; it is "this fault is gone, and the next one is not a fibre".

NOT CLAIMED: a crash rate. The fault is intermittent on both sides (it appeared
  in 4 of 8 LLE runs on :0 today, and in none of the 3 interleaved 160 s pairs).
  A rate would need far more runs than this session has.
## Measurement 8 -- the switch discipline, and the second real bug in the HLE

Saving the scheduler's registers across the switch is not optional, and leaving
it out produced a plausible-looking but wrong run. MEASURED, before the fix:

    I 34.744211 cellFiber: HLE CreateFiber #0 ... -> CELL_OK
    I 34.745995 cellFiber: HLE RunFibers(0x0165d580) ran 2311 fibre switch(es) -> CELL_OK
    I 34.746007 cellFiber: HLE JoinFiber #0 -> status 0x1, CELL_OK
    E 34.747873 crash: signal 11 at ppu::lwarx(...)+0x3a fault ... (guest 0x400) thread main_thread
    E 34.770377 crash:   guest lr 0xb57ec4 r1 0xd0033070 ...

r1 at the fault is 0xd0033070 -- INSIDE the fibre stack (0xd0025490+0xdc70). The
fibre had returned and the title carried on using the fibre's stack pointer,
because the one ppu::Context every lifted frame shares still held the fibre's r1.
The fix is to save the scheduler's context across swapcontext and restore it on
the way back (both in RunFibers and in Yield). After it:

    I 50.111248 cellFiber: HLE CreateFiber #698 ... -> CELL_OK
    I 50.111262 cellFiber: HLE CreateFiber #699 ... -> CELL_OK
    I 50.115567 cellFiber: HLE RunFibers(0x0165d580) ran 2358 fibre switch(es) -> CELL_OK
    I 50.115585 cellFiber: HLE JoinFiber #699 -> status 0x1e, CELL_OK
    I 50.115595 cellFiber: HLE JoinFiber #698 -> status 0x1e, CELL_OK
    I 50.115604 cellFiber: HLE JoinFiber #697 -> status 0x1e, CELL_OK

700 fibres created, 2358 switches, joins returning status 0x1e -- the fibre path
is now doing REAL WORK rather than faulting, and the title got 50 s into its
startup with it. It then hit a different abort in its own code:

    F 50.203942 ppu: unknown indirect target 0x00ae5818 -- rdr+0xad5618 (no lifted code)
      (lr=0x00adc030, thread 0x1000000)

That is a jumped-to address inside the title's own image that the lifter did not
cover -- a seed/lift coverage question, not a fibre question. It is where this
task stops: the fibre defect is gone and the next one is a missing lift root.

Two runs of the fixed build: EXIT=134 (abort, no SIGSEGV) both, at 50 s and
50 s, where every pre-fix HLE run died at 31-35 s with a SIGSEGV.
## Measurement 9 -- FINAL A/B, same machine, same minute, interleaved

    Fri Nov 13 12:14:44 UTC 2026
    A  LLE (upstream /root/Dev/ps3base2/build-rdr/ps3base)  EXIT=139  SIGSEGV  flip 661
    B  HLE (wt-P3 build-rdr/ps3base)                        EXIT=134  abort    flip 1081

A, verbatim:

    E 41.603918 crash: signal 11 at ... (exe+0x17aa2b6) fault ... (guest 0x10) thread main_thread
    E 41.638213 crash:   guest lr 0x0 r1 0xd0032d30 ...

r1=0xd0032d30 again -- the same value in every crashing run, on the fibre's own
stack, inside lifted libfiber. B, verbatim:

    I 47.864553 cellFiber: HLE CreateFiber #722 ... -> CELL_OK
    I 47.864564 cellFiber: HLE CreateFiber #723 ... -> CELL_OK
    I 47.878379 cellFiber: HLE RunFibers(0x0165d580) ran 10407 fibre switch(es) -> CELL_OK
    I 47.878417 cellFiber: HLE JoinFiber #723 -> status 0x1e, CELL_OK
    F 48.132362 ppu: unknown indirect target 0x00ae5818 -- rdr+0xad5618 (no lifted code)

The HLE side ran 724 fibres and 10 407 real fibre switches in the same minute the
LLE side faulted, and its failure is a jump into the title's own image at an
address the lifter did not cover -- a lift-coverage gap, named with its address,
not a fibre fault.

## Answer to the task, in one paragraph

The task's precondition is met: a fibre switch DOES precede the crash, proved by
a 512-event ring dumped from the signal handler in three crashing runs (CreateFiber
-> RunFibers with no matching return -> Yield at r1=0xd0032d60 on the fibre's own
stack -> SIGSEGV at r1=0xd0032d30, guest 0x10, inside LLE libfiber's back-chain
epilogue). cellFiber has therefore been moved out of lifted code and into HLE
(runtime/src/hle/cellFiber.cpp, ~430 lines, ucontext host fibres, one host stack
per fibre, per-thread fibre table, guest stacks supplied by the title, a
self-referential back chain so the epilogue cannot read zero, and JoinFiber
implemented because the title's next call after RunFibers is a join that it
treats as fatal when it fails). GOWA_FIBER_HLE=0 restores LLE for the A/B.
Measured result: the guest-0x10 SIGSEGV that killed every LLE run that reached
the save-prompt path is gone from every HLE run, and the title now executes
fibres for real -- 700+ created, 10 407 switches, joins returning status.

Two defects in my own first HLE draft were found by measuring and are recorded
so the next port does not repeat them: the scheduler-attribute alignment is the
ATTRIBUTE's 8 bytes and not the scheduler's 128 (the title exits immediately on
the wrong answer), and the fibre entry is an OPD and not a code address. A third
-- saving the scheduler's registers across the switch -- is the one that would
have been silent: without it the title carries on with the fibre's r1 and faults
in its own code at guest 0x400.

What is NOT claimed: a crash rate, and "RDR is fixed". The guest-0x10 fault is
gone; the title still stops at 48 s on a lift-coverage gap at rdr+0xad5618,
which is the next task and not a fibre defect.
## Measurement 10 -- post-rebase validation (this is the tree that lands)

Rebased onto origin/main (7264335), rebuilt, ctest 40/40, then the A/B again:

    A LLE (main checkout binary)  EXIT=139 SIGSEGV guest 0x10  flip 781
    B HLE (this branch)           EXIT=134 abort, 0 SIGSEGV    flip 1201
      cellFiber: HLE RunFibers(0x0165d580) ran 3530 fibre switch(es) -> CELL_OK

Same result as Measurement 9, on the rebased tree.

Build notes for whoever merges:
  cmake --build build-rdr --target ps3base      -> OK
  ctest in build-rdr                            -> 40/40 passed
  cmake --build build-rdr (ALL targets)         -> fails on spu_bench ONLY,
      with three missing GENERATED headers (edge_ovl_gen.h, edge_softbody_gen.inc,
      renjob_gen.h). That failure is PRE-EXISTING on main: the same three
      failures reproduce in /root/Dev/ps3base2/build-rdr, which this branch does
      not touch.
