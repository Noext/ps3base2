# The null this, located exactly

## The measurement

With an entry probe on the failing function (`hooks::add_guard` + a compile-time
switch, because `getenv` is inert in wasm):

    ps3base-wasm: entry probe on f_005d2b44 (flags=1, impl=1)
    [entryprobe] #0 f_005d2b44 r3=0x0 r4=0xff lr=0x2da54c sp=0x6001f670
    F ppu: unknown indirect target 0x00000000 (lr=0x005d2c08) r9=0x0 r31=0x0 ctr=0x0

**`f_005d2b44` is called exactly once, from `lr=0x2da54c`, with r3=0.** One call,
one null. The caller is `f_002da224` in `out/ppu/mc_028.cpp:19513`:

    L_2da534:
        ctx.gpr[29] = 0 + 14090240;                      /* 0x00D6C000 */
        ctx.gpr[30] = 0 + 12451840;                      /* 0x00BE0000 */
        ctx.gpr[4]  = 0 + 255;
        ctx.gpr[3]  = ld32(base, ctx.gpr[29] + 10240);   /* [0x00D6E800] -> the null this */
        ctx.gpr[5]  = ld32(base, ctx.gpr[30] + 1808);
        ctx.lr = 0x2da54c; f_005d2b44(ctx, base);

So the chain is fully determined:

    [0x00D6E800] = 0   ->   r3 = 0   ->   r31 = 0   ->   r9 = [r31+12] = 0
                        ->   ctr = [r9+0] = 0      ->   call_indirect(0)

**A global function-table pointer at `0x00D6E800` is never written.** That is what
the boot is missing, and it is one 32-bit store, not a subsystem.

## What this rules out

- Not a missing import: `cellGcmSys` and `cellSpurs` handlers now run and log
  correctly, and the failure is downstream of them.
- Not a mapping fault: the probe reports `r31=0`, and read-only reads of `[0+12]`
  would fault, not return zero, if the object were merely unmapped.
- Not a lifting bug: the body is `r31 = r3` then `ld32([r31+12])`, which is the
  correct translation of the original PPU sequence.
- Not the four static call sites listed earlier (`mc_019:1912`, `mc_020:5203`,
  `mc_028:19273`, `mc_028:19320`) — the real one is `mc_028:19513`, which the
  entry probe found and the static grep did not.

## Two real bugs fixed on the way

1. **`hooks::add_guard` did not set `ppu::hook_impl`.** A guard registered after
   `hooks::init()` set `hook_flags` but left `hook_impl` null, and the lifted code
   only consults the flags when `hook_impl` is set. The guard was registered and
   never called — the same trap `hooks.cpp` already documents for
   `PS3BASE_LIST_GUARD`. `add_guard` is now self-sufficient.
2. **`runtime/src/spu/edge/spu_float.h` did not compile with clang at all.**
   `__vectorcall` on line 67 is an MSVC attribute; clang stops at that token and
   the whole header fails, taking `spu_conformance` and every `edge_*` source with
   it. Replaced with `static inline`. This was pre-existing, confirmed by stashing
   the round's changes and rebuilding.

## The next step, precisely

Find what should write `0x00D6E800`. It is a global function-table pointer that
the title populates during rendering init; on the native run the same address is
written, so the candidate is an import whose HLE does not fill it, or an init path
that the wasm boot skips. The probe to add is a write watchpoint on
`0x00D6E800` — `add_guard` cannot express that, but the codewatch machinery
already can.
