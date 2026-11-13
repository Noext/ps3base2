# RESULTS-Q4 -- the RoundGuard rounding A/B on Minecraft and GTA

Agent: Q4. Branch: explore/q4. Worktree: /root/Dev/wt-Q4. Started 2026-11-13T12:25Z.

## Task

A/B the RoundGuard rounding fix in `runtime/src/spu/ops_impl.h` on Minecraft and
GTA-SA:

    BEFORE (main HEAD):  RoundGuard() { _mm_setcsr((old & ~0x6000u) & ~0x8040u); }   // nearest
    AFTER  (the fix):    RoundGuard() { _mm_setcsr((old |  0x6000u) & ~0x8040u); }   // toward zero

Measure: flips, crashes/exit code, any visual or behavioural difference. Every
comparison is an A/B on the same machine within the same minute, one game at a
time.

## Status

Claimed. Setting up builds. No measurement yet.

## Measurements

(none yet)
