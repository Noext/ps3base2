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
## Measurement 2 -- GTA pair 1

    === gta  A=nearest(pre-fix) B=truncate(fix)  1 pair(s) x 120s  12:42:34
    -- pair 1  12:42:34  load=14.50
      gta-A1                 exit=124  flips=6361   crashes=0   spu_threads=0
      gta-B1                 exit=124  flips=5641   crashes=0   spu_threads=0

Both alive at the 120 s budget (exit 124 = timeout, the title still running),
zero crashes, and **zero SPU threads started** on both sides -- which is the
prediction from Measurement 1 confirmed by the run itself: GTA never reaches the
SPU interpreter, so the rounding mode cannot affect it.

The 6361 vs 5641 gap is 11.3%, inside the 17% spread this machine shows between
two runs of the SAME code (docs/regress-baseline.md). More pairs are needed
before it can be called anything.
## Incident -- another agent deleted out-gta mid-A/B (12:48)

GTA pairs 2-3 came back `exit=1, flips=0` with:

    E   0.241849 loader: cannot read ./out-gta/gen/reloc/liblv2.elf

`/root/Dev/ps3base2/out-gta/ppu` and `out-gta/gen` were both empty, and free disk
jumped 98% -> 78% in the same minute. Another agent wiped the derived GTA tree
to reclaim space. This is not a rounding effect and those two pairs are void --
they never loaded the title.

Recovered from `/root/Dev/reg3/out-gta` (a complete 23 MB copy: 85 ppu TUs and
all 8 reloc ELFs). Restored, and GTA runs again -- smoke test at 25 s reaches
flip 901 with no load error. Pair 1 above was taken while the tree was intact
and stands; the pairs below are taken after the restore.

Note for the merge: `out-gta` is derived and gitignored, so this is a shared-
machine hazard, not a branch defect. Nothing in this branch touches it.
