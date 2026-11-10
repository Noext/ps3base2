# The null global: no writer exists, in either run

## The measurement

A write watch on the line containing `0x00D6E800` was armed in the wasm run, using
the existing `cw::note_line` machinery (every store goes through
`rsv::store_range`, which calls `check_store` and `note_hit`):

    I codewatch: line watch armed on 0x00d6e800 (t=0.83)
    F ppu: unknown indirect target 0x00000000 (lr=0x005d2c08)

**No write to that line was ever reported, before or at the crash.** The same watch
was then armed on a **native** run (`PS3BASE_CODEWATCH_NOTE=d6e800`):

    watch hits in the native log: 0

**Neither run ever writes `0x00D6E800`.** So this is not a value that was clobbered,
not a store the port dropped, and not an HLE that fails to fill a field. The global
is simply never initialised, and the wasm boot asks for it at a point where the
native boot does not.

## What that means

`f_002da224` (the caller, `mc_028.cpp`) is reached in wasm at **t=0.92 s**, roughly
half a second after startup. The native run at that same moment is still in
`sys_memory_allocate` — it has not yet reached the code that would read this global.

So the two runs diverge before the failing read: the wasm boot arrives at
`f_002da224` through a path the native boot has not taken yet at that point. The
null is therefore a **symptom of an earlier divergence**, not an independent missing
initialisation. Chasing the store was the wrong end of the chain.

## The honest position

I claimed last round that "a global function-table pointer at `0x00D6E800` is never
written — that is what the boot is missing, and it is one 32-bit store". **That was
wrong**: no such store exists anywhere, in either run. The measurement retracts the
claim.

What is established, by measurement:

1. `f_005d2b44` is called once, from `0x2da54c`, with `r3 = 0` (entry probe).
2. `r3 = ld32([0x00D6E800])`, and that global is zero.
3. Nothing writes `0x00D6E800` — watched in wasm, watched natively, zero hits.
4. The native run does not reach that code at the equivalent moment.

What is therefore indicated: the divergence is **earlier** than `f_002da224`, in the
boot sequence between "images loaded" and "rendering init". The next useful probe is
not on a memory address but on the boot order: log every import call in both runs
for the first second and diff them. That is a mechanical comparison and it is what
the next round should do.
