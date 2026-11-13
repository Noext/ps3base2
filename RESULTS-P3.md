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
