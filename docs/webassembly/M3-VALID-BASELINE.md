# A valid native reference exists, and the gap is now a number

## A correction to my own baseline

Since round 25 I have compared the wasm build against `/tmp/nat14.log`. That log **crashed**:

    E crash: signal 11 at ... (std::ostream::sentry::sentry(...))
    W blackbox: capture #0: crash

`std::ostream::sentry` is the stack-overflow-in-the-decompiler failure the code itself
documents (32 MB pthread stack, "the overflow corrupted an ostream"). **That run never
reached the menu either**, so several "native reaches X and wasm does not" comparisons were
made against a run that had itself failed.

## The valid reference

`/tmp/cmp.log` is a native run that reached the world: 34 `UserAutoSave`/world markers, a
9,126-second session, and real rendering numbers.

| | valid native (`cmp.log`) | wasm (this round) |
|---|---|---|
| `draw` rate | **59–65 k/s** | **3 k/s** |
| `flip` share | 29–30 % | 93 % |
| `UserAutoSave` | **yes**, repeatedly | **no** |

Two clean facts:

1. **The wasm build draws ~20× less per second than a working native session.** Its FIFO is
   93 % *flip* and almost no draw — it is presenting frames that are mostly empty.
2. **It has never issued a `UserAutoSave`**, which is the marker that the title has a live
   world to save.

So the wasm guest is not "at the menu drawing quietly": it is running a frame loop that
flips nearly empty buffers, 20× lighter than the real workload.

## What that means, stated as a hypothesis again

The RSX stub from round 19 consumes and **counts** commands but does not execute them. A
title whose draws do nothing still paces its frames; it just has nothing to show. The 93 %
flip share is consistent with exactly that.

The honest conclusion is therefore not "the menus are invisible but present" — it is that
**the draw traffic exists but is 20× too thin**, and the `UserAutoSave` marker never fires.

## Honest position

Milestone 3 remains **open**, and this round's contribution is to replace a bad baseline
with a good one and to put the remaining gap in numbers: 3k vs 60k draws per second, no
`UserAutoSave`, 93 % flips. The next step is to find why the title submits so few draws —
which is a rendering-pipeline question, not a scheduling one.
