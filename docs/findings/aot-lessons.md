# Two AOT lessons from the 100% campaign

## Lesson 1: several blocks can share one LSA, and they must be told apart by content

On SPU a local store address is not a stable identity for a piece of code. SPURS
tasks overlay their own images: task A's function at LSA `0x2000` and task B's
different function at the same `0x2000` are both real, at different times. A
table keyed by address alone keeps one and silently sends the other to the
interpreter forever.

That is exactly what happened at `lsa 0a590`. The table held a 16-word entry
there, but not the content the runtime executes at that address, so
`aot::find()` compared bytes, found nothing, returned nullptr, and the
interpreter ran the block on every pass. `GOWA_SPU_STRICT=1` named it:

    INTERPRETED BLOCK lsa 0a590 words 16 hash 6d114c97519d0f8b

**The structure was already right**: the index is a map from LSA to a
*vector* of blocks, and `find()` `memcmp`s every candidate at that address
before choosing. What was wrong was the table's *content*, not its shape. The
fix was to capture the live block and add it beside the stale one -- the two now
coexist, and the runtime picks whichever matches the bytes it is about to run.

`tests/spu_aot_variants.cpp` locks the behaviour in with two genuinely
different 4-word blocks at `0x2000`: A resolves to A, B resolves to B, they are
distinct, unknown content matches nothing, equal bytes at another LSA do not
alias, and a block running past the end of the local store is rejected rather
than read out of bounds. 9 checks.

Ordering rule worth knowing: `find()` returns the **first declared match**, so
when a shorter entry shares an address with a longer one, declaring the longer
first gives more instructions per dispatch.

## Lesson 2: PPU code that looks like SPU is worse than missing code

802 of 24657 manifest entries decode to `op_illegal` in **every** word. They are
PPU instructions that a static scan located inside an SPU image. They cannot
ever match what the interpreter runs, so they only cost table space and
generated code size -- and, worse, they made the table *look* complete.
`codegen.py` now drops them (`usable()`).

The tempting next step is to also drop entries where *most* words are illegal.
**Do not.** Measured across every captured block, the legality distribution is
continuous, not bimodal:

    legal  0/10:   806 blocks      legal  5/10:  4377 blocks
    legal  1/10:   131 blocks      legal  8/10:  1254 blocks
    legal  3/10:   600 blocks      legal 10/10: 10632 blocks
    legal  4/10:   860 blocks

A majority rule therefore cuts through real code: I tried it and it removed
about 5000 blocks, taking the table from 24445 to 19947. Only the all-illegal
case has a defensible boundary. When a threshold is not a gap in the data, it is
a guess wearing a metric's clothes.

## Why the strict counter is what made this findable

Neither bug was visible in aggregate numbers. The interpreted share sat at 0.00%
for whole sessions while individual blocks kept falling through, because one
hot block out of 25000 barely moves a percentage. The per-hash
`INTERPRETED BLOCK` warning and `GOWA_SPU_STRICT=1` name the block directly, and
that is what turned a percentage into an address, an offset and a fix.
