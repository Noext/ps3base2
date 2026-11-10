# The wasm main_thread is in a different function than the native one

## The measurement

A guard on `f_0021e060` — the 250 ms worker loop — shows the wasm `main_thread`
enters it **once** and never leaves:

    [loopB] #0 f_0021e060 r3=0xce5c28 lr=0x221adc
    [sleepprobe] #1 f_0021c178 r3=250 lr=0x21e378
    [sleepprobe] #2 f_0021c178 r3=250 lr=0x21e378      (forever)

Its caller is `f_002219d8` at `mc_021.cpp:5546`, which runs a start-up sequence
(`f_008554e8`, import 174, `f_003316d4`, `f_0021b900`, `f_002d635c`, `f_0021e8bc`) and
then **falls into the work loop as its main body**. That is correct PPU code: this
function *is* a thread's entry point, and its loop *is* the thread's life.

## The comparison that matters

| | `main_thread` lr | where that is |
|---|---|---|
| native | `0x5d28a0` → `f_005d2868` | called from `mc_028` (the rendering path) |
| wasm | `0x21c18c` → `f_0021c178` | called from `f_0021e060`, the worker loop |

And `grep -ac '0x221adc|0x21e060' /tmp/nat14.log` returns **0**: the native
`main_thread` **never enters** the worker loop at all. The wasm `main_thread` never
leaves it.

So the two runs' `main_thread`s are executing **different functions**. The wasm one has
taken a path the native one never takes, and it is a path that loops forever by design.

## What that implies

`f_002219d8` is a worker whose body is an infinite loop. It is *supposed* to be run by
a thread that does nothing else. The native run's `main_thread` is not that thread —
it is in the rendering path — which means in native, this worker is **another thread**,
started by someone else.

The wasm run appears to have started this worker as (or inside) its main thread. That
is a scheduling difference, not a lifting or HLE defect: the same function exists and
runs correctly; it is *which thread runs it* that differs.

## What I can and cannot claim

**Can claim, measured:** the wasm `main_thread` is in `f_0021e060`; the native one is
in `f_005d2868`; native never enters `f_0021e060`; the wasm entry never leaves it.

**Cannot claim:** why the wasm run's thread creation did not spawn a separate worker.
That needs the `sys_ppu_thread_create` call log — which thread the guest asked for and
which one it got — and that is the next measurement.

Milestone 3 remains **open**. This round narrowed the difference from "4 threads vs
10+" to a **specific function executed by the wrong thread**, which is the smallest and
most actionable statement of the gap so far.
