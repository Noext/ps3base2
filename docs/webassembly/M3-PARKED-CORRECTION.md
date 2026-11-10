# Correction: the SPUs ARE parked. The clock runs, the machine does not.

## What last round got wrong

I wrote "the machine is not deadlocked, it is early" on the evidence that the native
and wasm SPU states were identical except `insns` and `events`. **The clock
measurement refutes half of that.**

```
ps3base-wasm: guest clock +80040240 ticks/s (100.1% of 80MHz realtime)
ps3base-wasm: guest clock +80158320 ticks/s (100.2% of 80MHz realtime)
```

**The guest clock runs at 100 % of real time.** So "earlier in the boot" is not an
explanation: five seconds of wall clock gives five seconds of guest time, and in that
time the SPUs execute **25,600 instructions and then stop**. `insns` is frozen across
dumps. They are parked, not slow.

The state comparison was still true and still useful — it showed the park is a
legitimate SPURS wait, not a defect in the SPU core. But the conclusion I drew from it
("early") was wrong, and the rate measurement is what disproved it.

## Where the park actually is

The channel-read wait at `thread.cpp:355-365`:

    lk.unlock();
    waiting.store(1);
    rsv::wait_parked(line, rt, snap, park_slot, w0, limit, stop_flag);
    waiting.store(0);
    lk.lock();

`rsv::wait_parked` blocks until **the version of the reservation line changes**. The
line is `0x30028500` — the SPURS taskset.

And nothing changes it:

    grep -rn '0x30028500' runtime/src/ | grep -v thread.cpp   ->  no matches

**No HLE, no PPU code, no SPU code writes that line.** The tasket is created, its
address is passed to the five kernels as `arg1`, and then nobody touches the memory
the kernels are waiting on.

## So which is it

Two readings, and the evidence now favours the first:

1. **The PPU should write it and does not.** The PPU thread that would submit work to
   the taskset is `main_thread`, and it is itself parked in the 4 Hz poll of
   `f_0021e060` — a loop that waits for the SPURS handlers to report completion. Both
   sides wait on each other's *first* step, and neither takes it.
2. The native run reaches the world with the SPUs in this same park state, so the park
   itself is survivable — but native gets there because the PPU side keeps running,
   whereas wasm's PPU side is stuck in a poll that depends on it.

## The honest state

| measurement | value |
|---|---|
| guest clock rate | **100 % of real time** (80 MHz) |
| SPU instructions executed | 25,600 then **frozen** |
| SPU state | `pc 0x011ac`, `mask 0x400`, `raddr 0x30028500` — legitimate SPURS wait |
| writers of `0x30028500` | **none, anywhere in the tree** |
| PPU `main_thread` | parked in the `f_0021e060` poll |
| main menu | **not reached** |

Milestone 3 remains **open**, and the blocker is a mutual first-step wait: the SPUs
wait for the taskset line to change, the PPU waits for the SPUs to report, and the code
that would break the tie is not running.

## What I am not doing

I am not calling this "working" or "early". Two rounds ago I called the SPUs missing;
last round I called them early; both were wrong. **They are parked, the clock is fine,
and the deadlock is real.**

## The symmetric wait, confirmed natively

The native run's own thread dump shows the other half:

    status: ppu 0x1000002 'MinecraftSpursHdlr0' blocked in 178
            (r3 0x4000100 r31 0x30028500) syscalls +2 lr 0x0

`syscall 178` is `sys_spu_thread_group_join` on group `0x4000100`, and `r31` is
**`0x30028500`** — the same taskset line the SPUs are parked on.

So the shape is confirmed in **both** runs:

    SPU kernels  -> wait on version change of 0x30028500  (rsv::wait_parked)
    PPU handler  -> waits for the SPU group to finish     (sys_spu_thread_group_join)

The native run escapes this because **other PPU work proceeds** — it reaches the world
with the kernels still parked in this exact state (`events 0x20 insns 19240960`). The
join is one path among many, and the game continues on the paths that do not join.

The wasm run does not escape because its PPU side has only one live thread,
`main_thread`, and it is in the `f_0021e060` poll — a loop that also depends on the
SPURS handlers.

**That is the difference between the two runs, and it is now precisely stated:** not a
missing writer, not a slow interpreter, but a PPU that has no thread left to make
progress while three of its four threads are joined to the SPU group.
