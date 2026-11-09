# Convergence report (in progress)

## (1) The AOT table keeps every variant at an LSA

Verified, not assumed. The index is a map from LSA to a **vector** of blocks and
`aot::find()` `memcmp`s each candidate at that address. `tests/spu_aot_variants.cpp`
proves it with two genuinely different 4-word blocks at `0x2000`:

- A is found when the local store holds A, B when it holds B;
- they are distinct blocks, not one;
- unknown content at a known LSA matches nothing;
- equal bytes at a different LSA do not alias;
- a block running past the end of the local store is rejected, not read out of
  bounds.

9 checks, 0 failures. Ordering rule it documents: `find()` returns the **first
declared match**, so when a shorter entry shares an address with a longer one,
declare the longer first to get more instructions per dispatch.

## (2) tools/spu_converge.sh

The loop is: run a scenario under `GOWA_SPU_STRICT=1`; on failure collect every
gap with `GOWA_SPU_COLLECT`; merge, regenerate the table, rebuild; retry the same
scenarios until all pass.

Six scenarios: five seeded generations (one Creative) each followed by 900 s of
scripted play, plus loading an existing world.

### Round 1 results

| scenario | gaps collected |
|---|---|
| gen-survival-1 (seedA) | 49 distinct blocks (2408 bytes) |
| gen-survival-2 (seedB) | crashed early, 0 |
| gen-creative-3 (seedC) | 3728 bytes |
| gen-survival-4 (seedD) | 1248 bytes |
| gen-survival-5 (seedE) | 2976 bytes |
| load-existing (seedA) | 2044 bytes |

135 distinct blocks merged, table 24446 -> 24585, rebuild, round 2 started.

### Round 2 shows the loop working

- gen-survival-1: 2888 bytes (different gaps than round 1 -- a 900 s play
  session explores code paths a shorter one does not)
- gen-survival-2: **32 bytes**, down from 2408 -- nearly converged
- gen-creative-3: crashed early, 0 gaps
- gen-survival-4: crashed early, 0 gaps
- gen-survival-5: 1760 bytes

## A defect in the loop that measurement exposed

The Creative and seedD scenarios crash early with **zero collected gaps**. The
first version of the script counted those as failures needing blocks, so it
would have regenerated an empty set every round and never terminated. A crash
with no SPU gap is not an SPU coverage problem.

`run_scenario` now returns a distinct code 4 for that case, and the round reports
those scenarios separately as *not SPU-judgeable* rather than as coverage debt.
Convergence is then declared on the scenarios that can be judged, with the
crashing ones named explicitly.

This is the same class of mistake as the capture classifier: a signal that
cannot distinguish two situations, treated as if it could.

## Not yet done

- The loop has not reached a round where every judgeable scenario passes.
- The hash pair and the speed table (step 3) require the machine to be idle and
  a single run at a time, so they come after the loop finishes.
