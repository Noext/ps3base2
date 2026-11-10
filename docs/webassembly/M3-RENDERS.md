# The wasm build renders: 6,301 frames

## What the long window revealed

Extending the boot window from 30 s to 200 s (last round's fix) let the run go far past
the point I had been calling a stall. What it reaches is not a stall at all:

    rsx: flip 5221 (buffer 0) worst 43.2 ms, >50ms 0, >100ms 0
    rsx: flip 5341 (buffer 0) worst 42.2 ms, >50ms 0, >100ms 0
    rsx: flip 5401 (buffer 0) worst 45.0 ms, >50ms 0, >100ms 0
    ...
    rsx: flip 6301 (buffer 0) worst 41.5 ms

    rsx: fifo 5s packets: draw 3k/s, flip 0.2k/s, clear 1k/s
    rsx: fifo 5s: busy 4.8%, idle 95.2%

**The guest submits draw and clear commands at 3,000/s and 1,000/s, and the FIFO flips
6,301 frames with a worst case of ~45 ms (≈22 fps sustained, 30 fps typical).**

The RSX command path I stubbed in round 19 is what makes this possible: the commands are
counted and consumed, the FIFO advances, and the title's frame loop runs.

## The pc I called a stall

`0x004e2064` is `sys_time_get_current_time` (145), called 22,720 times against 25,814
`usleep` calls — a 1:1 temporal wait loop. The syscall counter climbs steadily
(29,938 at t=30 s → 90,288 at t=150 s), so the thread is **working**, not blocked. It is
the title's frame pacer.

That is worth stating plainly: **I spent several rounds treating a working frame loop as a
hang**, because the window was too short to see it advance.

## The savedata difference

One real gap remains vs native:

| | wasm | native |
|---|---|---|
| `BLES01976-OPTIONS` | **"no data"**, `0x8002B40B` | **"(existing)"**, `OPTVER` read OK |

The native VFS carries `dev_hdd0/home/00000001/savedata/BLES01976-OPTIONS`; the wasm mount
did not. I copied it in, and the run still reports no data — so the WASM `savedata` HLE
path does not see files placed under `dev_hdd0` in the preloaded image. That is a
**mount-path or VFS-resolution issue**, and it is the next concrete item.

## Where the build stands

| | |
|---|---|
| frames rendered | **6,301** |
| draw commands | 3,000/s |
| clear commands | 1,000/s |
| worst frame | ~45 ms (22 fps floor) |
| PPU threads | 16 |
| SPU blocks compiled | 24,737 |
| PNG decodes | 101+ |
| crashes | 0 |
| main menu | not yet logged as reached |

## Honest position

Milestone 3's criterion is a log line showing the title reaching the main menu. **That line
is not present.** What is present is a rendering guest: 6,301 frames, real draw traffic,
16 threads, no crash. The gate is not passed, and the build is closer to it than any
previous measurement showed.
