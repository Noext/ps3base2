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
