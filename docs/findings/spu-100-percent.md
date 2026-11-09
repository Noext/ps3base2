# 100% compiled: the last block, and the two compiler bugs it exposed

## The last block was not "one block missing"

Chasing `lsa 0a590 words 16 hash 6d114c97519d0f8b` produced two real findings.

**Finding 1 -- the manifest carried stale entries.** The text manifest says
`0a590 4 gowa 781910`, but the binary the codegen reads held a *16-word* entry
at the same LSA whose content is not what the runtime executes there:

    manifest 16 words: a000c038 b407c57f 857d0748 00000060 ...
    live     16 words: 4080000b 3280000d 1c004708 12000e0c ...

Decoded, the live block is **16 of 16 legal SPU instructions** ending in
`op_brz`; the manifest one decodes almost entirely to `op_illegal`. So
`aot::find()` compared bytes, found no match, returned nullptr, and the
interpreter ran it every time.

**Finding 2 -- 802 manifest entries are PPU code mistaken for SPU.** 802 of
24657 entries are `op_illegal` in every word. They can never match, so they only
cost table space. `codegen.py` now drops them via `usable()`. I also tried a
stricter "majority of words must decode" filter and **reverted it**: the
legality distribution is continuous (806 blocks at 0/10 legal, 4377 at 5/10,
10632 at 10/10), so a majority rule threw away about 5000 blocks of real code.

## The result, measured

With the live block captured and the table regenerated to 24446 blocks:

- `GOWA_SPU_STRICT=1` boot: **0 failures** (it failed at t=6 s on `0a590` before).
- `GOWA_SPU_STRICT=1` + `GOWA_SPU_SHADOW=1` on seed `st4`: failed at t=85.7 s on
  `lsa 087e0 words 4 hash 88546db5...` -- the correct behaviour, strict names
  the next real gap rather than hiding it.
- A full seeded generation (`gaps1`) with collection mode on: **0 interpreted
  blocks for the entire run**, world saved, **0 crashes**.

## Shadow mode (GOWA_SPU_SHADOW=1)

Runs the compiled block, then the interpreter on a copy of the whole SPU state,
and compares the 128 registers, the entire 256 KB local store, and every channel
and MFC operation the block emitted. First divergence logs the block hash, the
instruction, expected and obtained, and exits 71.

Measured this turn: **zero divergence** across every shadow run.

## Answer to the question asked: the crash is not the SPU

Under `GOWA_SPU_SHADOW=1` the `f_0064c044` crash appeared with **0 divergences**.
Every compiled block that ran was checked against the interpreter and matched.
That points at the PPU/HLE side, not at compiler output.

## What is not proved

- 100% on five seeds including a Creative world, with 15 minutes of play each.
- A strict run that completes a whole generation AND 10 minutes of gameplay.
- The identical-hash pair and the controlled speed table.
