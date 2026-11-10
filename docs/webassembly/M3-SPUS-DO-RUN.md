# The SPUs DO run. My previous round was wrong.

## Correction first

Last round I wrote: "no SPU execution, so the PPU parks on SPURS waits" and called
the missing SPU model the structural blocker. **The measurement says otherwise.**

    spu: thread 0x100   'MinecraftCellSpursKernel0' start pc 0x00848 args 0x0
    spu: thread 0x2000100 'MinecraftCellSpursKernel2' start pc 0x00848
    spu: thread 0x1000100 'MinecraftCellSpursKernel1' start pc 0x00848
    spu: thread 0x3000100 'MinecraftCellSpursKernel3' start pc 0x00848
    spu: thread 0x4000100 'MinecraftCellSpursKernel4' start pc 0x00848
    spu: 24737 compiled SPU blocks

**Five SPU threads start, and they execute.** A later dump shows real instruction
counts, and they are *still climbing* between dumps:

    spu 0x100   insns 27648  pc 0x011ac  mask 0x400  events 0x0  running 1
    spu 0x2000100 insns 8192 pc 0x011ac  mask 0x400  events 0x0  running 1
    spu 0x1000100 insns 7168 pc 0x011ac  mask 0x400  events 0x0  running 1
    spu 0x3000100 insns 5120 pc 0x011ac  mask 0x400  events 0x0  running 1
    spu 0x4000100 insns 3072 pc 0x011ac  mask 0x400  events 0x0  running 1

    (later) spu 0x100 insns 28672 -- 27648 -> 28672, so it is advancing

Emscripten's pthread pool did **not** block them. `-sPTHREAD_POOL_SIZE=4` is the
pre-created pool, but Emscripten grows it on demand for `std::thread`, so ten guest
threads live happily. **The unwind-on-switch work I planned this round is
unnecessary**, and I am not going to build it after measuring that its premise was
false.

## What the SPUs are actually doing

All five sit at **`pc=0x011ac`** with `mask 0x400` and `events 0x0`. That is the
SPURS kernel's wait loop: each SPU is parked waiting for a channel event, and the
event mask never becomes non-zero.

`insns` differ per thread (27648, 8192, 7168, 5120, 3072) because they start at
slightly different times, and they keep climbing — so they are looping, not hung on a
lock.

## What that means for the gate

The PPU handlers wait on SPURS event queues; the SPUs wait on channel events. **Both
halves are alive and both are waiting on each other's event**, which is either:

1. a deadlock caused by an event that our HLE should post but does not, or
2. normal SPURS startup where the first workload is never submitted because an
   earlier step failed.

Distinguishing them needs the channel-event log: which channel each SPU waits on,
and whether anything writes it. That is the next measurement, and it is a much
smaller question than "build an SPU execution model".

## Positioning

Milestone 3 is still **not** reached. But the state is better than last round's
report claimed: the machine has five SPUs executing, four PPUs scheduled, six
firmware libraries loaded, 24 PRX modules started, the RSX path stubbed, and zero
crashes. The remaining blocker is an event that never arrives, not an execution model
that is missing.
