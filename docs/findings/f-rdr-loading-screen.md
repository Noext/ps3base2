# RDR's "EXODUS IN AMERICA" screen: what three 900-second runs measured

**Measured 2026-11-13.** This note corrects two claims that were carried into the
session as premises and are false. Both were false in the direction of "the game
crashes", and each would have sent the next reader hunting a crash that is not
there.

## Premise 1: "the game crashes during the loading screen" -- false

Three runs of 900 s each, one at a time under `/var/lock/ps3-machine.lock`, on
`build-rdr/ps3base` at HEAD `b6ed791`, with one screenshot per second:

| run | chapter card first seen | ran after the card | how it ended | crash lines |
|---|---|---|---|---|
| 1 | 15:33:36.643Z | 755.5 s | killed by the 900 s bound | **0** |
| 2 | 16:06:35.656Z | 743.0 s | killed by the 900 s bound | **0** |
| 3 | 16:22:31.430Z | 745.0 s | killed by the 900 s bound | **0** |

`grep -ac 'crash: signal' run{1,2,3}/drv/run.log` = 0, 0, 0. The last guest log
line of each run is at t≈901 s: the title was alive and logging at the moment it
was killed. There is no crash site, so there is nothing to resolve with
`addr2line` and no guest lr or register dump to report.

**All three runs are therefore inconclusive about a crash** in exactly the sense
that matters: they are evidence that no crash happened within 900 s, not evidence
that none happens.

## Premise 2: "the barrel freezes before the crash" -- false, and there is no crash

The film-reel indicator sits in the lower-left of the card. Its region was
measured from a capture, not guessed: red-dominant pixels span x 128..239,
y 584..646, and the comparison crop is (110,570)-(250,660).

Consecutive one-second frames, mean absolute difference (MAD) of the greyscale
crop. The animation gives MAD 2.1-5.5 per second while the card is at full
brightness; two identical frames give 0.0000. Two orders of magnitude, so the
separation is not a judgement call.

| pair | barrel MAD | mean R of the frame |
|---|---|---|
| 270→271 | 2.845 | 37.0 → 37.0 |
| 271→272 | 3.458 | 37.0 → 36.9 |
| 272→273 | 3.812 | 36.9 → 36.9 |
| 273→274 | 2.102 | 36.9 → 36.9 |
| **274→275** | **5.539** | **36.9 → 31.3**  <- the fade starts |
| 275→276 | 0.000 | 31.3 → 31.3 |
| 276→277 | 1.183 | 31.3 → 20.5 |
| 277→278 | 0.382 | 20.5 → 14.8 |
| 278→279 | 0.472 | 14.8 → 4.0 |

The barrel is still animating in the last pair before the card starts to fade.
The card does not freeze and is not replaced abruptly: it **cross-fades**, red
falling 37 -> 4 over shots 275-279, and the green clear colour **fades in** from
shot 280 (G 39) to shot 288 (G 145). The last frame showing the card is 279.

### The boundary is a fade, and two earlier readings of it were wrong

The first pass put the green transition at shot 284 by testing
`G > 100 and R < 100 and B < 100`. That threshold is above the actual clear
colour, which is `#006200` (G = 98), so it reported the fade 4 shots late. A
second pass with `G > 60` put it at 282. Both are wrong in the same way: they
look for a step where there is a ramp. Measured frame by frame, the mean green
of the frame interior runs 0, 39, 57, 86, 98, 121, 134, 145 -- a fade, not a cut.
The honest statement is "the card's last frame is 279 and the green is complete
by 288", not "the screen changes at shot N".

## What the title actually does

The frame rate OSD (top-left, read by OCR) shows 36 fps on the card, then 4 fps,
then 1-2 fps from the moment the fade begins, and it stays there for the rest of
the run. `rsx: flip` agrees: run 1 reports flip 6601 at t=273.7 s and flip 7381
at t=865.8 s, i.e. 780 flips in 592.1 s = 1.32 fps.

The guest is not dead and not blocked on a lock. The main thread spins in
`cellFiberPpuYield` and is told `CELL_FIBER_ERROR_PERM` (0x80760009) on every
call -- the HLE arm returns PERM when `!st || !st->initialized || st->current < 0`
(`runtime/src/hle/cellFiber.cpp`), i.e. the title yields outside a fibre or after
the scheduler state was torn down. Meanwhile `CreateFiber`/`RunFibers`/`JoinFiber`
continue (5 516 calls between t=280 s and t=880 s), `sceNp: ManagerGetStatus`
returns -1 and `cellNetCtl: GetInfo` returns 0x80130108, and the RSX keeps
working (last `rsx: fifo 5s` at t=898.7 s: busy 8.4 %, 518k methods/s).

So the state after the card is: **a green screen at 1-2 fps with the main thread
spinning in a refused Yield**. That is the next frontier, and it is not a crash.

## The earlier crash logs are a different state and do not resolve here

The crashes in `/tmp/rdr-ab-ref-vs-cand-{A,B}` are not the chapter card:

    run A states {'OTHER': 216, 'TITLE': 10, 'MENU': 27, 'ALERT': 21, 'WARNING': 21, 'LOADING': 6}
    run B states {'OTHER': 215, 'TITLE': 13, 'MENU': 26, 'ALERT': 22, 'WARNING': 22, 'LOADING': 6}

Neither journal contains a `CHAPTER` state. Both crashed on the `Loading...`
screen *before* the card appeared, at 220.3 s and 228.4 s. And their top frames
(`exe+0x9634e6`, `exe+0x96545c`, `exe+0x97532c`, `exe+0x83ee7d`, `exe+0x77f6e8`)
are **below `.text`** of the current binary (`.text` starts at 0xafb6f0), so
`addr2line` cannot resolve them against it. They are not a baseline for the
card.

## Two voided attempts, recorded because hiding them is worse

* Run 2 attempt A set `PS3BASE_IMPORT_NAME=main_thread` to get the per-import
  history. It emitted 15 513 016 lines / 198 MB in 400 s, starved the guest and
  was killed at 399 s. Void.
* Run 2 attempt B reused the same output directory and its screenshot timeline
  was contaminated by attempt A's files. Killed, directory deleted, harness
  changed to `rm -rf` the directory first. Void.

## The next measurement

Not "run it longer": 900 s is 5.7x the time to the card and the state is stable.
It is the refused Yield -- why the main thread is outside a fibre when it calls
`cellFiberPpuYield`, and what the real libfiber does there. The lifted
`cellFiberPpuJoinFiber` at `0x36003574` is a full implementation that waits in a
loop on the fibre's state word at +772 and writes a real exit status read from
+792; our HLE returns CELL_OK with the fibre's last recorded status even when the
log says the fibre is "still in state 3". That gap is the next thing to close.

## What changed after the cellFiber fix: the crash stopped reproducing

The 900 s run that produced the crash above ran with the corrected cellFiber
(JoinFiber waits, Yield distinguishes NOSYSINIT from PERM). Three further 900 s
runs on the same binary produced **no crash at all**:

| run | chapter card first seen | ran after the card | how it ended | crash lines |
|---|---|---|---|---|
| 1 | t+game 184.98 s | 717.0 s | killed by the 900 s bound, still running | **0** |
| 2 | t+game 176.97 s | 725.0 s | killed by the 900 s bound, still running | **0** |
| 3 | t+game 223.98 s | 676.0 s | killed by the 900 s bound, still running | **0** |

2705 s of guest runtime, zero `crash: signal` lines, and the last log line of each
run is a `cellFiber: HLE JoinFiber ... CELL_OK` at t=900.5 s. The title is alive
and looping, not crashed.

**This is a failed reproduction and it is reported as one.** It is evidence that
no crash happened within 900 s, not that none happens.

## The fault address was never a guest address

The single most important correction, verified independently:

    $ grep -m1 'vm: guest base' /tmp/rdr-fix-verify/run.log
    I   0.367463 vm: guest base 0x742c70000000 (backing: os)
    $ python3 -c "print(hex((1<<64)-0x742c70000000))"
    0xffff8bd390000000

which is exactly the reported `guest 0xffff8bd390000000`. The reporter computes
that field as `si_addr - vm::g_base`; with `si_addr = 0` it is the arithmetic
artefact of subtracting the guest base from a **null host pointer**. The same
check on the other crash log on the machine (base `0x74d2dc000000`) gives
`0xffff8b2d24000000`, also exact.

So the crash is `rip = 0`: a null host code pointer being called, not a guest
memory fault. Reading `0xffff8bd390000000` as a sign-extended 32-bit guest value
points the search at guest memory when the fault is in the host dispatcher.

Two related traps, both avoided rather than reported wrongly:

- **`addr2line` "resolves" the guest lr and the answer is meaningless.**
  `0xb583e0` is a guest address; the host `.text` starts at `0xafb6f0`, so it
  falls inside the host binary by coincidence and `addr2line` returns
  `gpu::rsx::frame_complete_body()`. It is meaningful only against the lifted
  guest source, where it is the return address of the call at `0xb583dc` into
  `f_00b57c10`, inside `f_00b58368` (`out-rdr/ppu/rdr_115.cpp`).
- **The crash block contains zero `exe+0x` offsets.** There was no frame to
  resolve: `rip = 0` cannot be matched against `backtrace()`, so the reporter
  emits the single frame `#0 ?`. That is not "addresses below .text".

## The barrel does not freeze

Measured on each run's own pixels. The reel is at x 128..190; the chapter title
text starts at x 203, so the crop is (120,575)-(200,655) — an earlier note used
(110,570)-(250,660) and therefore measured the first letter of the title too.

| run 1 pair | barrel MAD |
|---|---|
| 364 → 365 | 7.5389 |
| 367 → 368 | 5.4508 |
| 368 → 369 | 6.4044 |
| 369 → 370 | 0.0000 (card gone) |

The barrel is still animating in the last frame that shows the card. The card
then **cross-fades** out over ~10 s and the green fades in — it is a ramp, not a
cut, so "the green starts at shot N" is not a meaningful statement.

## What is still wrong

The title does not crash, but it does not finish loading either. After the card
it sits on a solid green screen (mean G = 145) at 0.6-1.5 fps, cycling
`CreateFiber`/`RunFibers`/`JoinFiber` and `sceNp: ManagerGetStatus -> -1`
forever. The loading screen never completes and the screen that follows it
(opening cinematic or gameplay) never appears.

Two defects were found while reading the code and are **not** fixed, because
this task measures:

1. `CreateFiber` leaks its 256 KB host stack (freed only on the `getcontext`
   failure path). The three runs created 5522 / 4983 / 5739 fibres, i.e. 1.35 /
   1.22 / 1.40 GiB of host stacks never freed.
2. `ThreadState*` points into a `std::vector` that can reallocate
   (`state_for()` returns `&g_threads.back().second`). RDR only creates fibres
   on one guest thread, so it does not fire here, but it is a latent
   use-after-realloc for any title that creates fibres on two threads.

## The confound, recorded rather than hidden

From run 2 onward `/root/Dev/t-T1` ran a heavy emscripten compile **without
taking the machine lock**, so load sat at 10-20 and runs 2 and 3 are not a clean
A/B against run 1 (load 2.0). That is exactly what the machine lock exists to
prevent, and it is a gap in the other project's compliance, not in this
measurement's method — but it means run 1 is the clean sample and the agreement
of all three is what makes the "no crash" observation usable.

