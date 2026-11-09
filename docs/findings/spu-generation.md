# SPU during world generation: 65% -> 0.00% interpreted

## The measurement asked for: the counter during generation, every 20 s

Same binary, same VFS base, seeded creation, `GOWA_SPU_JOBS=1` printing the
interpreted share, `GOWA_SPU_LOUD_INTERP=1` naming each unknown block by hash.

**Before compiling the generation blocks:**

    interpreted 65.01% of 670283600
    interpreted 68.47% of 731355309
    distinct never-compiled blocks: 1129

**After compiling them:**

    interpreted 0.00% of 333052819
    interpreted 0.00% of 373294753
    distinct never-compiled blocks: 1

So the answer to "if the interpreted share rises during generation, that is the
cause of the missing gain" is **yes, it rose, and that was the cause**: the AOT
table had been built from a manifest of the ELF and firmware plus one boot
capture, and world generation uploads about a thousand blocks the table had
never seen.

## The loop, run twice

1. `GOWA_SPU_RECORD=/tmp/spu-caps/generation.bin` on a full seeded generation
   with a clean VFS. It captured **3502 blocks** (the recorder counts distinct
   hashes, and generation re-uploads shared code paths many times).
2. Install it beside the manifest and regenerate:
   ```
   cp /tmp/spu-caps/generation.bin out/gen/blocks/run_generation_01.bin
   recomp/spu/codegen.py out/spu out/gen/blocks/run*.bin
   ```
   Table went **21449 -> 25059 blocks** (63 generated sources, 1065 + 5343
   regions).
3. Rebuild, re-measure: 65% -> 0.00%, 1129 -> 1 unknown block.

## One block remains, and it is named

    INTERPRETED BLOCK lsa 16774 words 29 hash f3e2cfcd654a3938 (total distinct 1)

One block out of ~25 000, hit once at t=42 s. It was not captured because the
recording run and this run took different creation paths (different seed), so it
needs one more record/compile cycle to reach zero.

## The safety net, proven both ways

`GOWA_SPU_STRICT=1` fails the run at the first interpreted block:

    EXIT=70
    INTERPRETED BLOCK lsa 00848 words 11 hash 9835e6b75f04dd45 (total distinct 1)
    STRICT: ... 100% compiled is violated, failing the run

With `GOWA_SPU_NO_AOT=1` it fires at t=0.29 s; on a normal 120 s run it fired
zero times. The exit path now always prints

    SPU VERDICT: N distinct interpreted blocks, M interpreted instructions -- 100% compiled / NOT fully compiled

so no run can end without stating the number.

## Seed handling: fixed, and the earlier comparison voidness explained

The driver now walks toward the Seed row and **requires** the ring to be on it
(`on_seed_row`, band ±26 around y=113, measured drift 113/127/131) before
pressing z. If the row is unreachable it returns exit 3 rather than typing blind.
`seed_visible()` is **deleted** -- it was a stub returning True, and a check that
cannot fail is worse than no check. `tools/compare_worlds.py` reads RandomSeed
from both saves and exits 2 (void) when they differ, before looking at any hash.

Verified working in this turn's runs: `seed entered on the Seed row: gen1`,
`gen2`, `cap1` each after printing the walk `y=65 -> y=136`.

**The two-hash comparison is still not done.** Both generation runs in this turn
ended in the known `f_0064c044` crash before saving (g2 crashed at t=464 s in
`(4J) Save thread 0`), so there is no saved world to hash. The tool that would
refuse a bad comparison is in place; the pair of worlds is not.

## What is proved and what is not

Proved: the interpreted share during generation went from 65% to 0.00% by
compiling runtime-uploaded blocks, measured by the same counter, with 1 unknown
block left named by hash; the strict guard fails when it should.

Not proved: a full 25 000-block convergence to zero across five seeds with 15
minutes of gameplay each; the identical-hash pair; controlled generation times
and fps.
