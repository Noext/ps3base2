# The wait is a 4 Hz poll that never satisfies, and the branch reads the opposite

## What the probes measured

```
[pollprobe] #0 idx=0x0 ea=0x00ce5aa0 byte=1
ps3base-wasm: hist yield43=0 usleep141=483
...          usleep141 climbs by exactly 4 per second
```

- The polled byte is **1**, not 0.
- The loop sleeps 250 ms and repeats: **4 iterations per second**, forever.
- `yield43` never fires, so the branch taken is always the `usleep` one.

## What that means for the branch

The lift at `mc_021.cpp:1414`:

    f_0021da50();                                  /* returns the byte */
    cr_set(0, (int32_t)r3, 0);                     /* eq = (byte == 0) */
    if (bc_cond(4, 2)) goto L_21e360;              /* loop */

And `bc_cond(4,2)` with `bo=4` has the CTR bit set, so it reduces to
`crbit(2)` = `cr[0].eq` = **(byte == 0)**.

**The byte is 1, so `eq` is false, so the branch should NOT be taken.** Yet the loop
runs. That is a contradiction in my reading of the generated code, not in the guest,
and it means one of these three is true:

1. what `f_0021da50` returns is not the byte I read at its entry — the function may
   overwrite `r3` before returning (its body is two instructions, so this is
   checkable and is the most likely);
2. the loop that runs is at a different call site (`lr=0x21e268` vs `0x21e378`),
   which the sleep probe showed exist as two distinct paths;
3. the `cr` field read is not the one written because another call clobbers `cr0`
   between `cr_set` and `bc_cond` — `f_0021da50` is a call and could.

I am not going to guess between them. **The next round runs the same probe on the
branch instruction itself** (log `cr[0]` and `r3` at `0x21e384` and `0x21e388`),
which distinguishes the three in one run.

## Correction to the previous round

I wrote that the guest "polls a state table that nothing in this build populates".
That was wrong twice over: the table **is** written (35 writes, some non-zero —
measured last round), and the byte at the polled address is **1**, not 0. What is
actually happening is more specific: the loop condition reads as satisfied but the
branch is taken anyway, which points at the code's register/CR handling rather than
at a missing subsystem.

## Where the port stands

| measurement | value |
|---|---|
| firmware libraries | 6 |
| PRX modules started | 24 |
| `f_005d2b44` (the old null-this crash) | runs 40x with `r3=0x130c3fc` |
| RSX command path | stubbed, counting, no longer blocks |
| current state | 4 Hz poll loop, byte=1, branch still taken |
| syscalls / 30 s | ~850 (down from 192k) |
| crashes | 0 |
| main menu | **not reached** |

Milestone 3 remains **open**.
