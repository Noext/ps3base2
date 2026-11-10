# Both loops run in wasm; the native run is simply far ahead

## The probe result

I guarded `f_005d2868` — the function the **native** `main_thread` spends its time in
(`lr 0x5d28a0`) — to see whether the wasm run ever reaches it:

    [loopA] #0 f_005d2868 r3=0x130c3fc r31=0xd00078 lr=0x2dc328
    [sleepprobe] #1 f_0021c178 r3=250 lr=0x21e378
    [loopA] #1 f_005d2868 r3=0x130c3fc r31=0xd00078 lr=0x2dc328
    ...

**Both loops run in wasm, interleaved.** So the wasm run is doing the same work as the
native one, not skipping a path. That was worth knowing and it closes the "maybe a
branch is never taken" line of inquiry.

## The real difference, measured

Thread state at the point each run was sampled:

| | native | wasm |
|---|---|---|
| `main_thread` blocked in | 141 `r3=0x1e` (**30 µs**) | 141 `r3=0x3d090` (**250 ms**) |
| total syscalls | **+10,013** | +647 |
| PPU threads alive | **10+** (Tile update, MSSAsync, MSSTimer, Chunk update, Basic Event Handler, …) | **4** |
| top syscalls | `802x6299` (mutex), `92x5647` (semaphore) | `141x410` (sleep) |

Two facts settle the comparison:

1. **The wasm run only has four PPU threads; the native one has ten or more.** The
   extra six (tile/chunk update, MSSAsync, MSSTimer, event handler) are what the title
   creates once it is far enough along. The wasm run has not reached the point where
   they are spawned.
2. **The native `main_thread` spends its syscalls on mutexes and semaphores**
   (`802`, `92`), the wasm one on `sleep` (`141`). A thread doing mutex work is doing
   work; a thread sleeping at 4 Hz is waiting.

So the earlier conclusion holds and is now much better evidenced: the wasm boot is not
deadlocked by a defect, it is **stopped several stages earlier than native**, in a poll
that its own state never satisfies because the state depends on threads the title has
not created yet.

## What that means for the gate

The chain is circular as written:

    main_thread polls -> needs SPURS handler completion
    SPURS handler 0 -> joined to the SPU group
    SPU kernels    -> wait on a reservation of 0x30028500 that nothing writes
    nothing writes -> because the work that would is submitted by threads that
                      the title creates later, which it cannot reach while polling

Native escapes because it has **ten** threads and the poll is one of many paths. The
wasm run has **four** and the poll is the only one still moving.

## The honest position

Milestone 3 is **not** reached. The specific gap between the two runs is now a
statement I can make with numbers: 4 threads vs 10+, sleep-dominated vs mutex-dominated,
and a poll that depends on threads that have not been spawned. What would close it is
making the PPU side create the threads the native run has — which is the same
"submit the first workload" problem stated from the other end.

I am reporting it as what it is: a measured gap, not a passed gate.
