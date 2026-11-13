# S3 -- characterising the RDR "EXODUS IN AMERICA" crash: three 900 s runs

**Measured 2026-11-13.** Binary `build-rdr/ps3base`, HEAD `48f2a20`, 695 805 304 bytes,
built 17:30:16Z. One run at a time under `/var/lock/ps3-machine.lock`; one screenshot per
second for the whole run into `/tmp/rdr-crash-s3/run<N>/shots/`.

## (a) and (b): the headline -- the crash did not reproduce in three runs

**(a) start, end and how each run ended; (b) how long it ran after the card.**

| run | game start | game end | chapter card first seen | ran after the card | how it ended | crash lines |
|---|---|---|---|---|---|---|
| 1 | 17:34:06.059Z | 17:49:07.822Z | 17:37:11.039Z (t+game 184.98 s) | 717.0 s | killed by the 900 s bound (driver rc 1, exit None) | **0** |
| 2 | 17:50:30.769Z | 18:05:32.648Z | 17:53:27.744Z (t+game 176.97 s) | 725.0 s | killed by the 900 s bound (driver rc 1, exit None) | **0** |
| 3 | 18:05:51.339Z | 18:20:52.674Z | 18:09:35.322Z (t+game 223.98 s) | 676.0 s | killed by the 900 s bound (driver rc 1, exit None) | **0** |

"Killed by the 900 s bound" is the driver's own `max_seconds`, not a wrapper timeout: each
run reports `exit None elapsed 900.5 / 900.5 / 900.1`, i.e. the guest was **still running**
when the driver stopped sampling and SIGKILLed it. The distinction the task asks for is
therefore: **no run was killed by a measurement timeout (no exit 124, no driver rc other
than 1), and no run crashed** -- the process was alive at the bound in all three cases.

`grep -ac 'crash: signal' run{1,2,3}/drv/run.log` = 0, 0, 0. The last guest log line of
each run is at t=900.56 s, t=900.56 s and t=900.07 s respectively -- in every case a
`cellFiber: HLE JoinFiber #N -> status 0x16 after 0 pass(es), CELL_OK` line. The title was
alive and logging cellFiber imports at the instant it was killed, three times out of three.

**This is a FAILED reproduction, and it has to be stated that way.** Three 900 s runs did
not crash, so this document cannot report a crash site, stack frames, `addr2line` output,
a guest lr, a register dump or "the last 50 imports of the crashing thread" for a crash it
observed. What it can do -- and does below -- is characterise the ONE crash that is on
disk (`/tmp/rdr-fix-verify/run.log`, 192.63 s) from its own log, and report what the three
new runs measured about the state that crash interrupted.

The runs are **not** "killed by my timeout" in the sense the task warns about: the driver's
own `max_seconds` is 900 and it reports `exit None elapsed 900.5`, i.e. the guest was
still running when the driver stopped sampling and SIGKILLed it. That is the intended
bound, not a measurement failure -- but it is also not evidence that no crash happens,
only that none happened within 900 s.

## The crash that is on disk, read from its own log

`/tmp/rdr-fix-verify/run.log`, written 16:56:57Z. It is the run the task describes.

### (c) the crash line, the stack frames, and addr2line

    E 192.631565 [3982108] crash: signal 11 at (nil) (?) fault (nil) (guest 0xffff8bd390000000) thread main_thread 0x1000000
    E 192.631763 [3982108] crash:   #0 ?
    E 192.631773 [3982108] fibertrace: crash: NOT RECORDING -- PS3BASE_FIBER_TRACE is not set, so this run gathered no cellFiber events (this is not evidence that none ran)
    E 192.631782 [3982108] crash:   cellFiber import nesting depth on this thread: 0

**There is exactly one stack frame and it is "?".** The host backtrace produced nothing:
`rip` is 0, so `stack_from()` (runtime/src/crash_posix.cpp:79) cannot match it against
`backtrace()`'s array and emits the single frame `#0 ?`. There is therefore **no
`exe+0x...` offset anywhere in this crash block**, and the task's instruction to run
`addr2line` on the frames applies to nothing: there is no frame to resolve. That is not
"addresses below .text" -- it is "no addresses at all".

    $ sed -n '/crash: signal/,/tls: census (crash)/p' /tmp/rdr-fix-verify/run.log | grep -c 'exe+0x'
    0

**A trap worth naming, because it produces a confident wrong answer:** `addr2line` will
happily "resolve" the guest lr, and the answer is meaningless. `0xb583e0` is a **guest**
address; `.text` of `build-rdr/ps3base` starts at `0xafb6f0`, so the guest address falls
inside the host binary's text by coincidence and `addr2line` returns
`gpu::rsx::frame_complete_body()` at `vector.tcc:119` -- a host function that has nothing to
do with the guest. `0xb583e0` is meaningful only against the **lifted guest source**
(`out-rdr/ppu/rdr_115.cpp`), and that is how it is resolved below.

### The fault address is NOT a guest address, and this is the single most important correction

`fault (nil)` and `at (nil)` say the host faulted at address 0. The `guest 0x...` field is
computed by the reporter as `si_addr - vm::g_base` (runtime/src/crash_posix.cpp:359). With
`si_addr = 0` that is `0 - g_base` in unsigned 64-bit arithmetic:

    $ grep -m1 'vm: guest base' /tmp/rdr-fix-verify/run.log
    I   0.367463 [3981972] vm: guest base 0x742c70000000 (backing: os)
    $ python3 -c "print(hex((1<<64)-0x742c70000000))"
    0xffff8bd390000000

which is exactly the reported `guest 0xffff8bd390000000`. The same check on the other
crash log on the machine (`/tmp/rdr-ab-ref-vs-cand-B`, base `0x74d2dc000000`) gives
`0xffff8b2d24000000`, also exact.

So **`guest 0xffff8bd390000000` is not a guest address at all** -- it is the arithmetic
artefact of subtracting the guest base from a null host pointer. The fault is a **null
host code pointer being called**: `rip = 0`, `si_addr = 0`, SIGSEGV, no guest memory
involved. The task's premise that "the fault address 0xffff8bd390000000 is a sign-extended
32-bit value" is a misreading of the reporter's own arithmetic, and it points the search
at guest memory when the fault is in the host dispatcher.

### (d) the guest lr and the full register dump

    E 192.631792 crash:   guest lr 0xb583e0 r1 0xd0032f00 r2 0x139ebb0 r3 0x0 r4 0x1734780 r5 0x3
    E 192.631804 crash:   guest regs: r0=0xb583e0 r1=0xd0032f00 r2=0x139ebb0 r3=0x0 r4=0x1734780 r5=0x3
                                       r6=0x10 r7=0x100000000000000 r8=0x30000000 r9=0x100000000000000
                                       r10=0x1000000 r11=0x0 r12=0x733f0600 r13=0x10007060 r14=0x0
                                       r15=0x0 r16=0x0 r17=0x0 r18=0x0 r19=0x1 r20=0xd0033480 r21=0x1
                                       r22=0x1 r23=0x1 r24=0x1 r25=0x1385418 r26=0x732df470 r27=0x1
                                       r28=0xe000 r29=0x0 r30=0x1 r31=0x732df470

`lr 0xb583e0` is the return address of the call at `0xb583dc`, which is
`ctx.lr = 0xb583e0; f_00b57c10(ctx, base);` in `out-rdr/ppu/rdr_115.cpp:18373`, inside
`f_00b58368_body` (`out-rdr/ppu/rdr_115.cpp:18340`). That call is a **direct** call to a
lifted function, not an indirect dispatch, so `lr` names where the guest was, not what was
called through a null pointer.

`r1 = 0xd0032f00` is on the fibre stack: `CreateFiber` was called with
`stack=0xd0025490+0xdc70`, so the fibre stack spans `0xd0025490..0xd0033100`, and
`0xd0032f00` is 0x200 below its top. `r31 = 0x732df470` is the fibre's argument
(`arg=0x732df470` in every `CreateFiber` line of that run), and `r25 = 0x1385418` is the
fibre entry OPD. So the crash happened **on a fibre's guest stack, inside the fibre body**.

Two intervals are quoted for this and **both are right, because they end at different
instants** -- this is written down because "correcting" one of them would be a false
correction:

* **20.96 s** -- card (171.67 s) to the crash line (192.631565 s). This is the interval to
  the *fault*.
* **22.03 s** -- card (171.67 s) to the driver's exit (193.7 s). This is the interval to the
  *process death*, and it is the figure the task quotes.

The 1.07 s between them is the crash handler's own work: `hooks::capture("crash")` dumps the
whole guest address space to `blackbox/memory_0.bin` (517 MB in this run) before the process
exits, so the driver cannot observe the death until that dump finishes.

### (e) the last imports of the crashing thread

`PS3BASE_IMPORT_NAME` was **not** used: the task records that it emitted 15.5 M lines in
400 s and starved the guest, and re-running with it would have been an instrumented run
whose timing is not comparable. The ordinary `PS3BASE_LOG=info` log already carries every
HLE import with its result, and every line carries the host thread id in brackets, so the
crashing thread is identified from the crash line itself (`[3982108]`) rather than assumed.

The last imports of host thread 3982108 before the fault, verbatim:

    I 192.081679 cellFiber: HLE CreateFiber #253 entry=0x01385418 arg=0x732df470 stack=0xd0025490+0xdc70 prio=0 -> CELL_OK
    I 192.090533 cellFiber: HLE RunFibers(0x0165d580) ran 6405 fibre switch(es) -> CELL_OK
    I 192.090533 cellFiber: HLE JoinFiber #253 -> status 0x1 after 0 pass(es), CELL_OK
    I 192.176930 cellFiber: HLE CreateFiber #254 entry=0x01385418 arg=0x732df470 stack=0xd0025490+0xdc70 prio=0 -> CELL_OK
    I 192.177676 cellFiber: HLE RunFibers(0x0165d580) ran 814 fibre switch(es) -> CELL_OK
    I 192.177683 cellFiber: HLE JoinFiber #254 -> status 0x1 after 0 pass(es), CELL_OK
    I 192.226835 sceNp: ManagerGetStatus -> -1
    I 192.287521 cellFiber: HLE CreateFiber #255 entry=0x01385418 arg=0x732df470 stack=0xd0025490+0xdc70 prio=0 -> CELL_OK
    I 192.296080 rsx: fifo 5s: busy 31.7%, idle 0.4%, semaphore wait 68.0% (30/s), 3 idle sleeps/s, methods 99.3% (58k/s)
    W 192.388844 cellFiber: RunFibers: 100000 switches without finishing; stopping
    I 192.388883 cellFiber: HLE RunFibers(0x0165d580) ran 100001 fibre switch(es) -> CELL_OK
    W 192.487338 cellFiber: JoinFiber #255: the fibre did not terminate after 1 passes; returning the status it has
    I 192.487390 cellFiber: HLE JoinFiber #255 -> status 0x0 after 1 pass(es), CELL_OK
    I 192.513542 cellNetCtl: GetInfo(code=0x4) -> 0x80130108, 0 byte(s)
    I 192.513656 sceNp: ManagerGetStatus -> -1
    I 192.628450 cellFiber: HLE CreateFiber #256 entry=0x01385418 arg=0x732df470 stack=0xd0025490+0xdc70 prio=0 -> CELL_OK
    E 192.631565 crash: signal 11 at (nil) ... thread main_thread 0x1000000

The sequence is: **CreateFiber #256 returns CELL_OK, and 3.1 ms later the host jumps to
address 0.** No import is in flight (`nesting depth 0`), so the null call is not inside a
handler -- it is the dispatcher or the lifted code calling through a function pointer that
is null. The immediately preceding event is the `RunFibers` guard trip and the
`JoinFiber #255` that returned `status 0x0` for a fibre that "did not terminate", which is
the state the 48f2a20 JoinFiber change introduced.

## (f) the barrel: it animates right up to the card's last frame, then the card fades

The film-reel indicator was located on **this run's own pixels**, not carried over from an
earlier note. Red-dominant pixels in the lower-left card area form one blob at
**x 128..190, y 584..647**; the chapter TITLE text starts at x 203, so the animation crop is
`(120,575)-(200,655)`, which holds the reel and nothing else. (The s2 note's crop,
`(110,570)-(250,660)`, reaches past x 190 and therefore measured the first letter of the
title as well.)

Consecutive one-second frames, mean absolute difference (MAD) of the greyscale crop. Two
identical frames give exactly 0.0000; the animating reel gives 3-7:

| run 1 pair (shot) | barrel MAD | note |
|---|---|---|
| 348 -> 349 | 3.8698 | |
| 352 -> 353 | 5.9769 | |
| 356 -> 357 | 7.1289 | |
| 360 -> 361 | 6.4994 | |
| 364 -> 365 | 7.5389 | |
| **367 -> 368** | **5.4508** | **last frame that still shows the card** |
| 368 -> 369 | (card gone) | the fade has started |

**The barrel does not freeze before the crash -- in these three runs there is no crash, and
the barrel is still moving at the last frame that shows the card.** The card then
**cross-fades out** over shots 369-377 (mean R 26.8 -> 22.7 -> 18.6 -> 14.9 -> 10.7 -> 2.9
-> 0.0) and the green clear colour fades **in** over 375-385 (mean G 17.8 -> 34.7 -> 50.8 ->
65.0 -> 87.3 -> 97.1 -> 107.8 -> 123.8 -> 127.3 -> 129.1). So the honest statement is the
one the task asks for: **the last frame that still shows the card is shot 368 (t+game
367.98 s); the green is complete by shot 385 (t+game 384.98 s)**. Reporting "green starts at
shot N" would be reporting a step where there is a ramp.

The green test uses **G > 60**, as the task requires. The clear colour measured on this run
is `srgb(0,145,0)`, not `#006200` (G=98) as the s2 note says; both are above 60, and a
`G > 100` test is above both and reports the fade late. The test used here is
`G > 60 and R < 40 and B < 40`.

All three runs agree, and the numbers are independent measurements of the same behaviour:

| run | first card | last card (still animating) | first green >80% | fade length |
|---|---|---|---|---|
| 1 | t+game 184.98 s | t+game 367.98 s (MAD 5.4508) | t+game 377.98 s | 10.0 s |
| 2 | t+game 176.97 s | t+game 374.97 s (MAD 3.5116) | t+game 385.97 s | 11.0 s |
| 3 | t+game 223.98 s | t+game 408.98 s (MAD 4.2277) | t+game 418.98 s | 10.0 s |

The gap from the last barrel movement to the end of the run is 533.78 s in run 1, 526.90 s
in run 2 and 492.35 s in run 3 -- but that gap is to the **900 s bound**, not to a crash,
because there was no crash.

## (g) did the three runs crash at the same site?

**They did not crash at all.** There is no second crash line to compare with the one on
disk, so the question "same site?" cannot be answered from these three runs and this
document does not answer it. What can be compared is the *state* the runs reached, and on
that the three runs and the reference crash run are near-identical up to the moment the
reference run died:

| | reference crash run | run 1 | run 2 | run 3 |
|---|---|---|---|---|
| chapter card first seen | 171.67 s | 184.98 s | 176.97 s | 223.98 s |
| `CreateFiber` calls | 257 | 5522 | 4983 | 5739 |
| `RunFibers` 100 000-switch guard trips | 4 | 3 | 8 | 8 |
| `JoinFiber ... did not terminate` | 2 | 1 | 2 | 3 |
| crash | **yes, 192.63 s** | no | no | no |

**Run 2 and run 3 each tripped the guard and produced a non-terminating join MORE often
than the run that crashed, and survived.** So neither the guard trip nor the
non-terminating join is sufficient for the crash. The fibre rate is also not the
differentiator: at t=180..192 s the reference run created 114 fibres (9.5/s), run 1 created
122 (10.2/s) and run 2 created 112 (9.3/s) -- the same within noise.

## What the three runs did establish, and what they did not

Established, each with a command:

1. **No crash in 2705 s of runtime across three runs** on `build-rdr/ps3base` at 48f2a20
   (game-process lifetimes 901.8 s, 901.9 s, 901.3 s, measured from `ps3base.start` to
   `ps3base.end.real`). Zero `crash: signal` lines and zero `F ` (fatal) lines in all
   three logs. The state after the card is stable: green screen, 0.6-1.5 fps, main thread
   cycling `CreateFiber`/`RunFibers`/`JoinFiber` and `sceNp: ManagerGetStatus -> -1`
   forever.
2. **The crash on disk is a null host code pointer (`rip = 0`), not a guest-address fault.**
   `guest 0xffff8bd390000000` is `0 - guest_base` exactly, verified against the run's own
   `vm: guest base` line.
3. **The card's last frame is shot 368 (t+game 367.98 s) in run 1, shot 375 (t+game
   374.97 s) in run 2 and shot 409 (t+game 408.98 s) in run 3, and the barrel is still
   animating in that frame every time (MAD 5.45, 3.51, 4.23).** The transition is a
   cross-fade, 10-11 s long, not a cut, in all three runs.
4. **The reference crash run was the same cellFiber code as HEAD, so the crash is not
   explained by "the reference run predates the fix".** This is an inference from log
   strings, and the reasoning is written out so it can be checked:

       $ for c in b6ed791 48f2a20; do echo -n "$c: "; \
           git show $c:runtime/src/hle/cellFiber.cpp | grep -c 'pass(es)'; done
       b6ed791: 0
       48f2a20: 1
       $ grep -c 'pass(es)' /tmp/rdr-fix-verify/run.log
       256

   The `after N pass(es)` wording exists only in 48f2a20's cellFiber.cpp, and the 16:56
   crash log contains 256 such lines. The commit is dated 17:30, i.e. *after* the run, so
   the change was in the working tree (uncommitted) when that run was built; it was then
   committed and rebuilt at 17:30. The working tree's cellFiber.cpp is byte-identical to
   48f2a20 (`git diff --stat 48f2a20 -- runtime/src/hle/cellFiber.cpp` is empty), so the
   three new runs and the crash run exercised the same handler code.

   What this does **not** establish: that the whole binary was identical. The lift stamps
   are the same (`GOWA-pc: 9 images, 76793 functions, 447 imports, 860 exports` in all
   four runs), but the 17:30 rebuild could in principle differ elsewhere. The claim is
   about cellFiber, which is what the crash's import history implicates.

Not established: any crash site, any stack frame, any `addr2line` resolution, any guest lr
for a crash these runs observed -- because they observed none.

## Two defects found in the code while reading it (not fixed: this task measures)

Both are in `runtime/src/hle/cellFiber.cpp` and both are stated because they are visible in
the source and would each explain a late, rare, timing-dependent failure:

1. **`CreateFiber` leaks its host stack.** `f.host_stack = std::malloc(256 * 1024)`
   (line 319) is freed only on the `getcontext` failure path (line 322), never when the
   fibre terminates or its slot is reused. The three runs created 5522, 4983 and 5739
   fibres, i.e. **1.35, 1.22 and 1.40 GiB of host stacks that are never freed**. This is not the crash
   (it would OOM, not SIGSEGV at rip 0) but it is a real leak that grows without bound.
2. **`ThreadState*` points into a `std::vector` that can reallocate.**
   `g_threads` is `std::vector<std::pair<uint32_t, ThreadState>>` (line 125) and
   `state_for()` returns `&g_threads.back().second` (line 130). `ThreadState` holds
   `scheduler_ctx` and `live_ctx`, and `state_for_self()` is called at 12 sites, several of which hold the
   pointer across a `swapcontext`. RDR only ever creates fibres on one guest thread: every
   `fiber: -> cellFiberPpu...` trace line in all three runs carries `th=0x1000000`
   (`grep 'fiber: ->' runN/drv/run.log | grep -o 'th=0x[0-9a-f]*' | sort -u` returns that
   one value), and every `CreateFiber` line comes from one host thread per run. So the
   vector never grows past one element and this does not fire here -- but it is a latent
   use-after-realloc for any title that creates fibres on two threads.

## Method, and the confound that has to be recorded

* Lock: `tools/machine_lock.sh run 's3 RDR crash characterisation runN' -- /tmp/rdr-crash-s3/run.sh runN`.
  `tools/machine_lock.sh status` was `free` and `pgrep -f '^/root/Dev/ps3base2/build-rdr/ps3base --no-launcher'`
  was empty before each run. Load at start: run 1 `1.99 5.15 7.04`, run 2 `10.44 14.28 12.61`,
  run 3 `15.91 17.59 15.93`.
* **Confound, recorded rather than hidden:** from run 2 onward the machine was NOT quiet.
  `/root/Dev/t-T1` ran a heavy emscripten compile (a full `em++` link of
  `/tmp/out-T1/game.js`) **without taking the machine lock**, and load sat at 10-20 through
  runs 2 and 3. `ps` shows the `em++` process. So runs 2 and 3 are not an A/B against run 1
  in the sense AGENTS.md requires; run 1 (load 2.0) is the clean one, and the fact that all
  three agree is what makes the "no crash" observation usable.
* `PS3BASE_FIBER_TRACE=1` was set for all three runs. It keeps 512 events **in memory** and
  writes nothing during the run (runtime/src/fibertrace.cpp), so it is not the per-import
  logging that starved the guest. It was on so that a crash would have printed the cellFiber
  ring; no crash occurred, so it printed nothing.
* Screenshots: `/tmp/rdr-crash-s3/shots.py`, one `import -window root` per second, files
  named `shot-NNNNN-YYYYMMDDTHHMMSS.mmmZ.png` with a `manifest.csv` carrying the epoch of
  each. 902, 902 and 901 shots for runs 1, 2 and 3.
* A first version of `shots.py` used `pgrep -x ps3base` and recorded the game as dead 2.5 s
  into run 1, because the runtime renames its main thread and `/proc/PID/comm` becomes
  `Host main`. The capture loop was patched to match the command line, and run 1's end
  instant was measured independently by `endmon.py` watching `/proc/PID` at 100 ms
  (`ps3base.end.real`). The bogus `ps3base.end` written by the broken loop is still in
  run 1's directory and is NOT the value used above.

## Reproduce

    cd /root/Dev/ps3base2
    tools/machine_lock.sh status          # must say free
    pgrep -f '^/root/Dev/ps3base2/build-rdr/ps3base --no-launcher'   # must be empty
    tools/machine_lock.sh run 's3 RDR crash characterisation run1' -- /tmp/rdr-crash-s3/run.sh run1
    python3 /tmp/rdr-crash-s3/analyse.py /tmp/rdr-crash-s3/run1 - - quiet
    python3 /tmp/rdr-crash-s3/crashreport.py /tmp/rdr-crash-s3/run1/drv/run.log   # 0 crash lines
    python3 /tmp/rdr-crash-s3/imports.py /tmp/rdr-crash-s3/run1/drv/run.log 50
