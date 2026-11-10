# The SPU wait, named exactly

## What the probe found

The first probe (inside `SpuThread::block`) printed **nothing**, which was itself the
finding: the SPURS kernels never reach `block()`. The second probe, placed in the
channel-read spin (`event_count` loop), fires immediately:

    spuwait: slot 0 'MinecraftCellSpursKernel0' pc 0x011ac event_mask 0x400
             events 0x0 raddr 0x30028500 insns 21504

    ... repeated, insns frozen at 21504 ...

- **`event_mask 0x400`** is `EV_LR` — a pending channel read.
- **`raddr 0x30028500`** is the **reservation address** (a `getllar` line), not a
  channel number.
- **`insns` frozen at 21504** — the SPU is not executing while parked.

So the wait is: *a `putllc` needs a valid reservation on `0x30028500`, and until one
is granted the SPU sits on `EV_LR`.*

## The obvious hypothesis, tested and rejected

`putllc` failing would park the SPU exactly this way (`set_events(EV_LR)` at
`thread.cpp:1174`). A counter on both outcomes says otherwise:

    spuput: putllc slot 0 line 0x30028580 ok=4 fail=0
    spuput: putllc slot 0 line 0x30028500 ok=8 fail=0
    spuput: putllc slot 4 line 0x30028500 ok=43 fail=0

**Zero failures.** The MFC reservation path works. The SPUs commit their stores
successfully, then park at `0x011ac` anyway.

## What that means

The SPU kernels finish their startup, commit their writes, and then wait at a point in
the SPURS kernel that expects **a signal from outside**:

- either another SPU task (the workload the taskset would run, which the PPU never
  submits because it is itself parked), or
- a PPU-side write to `0x30028500` that would invalidate the reservation and wake the
  reader.

Both halves of the machine are now measurably waiting on each other, and neither is
broken: the PPUs wait on SPURS event queues, the SPUs wait on a reservation that only
a PPU write would break.

## Where the port stands

| | |
|---|---|
| firmware libraries | 6 |
| PRX modules started | 24 |
| PPU threads | 4 (main polling at 4 Hz, 3 parked in SPURS waits) |
| SPU threads | 5, executing then parked at `0x011ac` |
| SPU blocks compiled | 24,737 |
| putllc ok / fail | **43 / 0** |
| crashes | 0 |
| main menu | **not reached** |

Milestone 3 remains **open**. The blocker is no longer "a missing model" or "a missing
module": it is a mutual wait whose two sides are both identified, and whose resolution
is a single question: what should write `0x30028500` after the SPU commits.
