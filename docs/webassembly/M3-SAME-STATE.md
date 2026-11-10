# The wasm SPUs are in the same state as the native ones

## The comparison that reframes the problem

Native run, at t=86s (after reaching the world):

    status: spu 0x100 'MinecraftCellSpursKernel0' pc 0x011ac ie 0 mask 0x400 events 0x20 raddr 0x30028500 insns 19240960

Wasm run, at t=5s:

    status: spu 0x100 'MinecraftCellSpursKernel0' pc 0x011ac ie 0 mask 0x400 events 0x0  raddr 0x30028500 insns 24576

**Identical in every field except `events` and `insns`.**

- `pc`, `ie`, `mask`, `raddr` — the same. The SPURES kernel waits at `0x011ac` on a
  reservation for the taskset at `0x30028500` in both.
- `insns` — native 19,240,960; wasm 24,576. The native has run **780x more
  instructions**.
- `events 0x20` — `EV_TM`, the decrementer timer. That is a *transient* bit set when
  `elapsed > dec_value` and cleared by the next write. The native log happened to
  catch it set; the wasm log caught it clear.

**So the wasm SPUs are not stuck in a state the native ones never enter. They are in
exactly the state the native ones are in, just much earlier.**

## What the gate values say

The probe on `check_interrupt` shows why the timer has not fired:

    spuint: slot 0 ie=0 mask 0x400 events 0x0 dec_value 922190431 elapsed 5485840 raddr 0x30028500

`elapsed 5.5M` against `dec_value 922M` — the decrementer has **0.6 % of its budget
consumed**. It will fire after roughly 170x more SPU time.

And `ie=0`: interrupts are disabled, which is correct because the SPU is inside a
`wrch`/`rdch` sequence. `check_interrupt` returns early on `ie=0` by design; the
timer path is not even consulted while the channel sequence is in progress.

## What this means

There is no defect here. The wasm run is at **t≈5 s** of a boot that the native run
needs **86 s** to complete, and the SPUs are parked at a legitimate wait with 99.4 %
of their decrementer budget remaining.

**The machine is not deadlocked; it is early.** And "early" is survivable: the previous
rounds' waits (RSX field, SPURS queue) were each resolved by making the boot progress,
not by finding a missing writer.

The remaining question is therefore different from the one I posed last round. It is
not "what should write `0x30028500`" — the native run does not write it either, it
just runs 780x longer first. It is: **what is the wasm boot doing with its time such
that 5 seconds of wall clock yields 24k SPU instructions where native yields 19M?**

That is a throughput measurement, and it is the natural next step.

## Positioning

Milestone 3 remains **open**. But the diagnosis has inverted: the SPUs are healthy and
early, not blocked, and the outstanding item is throughput of the SPU interpreter
under wasm rather than a missing piece of the emulator.
