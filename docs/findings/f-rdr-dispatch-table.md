# RDR's 17-entry dispatch table at 0x013e5c8c: what was measured

**Measured 2026-11-13.** This note exists because the resume prompt for this
session carried a premise that was already false when it was written, and a
reader who trusts it will spend a session re-deriving a fixed thing.

## The premise that was stale

The prompt said:

> Aucune des 17 n'est liftée. Le relift avec la règle `run_extent` de Q1 ne les
> produit pas : le total reste à 76 793 fonctions.

Both halves are wrong at HEAD `2a67f4f`, and the run that followed proves it.
The `run_extent` rule is in `main` (`recomp/ppu/analysis.py`, the
`elif default_off is None and bound is None` branch), and it recovers the table on
the real image. Measured directly, on `bin/rdr/EBOOT.elf`:

    >>> jump_table(img, 0x00ae5178)
    base 0x013e5c8c, 17 targets, 0x00ae517c .. 0x00ae58b8
    >>> explore(img, 0x00ae50f0, set())
    522 instructions, 0 errors, 0 indirect jumps left
    all 17 targets are labels of the function
    17 bcctr sites (0x00ae5178 .. 0x00ae58b4) all resolve to 0x013e5c8c

And the emitted code contains the switch and its labels:

    $ grep -c 'case 0xae5818' out-rdr/ppu/rdr_110.cpp
    17
    $ grep -c 'L_ae5818:' out-rdr/ppu/rdr_110.cpp
    1

The function total is 76 793 in the prompt and 76 793 here, but that number does
not measure what the prompt thought it measured: the 17 targets are **labels of
an already-lifted function**, not new function entries. A table that resolves
inside an existing function cannot move the function count. The prompt used a
counter that is blind to the thing it was asking about.

## The bctr address in the prompt is wrong

The prompt gives:

    bctr = 0x00adc030     # le bctr qui dispatche

`0x00adc030` is not a bcctr. It is the instruction **after** the `bl` at
`0x00adc02c` that calls the dispatcher -- it is the return address the crash
report printed as `lr`. Read from the image:

    00adc02c  480090c5  bl    0x00ae50f0
    00adc030  8b41008c  lbz   r26, 0x8c(r1)     <- not a branch at all

The real dispatch is:

    00ae5174  7c8903a6  mtspr ctr, r4
    00ae5178  4e800420  bcctr                  <- this one

A `switch_tables.toml` keyed on `0x00adc030` would be loaded, validated (the
address is inside the text range), and never consulted, because no bctr is at
that address. That is the silent failure the loader's validation does not catch:
it checks that the bctr is in the code range, and `0x00adc030` is.

## What was done

1. **The hand-written table**, `out-rdr/gen/switch_tables.toml`, keyed on the
   real bctr `0x00ae5178`, base `0x013e5c8c`, 17 entries. The file is derived
   material and stays out of git, like the seeds; the format is documented in
   `load_switch_tables()`.

2. **The manual path was verified to reach `explore()`, not just the log.** This
   is the trap the prompt names -- the path was once "announced then ignored".
   The check is a lift with the automatic detectors switched off, so the only
   possible source of the table is the file:

       PS3BASE_NO_AUTO_TABLES=1 python3 tools/setup.py --steps ppu ... \
           --out-root /tmp/rdr-manual/out --gen-root /tmp/rdr-manual/out/gen

   Output:

       switch tables: 1 hand-written entr(y/ies) from .../switch_tables.toml
       rdr              switch table at bctr 0x00ae5178: 17 target(s) from 0x013e5c8c
       total functions 76788 imports 447 exports 860
       LIFT EXIT=0

   and the emitted file carries it:

       $ grep -c 'case 0xae5818' /tmp/rdr-manual/out/ppu/rdr_110.cpp
       1
       $ grep -c 'L_ae5818:' /tmp/rdr-manual/out/ppu/rdr_110.cpp
       1

   Note the function total: 76 788, five FEWER than the automatic lift's 76 793.
   That is the expected sign, and it is the second proof the table was consumed:
   the automatic pass had split what it could not see into extra function
   entries, and the manual table folds them back into one function's cases.

3. **A test on the real bytes**, `tests/ppu_run_extent_real_bytes.py`. The
   existing `tests/ppu_run_extent_table.py` builds its shape from synthetic
   instructions, and it passed while the real dispatch was still failing -- it
   encodes the author's idea of the code. The new test carries the 36 instruction
   words of the dispatcher window and the 18 words of the table from the ELF,
   checks the fixture against the ELF itself when the disc is present (so the two
   cannot drift), and asserts the three claims that matter: the bctr is the
   bcctr, the run ends at the zero word, and all 17 targets are labels of the
   function with zero indirect jumps left.

## What the run shows

One instance at a time, under `/tmp/ps3base-game.lock`, 240-250 s budget,
`tools/drive_rdr.py --branch cross`, with the hand-written table in place:

| run | load before | flips | crashes | unknown indirect | last screen |
|---|---|---|---|---|---|
| A | 5.46 5.65 7.50 | 4921 | 0 | 0 | EXODUS IN AMERICA |
| B | 4.85 7.59 8.27 | 6061 | 0 | 0 | EXODUS IN AMERICA |
| C | 11.09 11.12 9.65 | 5341 | 0 | 0 | EXODUS IN AMERICA |
| D (A/B candidate, load 22.55) | 22.55 17.01 14.32 | 3421 | **1** | 0 | LOADING |

Three of four runs reach the chapter card and none of them logs
`unknown indirect target 0x00ae5818`. The fourth crashed, and it is reported
here rather than dropped: see "the crash that is still there".

## The crash that is still there

Run D faulted at 228 s, after the chapter card, at LOADING:

    W 228.278816 cellFiber: RunFibers: 100000 switches without finishing; stopping
    I 228.278876 cellFiber: HLE RunFibers(0x0165d580) ran 100001 fibre switch(es) -> CELL_OK
    W 228.278884 cellFiber: JoinFiber #7: still in state 3 (not yet terminated)
    I 228.278891 cellFiber: HLE JoinFiber #7 -> status 0x0, CELL_OK
    I 228.365228 cellFiber: HLE CreateFiber #8 entry=0x01385418 ...
    E 228.388168 crash: signal 11 ... fault (nil) (guest 0xffff8b2d24000000)

This is NOT the dispatch table and NOT the old fibre crash. Three facts:

- The old fibre crash was `fault ...010 (guest 0x10)` through libfiber's
  back-chain epilogue under LLE. This one is `fault (nil)` at guest
  `0xffff8b2d24000000`, a value that is a sign-extended 32-bit address, and it
  happens with the HLE fibre in place.
- The 100 000-switch guard fires in EVERY run, including the three clean ones
  (9, 7 and 4 times), and it recovers: the scheduler returns CELL_OK and the
  title continues. So the guard is not itself the failure.
- What precedes the fault is `JoinFiber` returning `CELL_OK` with status 0 for a
  fibre the log itself says is "still in state 3 (not yet terminated)". The
  console would block there. The HLE returns a status the fibre never produced,
  and the next thing the title does is create fibre #8 and fault.

That is a real deviation from the console and it is the next thing to fix. It is
recorded, not fixed, because fixing it means deciding what a join of a live
fibre must do (block, or run it), and that decision deserves its own measurement.

## What this does NOT show

The A/B against `/root/Dev/reg3/build-rdr/ps3base` **does not attribute** the
chapter screen to the table, and the reason is worth writing down so the next
reader does not repeat the attempt:

- The reference lift has no switch at all:
  `grep -c 'switch (uint32_t(ctx.ctr))' reg3/out-rdr/ppu/rdr_110.cpp` = 0, and no
  `L_ae5818` label.
- In a 420 s A/B it still logged zero `unknown indirect`, because it **crashed
  first**: `fault ...010 (guest 0x10)` at 220 s, which is the OLD fibre crash
  (that binary predates the cellFiber HLE). It never reached the dispatch.

So the reference is not a usable control for this question: it dies earlier of a
different cause. The attribution rests on the lift-level measurements above --
17 targets that are all labels of the function, 0 indirect jumps left, and a
manual-only lift that produces the table from the file alone -- not on a run.

Reaching `0x00ae5818` at run time needs a run that gets past the chapter card,
which is blocked by the fibre defect above.

