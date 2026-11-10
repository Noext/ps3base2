# The null is an uninitialised vtable, and getenv does not work in wasm

## The measurement

With a compile-time probe at the indirect-call failure:

    [nullprobe] r31=0x00000000 r9=0x00000000
    F ppu: unknown indirect target 0x00000000 (lr=0x005d2c08) ctr=0x0

Reading `out/ppu/mc_058.cpp:27672-27677`, the failing sequence is:

    ctx.gpr[9]  = ld32(base, ctx.gpr[31] + 12);   /* r9 = obj->field_12 */
    ctx.gpr[0]  = ld32(base, ctx.gpr[9] + 0);     /* ctr = *r9 */
    ctx.ctr     = ctx.gpr[0];
    ctx.gpr[2]  = ld32(base, ctx.gpr[9] + 4);
    ctx.lr      = 0x5d2c08; call_indirect(ctx, base, ctx.ctr);

And `r31` is set once, at the top of the body:

    ctx.gpr[31] = ctx.gpr[3] | ctx.gpr[3];        /* r31 = first argument */

**So the first argument passed to `f_005d2b44` is zero, and `r9` is zero because
`[r31+12]` was never readable.** That is a guest object that was never constructed —
a null `this`, not a missing import and not a mapping problem.

I checked the four call sites of `f_005d2b44` (`mc_019:1912`, `mc_020:5203`,
`mc_028:19273`, `mc_028:19320`); none of them has `lr=0x5d2c08`, so the caller is
another entry into the same function through a different path, and the value it
passes is the one that is zero.

## The bigger finding: `getenv` is inert in the wasm build

The probe did not run at first, and the reason matters for everything that
follows:

    PS3BASE_NULL_PROBE=1 node --experimental-wasm-exnref ps3.js
    env: (null)

**Emscripten returns null from `getenv` unless `-sENV` is passed**, and `-sENV`
breaks this build's link. So **every `PS3BASE_*` environment variable is inert in
the wasm binary**: `PS3BASE_MEMORY`, `PS3BASE_REGIONS`, `PS3BASE_SYSCALL_LOG`,
`PS3BASE_CALL_TRACE`, `GOWA_*`, all of them.

The two that matter were already handled by making the wasm behaviour unconditional
(`mem::` returns Bitset, `regions` forces compact). The rest are diagnostics that
silently do nothing.

This also explains why `PS3BASE_CALL_TRACE=main_thread` produced no `calltree`
lines: `calltrace::enabled_for_current()` reads `getenv`, gets null, and returns
false. The trace facility is correct; its switch cannot be reached.

**Consequence:** diagnostics in the wasm build must be compile-time constants or
must arrive through an explicit API the page calls, not through the environment.
The null probe was changed to a `-D` constant for exactly this reason.

## What this round established

1. `hle::Reg`/`hle::Fn` dispatch works in the wasm build: `hooks::init()` is now
   called by the wasm entry, which was missing and would have made every hook inert
   even if `getenv` had worked.
2. `getenv` is dead in wasm — measured with a five-line test program, not inferred.
3. The failing argument is a null `this` at `f_005d2b44`, measured through the
   probe rather than reasoned about.

## What is not established

Which of the guest's constructors failed to run. The next step is a probe at
`f_005d2b44`'s entry printing `r3` and the caller's `lr`, so the call chain is read
from the machine rather than from the four static call sites that do not match.
