# Forced values (debts)

Each entry makes the title proceed past a place where our emulation differs from
the PS3. They are debts: every one must be replaced by the real fix.

Mechanism: `PS3BASE_FORCE="f<hexaddr>=<value>"` overrides r3 on return from the
lifted function at that address. `w<hexaddr>:<n>=<value>` writes n bytes at a
guest address. `lr<hexaddr>=<value>` matches the caller. Each rule logs once,
with the original and forced values.

| # | site | original | forced | unlocked |
|---|---|---|---|---|
| 1 | `f_00216258` (`mc_020.cpp:21243`, `ld8[obj+593]`) | `0` | `1` | The server loop `f_00188428` no longer exits at 66 ms. The title reaches **"Initializing server / Generating spawn area"** and the progress bar advances (measured 14060 → 17898 green pixels, then a second phase 2526 → 5604) instead of freezing on "Finalizing..." |

## Why forcing is legitimate here

The value read at `obj+593` is set by `f_00216560`, which leaves it at 1 unless
`f_00216260` returns 0, which happens when `sceNpMatching2Init2` (import 308)
is taken as succeeded. We do not implement that import, so it returns 0 through
the generic unimplemented path. The forcing substitutes for the missing NP
initialisation; the real fix is to implement `sceNp2::sceNpMatching2Init2`,
`sceNp2::sceNpMatching2CreateContext` and `sceNp::sceNpManagerGetNpId` with the
RPCS3 return values.

## What forcing #1 unlocked, measured

A 20-minute run with forcing #1 only. The server kept running and the spawn-area
bar advanced steadily: 11951 -> 14268 -> 15218 -> 17156 -> 18867 green pixels at
30 s intervals. At t=519 s the run **crashed** (SIGSEGV) in `(4J) Server`:

```
crash: signal 11 ... fault 0xbcf8d80f thread (4J) Server 0x1000016
  #0 ppu::ld32            (ops.h:43)
  #3 f_0064c044_body      (mc_063.cpp:23079)
  #4 ppu::call_indirect
  #5 f_004e2ac0_body      (mc_049.cpp:10084)
```

`f_0064c044` walks a linked list and does `ld32[ node + 28 ]`; the node pointer
was `0xbcf8d80f`, a corrupted value. Just before the crash, a new worker thread
`(4J) Post processing` was created and exited cleanly, so generation had
progressed past spawn area into post-processing.

**Time to reach this point: ~519 s of emulated time** for spawn-area generation.
Speed of the SPU work is a separate concern, to be addressed later.
