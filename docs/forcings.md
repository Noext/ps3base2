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

## Experiment: forcing the chunk-update flag (forcing #2, NOT a solution)

Rule: `e98046c00:1:5000` -- call sys_event_flag_set(0x98046c00, 0x1) once, 5 s
after the first UserAutoSave completes.

Result, measured:

```
349.721  force: save completed; flag rule armed
354.721  force: APPLIED flag 0x98046c00 bits=0x1 -> 0x0 (5000 ms after save)
354.721  flagtrace: wait-ok (4J) Chunk update lr=0x0014f974 bits=0x3
354.721  flagtrace: set  from=(4J) Chunk update lr=0x0014f994 bits=0x1 0x0 -> 0x1
354.726  flagtrace: set  from=(4J) Chunk update lr=0x0014fbe4 bits=0x2 0x1 -> 0x3
```

**(4J) Chunk update wakes up.** The path that was unreachable before now runs:
both the index-0 set at 0x14f994 and the index-1 set at 0x14fbe4 execute. The
thread finishes one round of work and **blocks again** on the same flag.

**The screen does not close** and the world is not rendered.

### Correction: there is no MSSTimer producer

An earlier reading of the same log attributed the first `set bits=0x1` to
`MSSTimer`. That is wrong. `force::tick()` runs from inside `call_indirect`,
so it fires on whatever thread happens to call an import at that instant, and
`(4J) Chunk update` was already waiting, which is why the guest thread named in
that line is `MSSTimer`. The line 180 us later is the force rule reporting
itself.

**Measured**: in runs without the flag rule (`world3.log`, `world4.log`) the
flag is set exactly **once** in the whole run, at startup, and never again. There
is no periodic producer to fix. The flag is simply never set by the guest after
the save.

So forcing it once buys exactly one cycle of work, and the screen stays. The
remaining problem is not a missing periodic setter: it is that the title expects
the chunk thread to have completed its work and never re-arm it. That needs the
call chain above the flag, not another forcing.
