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
## Measurement 1 -- the two arms, and a structural fact that decides the A/B

Built both arms from THIS tree so the only difference is the one line
`runtime/src/spu/ops_impl.h:110`. Only `interpreter.cpp` recompiles when that
line flips, which proves the fix is confined to one translation unit.

    A  nearest     RoundGuard() { _mm_setcsr((old & ~0x6000u) & ~0x8040u); }   (main HEAD, pre-fix)
    B  truncate    RoundGuard() { _mm_setcsr((old |  0x6000u) & ~0x8040u); }   (the fix)

Binaries, sha256:

    def9e157...  A-nearest-gta
    ad62e2ee...  A-nearest-mc
    78035cac...  B-trunc-gta
    79aca743...  B-trunc-mc

### GTA has no SPU code at all

This is the fact that decides what the A/B can mean for GTA. The RoundGuard is
reached only from `runtime/src/spu/ops_impl.h`, which is included only by
`runtime/src/spu/interpreter.cpp` -- the SPU interpreter. GTA's generated SPU
tree is EMPTY:

    $ cat out-gta/spu/spu_registry.cpp
    namespace spu::gen { }
    const CompiledBlock* compiled_blocks(size_t& count) { count = 0; return nullptr; }

    $ ls out-gta/blocks | wc -l
    0
    $ ls out-gta/gen/spu
    generated.txt        (0 bytes)

and the reference GTA log has zero `spu:` lines and zero `cellSpurs` calls:

    $ grep -c 'spu:' /tmp/regress-gta-sa-cand1.log
    0
    $ grep -c 'cellSpurs' /tmp/regress-gta-sa-cand1.log
    0

So GTA never starts an SPU thread, never interprets an SPU instruction, and
never constructs a RoundGuard. The two GTA binaries differ in the code that is
in them, but that code is unreachable in this title. The prediction is a null
result for GTA -- the A/B must confirm it rather than assume it.

Minecraft is the opposite: 170 generated SPU translation units, 24737 compiled
blocks, 5 SPURS kernel threads started, and 43 DFA / 54 DFS / 83 DFM / 40 DFMS /
12 FRDS / 24 FESD sites in the generated code, all of which go through the
RoundGuard.
