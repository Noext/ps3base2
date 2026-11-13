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
