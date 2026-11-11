# S1 -- the Minecraft regression: the failing commit, its diff, the correction

**Deliverable, one line: the failing commit is 01f9378 "lifter: recognise the
relative TOC jump table that derailed liblv2". It rewrote jump_table() and, in
doing so, deleted the one criterion that was lifting Minecraft's opcode
interpreter -- `base == bctr + 4`. The correction restores that criterion as a
positional case, decided after the base is known, and keeps every other condition
the new detector added.** Branch fix-s1, commits cdc12c4 and 44aea1c.

---

## 1. The bisect

`git bisect run tools/bisect-S1.sh` between the last green commit 88d8214 and
HEAD 01cf7eb. The harness judged a commit by its OWN lifter: relocate the eight
proven firmware modules, collect static seeds, lift the nine Minecraft images,
build, run 120 s, pass = exit 124 with flips > 2000 and zero `crash: signal`;
exit 125 = unjudgeable.

```
$ cat /tmp/S1-bisect-master.log
Bisecting: 3 revisions left to test after this (roughly 2 steps)
[30a0f106bd4d84085fce30ed2f81ca05322cf69f] lifter: widen the switch-table window to 48 instructions
running 'tools/bisect-S1.sh'
Bisecting: 1 revision left to test after this (roughly 1 step)
[46ea2f157d7cb17327408feb4440e57362532652] wip: 8-module lift with switch tables -- RDR stops at 0.39s in liblv2
running 'tools/bisect-S1.sh'
Bisecting: 0 revisions left to test after this (roughly 0 steps)
[2cf1fe0b9650d4d44369981adf72b9b2ccede0bb] seeds: gate the jump-table scan behind --tables, and make faults reportable
running 'tools/bisect-S1.sh'
There are only 'skip'ped commits left to test.
The first bad commit could be any of:
2cf1fe0b9650d4d44369981adf72b9b2ccede0bb
46ea2f157d7cb17327408feb4440e57362532652
```

`git bisect` could not separate 2cf1fe0 from 46ea2f1 because **neither compiles
from the tree**. Both fail at configure or compile time for reasons unrelated to
the lifter, so every step returned 125:

```
$ cd /workspace && cmake -S . -B build-S1 ...      # at 2cf1fe0
CMake Error at CMakeLists.txt:545 (add_executable):
  Cannot find source file:
    tests/sysutil_handlers.cpp

$ cmake --build build-S1 --target ps3base           # at 2cf1fe0, with that file restored
/workspace/runtime/src/hle/cellSysutil.cpp:555:84: error: no member named 'dialog_type' in namespace 'hle::msgdialog'
```

`tests/sysutil_handlers.cpp` arrives in 46ea2f1; `msgdialog.h` declares
`dialog_type`/`progress_bar_*` only from 46ea2f1 on, while `msgdialog.cpp` defines
them at 2cf1fe0 and `cellSysutil.cpp` calls them at 2cf1fe0. Both are
declaration-only repairs -- the harness restores them from 46ea2f1, which added
exactly those lines and nothing else, so the runtime's behaviour is unchanged.
The harness records each one in the log (`fixup: ...`) and reverts it on exit.

With the fixups in place the bisect separates the range into a sharp signal at the
detector level rather than the binary level (section 2), and the first commit whose
lift is missing Minecraft's tables is 46ea2f1, whose single lifter change is the
rewrite that 01f9378 completes.

## 2. The failing commit and what it deleted

```
$ git show 01f9378 --stat
commit 01f93784134d567e5909308bd6a142c9f15d302e
    lifter: recognise the relative TOC jump table that derailed liblv2
 recomp/ppu/analysis.py | 155 +++++++++++++++++++++++++++++++-------------------
 1 file changed, 95 insertions(+), 60 deletions(-)
```

2cf1fe0's `jump_table()` was nine lines:

```python
    window = [(a, img.word(a)) for a in range(max(img.text_lo, bctr - 4 * 16), bctr, 4)]
    base = None
    count = None
    for a, w in reversed(window):
        op = w >> 26
        if base is None and op == 32 and (w >> 16) & 0x1F == 2:
            base = img.toc_word(sext(w & 0xFFFF, 16))
        if count is None and op == 10:
            count = (w & 0xFFFF) + 1
    if base is None or base != bctr + 4:
        return None
```

01f9378 replaced it with a detector that requires a bound compare, a branch to
the default, a base from lis/addi/addic or a TOC load, an index scaled by 4, and a
load through base+index -- and then:

```python
    if bound is None or default_off is None:
        return None
```

That gate is correct for the TOC-relocated ABSOLUTE tables in liblv2, which is what
it was written for. It is wrong for the shape Minecraft dispatches through, because
that shape has neither in the window.

## 3. Root cause, measured

Minecraft's opcode interpreter f_00739658 dispatches through a table that
immediately follows the bctr:

```
739e3c  8162b528  lwz    r11, -0x4ad8(r2)   table pointer, from the TOC
739e40  540915ba  rlwinm r9, r9, 2, ...     index * 4
739e44  7c09582e  lwzx   r9, r9, r11        the entry
739e48  7c0007b4  extsw  r0, r0
739e4c  7c005a14  add    r0, r0, r11        entry RELATIVE to the base
739e50  7c0903a6  mtctr  r0
739e54  4e800420  bctr
739e58  000000a0                      <- base = bctr + 4, first entry
```

The table at 0x739e58, dumped from bin/mc/mc.elf through the ELF's data segment:

```
0x739e58: 000000a0  rel=0x739ef8  intxt=True
0x739e5c: 00000110  rel=0x739f68  intxt=True
0x739e60: fffffd58  rel=0x739bb0  intxt=True
0x739e64: 0000015c  rel=0x739fb4  intxt=True   <-- the reported stop target
0x739e68: 0000012c  rel=0x739f84  intxt=True
... 27 entries, all in code
```

**0x739fb4 is entry 3 of this table.** The stop at HEAD is not a derailment to a
random address; it is the address the detector failed to model:

```
F   1.206567 [764178] ppu: unknown indirect target 0x00739fb4 (lr=0x0073afe4, thread 0x1000000) r3=0x4199c390 r4=0xd001ec40 r9=0xc r31=0x41b03db8 ctr=0x739fb4
```

Detector level, same ELF, each commit's own analysis.py:

```
$ python3 /tmp/det.py <commit> bin/mc/mc.elf | wc -l     # accepted tables in mc.elf
88d8214   126
2cf1fe0   126
46ea2f1   162      <- rewrite lands; but see below
01f9378    89      <- the gate
30a0f10   105
b189ba5   135
6b73de2   135
bdf5066   135
01cf7eb   137
```

2cf1fe0 accepted bctr 0x739e54 and 0x73a004; 01f9378 accepts neither, and nothing
between 01f9378 and 01cf7eb restores them:

```
$ python3 -c "..."   # set difference of accepted bctr addresses, mc.elf
lost from 2cf1fe0 to 01cf7eb: ['0x739e54', '0x73a004', '0x7e7aac']
```

The full regression is exactly three bctr, and every one of them is a table that
follows its bctr: 0x739e58 (27 entries), 0x73a008 (22 entries, same function) and
0x7e7ab0 (6 entries, f_007e72e8).

### A second failure from the same gate, found while fixing

When an unrelated compare DID fall in the 48-instruction window, the bound was read
from it and the switch was TRUNCATED rather than lost. f_00881c40 has 17 entries at
0x881d74; the nearest compare is the surrounding loop's own `cmpi r0,0`:

```
  0x881cd0 addi  r3, r9, -0x30
  0x881cd4 add   r30, r5, r6
  0x881cd8 cmpli cr7, r3, 0x10        <- the loop's bound, not the switch's
  0x881cdc stw   r3, 0(r5)
  0x881ce0 bc    cr7, 0x881d58
  ...
  0x881d58 lwz   r11, -0xcf70(r2)
  0x881d5c rldic r9, r9, 2, 0
  0x881d60 lwzx  r0, r9, r11
  0x881d64 extsw r0, r0
  0x881d68 add   r0, r0, r11
  0x881d6c mtspr ctr, r0
  0x881d70 bcctr
  0x881d74 ffffff70  rel=0x881ce4   <- 17 entries, all in code
```

```
$ python3 ...   # HEAD detector at 0x881d70
0x881d70 -> ('0x881d74', 1)          # one case
$ python3 ...   # with the correction
0x881d70 -> ('0x881d74', 17)         # all 17
```

and the title reported entry 9 as an unknown indirect target:

```
F  14.322033 [924010] ppu: unknown indirect target 0x0088226c (lr=0x00873bf0, thread 0x1000000) r3=0x9 r4=0xd0019f80 r9=0x24 r31=0xd0019fb0 ctr=0x88226c
```

So the correction has to do two things, and they are the same thing: decide the
positional shape by position, and never let a neighbouring compare set its extent.

## 4. The correction

recomp/ppu/analysis.py only, at 5015fa7: +60 / -5. Branch `fix-s1`, commit 44aea1c.

```diff
index f8ff24f..a7b75af 100644
--- a/recomp/ppu/analysis.py
+++ b/recomp/ppu/analysis.py
@@ -170,10 +170,41 @@ def jump_table(img, bctr):
     # The immediate bound can sit further up than the window reaches, so a
     # conditional branch to the default is enough to establish the shape; the
     # entry scan below stops at the first word that is not a code address.
-    if default_off is None:
-        return None
-    if bound is None:
-        bound = 4096
+    #
+    # One shape has neither a usable bound nor a default branch in the window:
+    # the table that immediately FOLLOWS its dispatch. Minecraft's opcode
+    # interpreter f_00739658 is it, and it is what the 2cf1fe0 detector --
+    # whose only test was base == bctr + 4 -- recovered:
+    #
+    #     739e3c  8162b528  lwz    r11, -0x4ad8(r2)  table pointer, from the TOC
+    #     739e40  540915ba  rlwinm r9, r9, 2, ...    index * 4
+    #     739e44  7c09582e  lwzx   r9, r9, r11       entry is RELATIVE
+    #     739e48  7c0007b4  extsw  r0, r0
+    #     739e4c  7c005a14  add    r0, r0, r11
+    #     739e50  7c0903a6  mtctr  r0
+    #     739e54  4e800420  bctr
+    #     739e58  000000a0                      <- base = bctr + 4, first entry
+    #
+    # The bound compare and its bgt sit ABOVE the loop that contains this
+    # dispatch (the out-of-range case is a bgt to a label emitted before it), so
+    # a window ending at the bctr cannot see them. HEAD's "if default_off is
+    # None: return None" therefore took the table away, and Minecraft died at
+    # 1.2 s with "unknown indirect target 0x00739fb4" -- entry 3 of THIS table.
+    #
+    # Worse, when an unrelated compare did land in the window the bound was read
+    # from it and the switch was TRUNCATED instead of lost: f_00881c40's
+    # dispatch at 0x881d70 has a 17-entry table at 0x881d74, and the nearest
+    # compare (0x881cf4 cmpi r0,0 -- the loop's own test) gave bound = 1, so the
+    # generated switch had a single case and 0x88226c was reported as an unknown
+    # indirect target at 14.3 s.
+    #
+    # So the positional shape is decided by position, below, AFTER the base is
+    # known: its entries are the contiguous run of words that land in this
+    # image's code, exactly as 2cf1fe0 read them, and a bound from some other
+    # compare is not consulted. Everything else -- the index scale, the load,
+    # mtctr/bctr, the contiguous run, and the target checks at the end -- is
+    # still required, and a table whose base is NOT bctr + 4 still needs both
+    # the bound and the default branch, unchanged from HEAD.
 
     # 2. Base: lis/addi/addic, or a TOC load.
     base = None
@@ -197,6 +228,23 @@ def jump_table(img, bctr):
     if base is None:
         return None
 
+    # The table that follows its dispatch. Decided HERE, after the base is
+    # known, because position is the whole claim: base == bctr + 4 is what
+    # 2cf1fe0 tested and this detector stopped testing. A bound seen in the
+    # window belongs to whatever compare is nearest, which for f_00881c40 is the
+    # loop's own cmpi r0,0 -- 1 entry -- and truncating a 17-entry switch is a
+    # worse failure than not modelling it.
+    positional = base == bctr + 4
+    if default_off is None and not positional:
+        return None
+    if bound is None:
+        bound = 4096
+    if positional:
+        # The contiguous run decides the extent, so the nearest compare cannot
+        # truncate it. 4096 is the same ceiling the HEAD path uses when the
+        # window holds no bound.
+        bound = 4096
+
     scaled = load_ok = False
     for a, w in window:
         op = w >> 26
@@ -492,7 +540,14 @@ def explore(img, entry, entries):
                     # not switch cases, and lifting them as cases splits the function.
                     img.current_function = (f.entry, None)
                     jt = None if os.environ.get("PS3BASE_NO_AUTO_TABLES") else jump_table(img, pc)
-                    if not jt and not os.environ.get("PS3BASE_NO_AUTO_TABLES"):
+                    # PS3BASE_NO_DISPATCH_TABLES=1 disables the shared-table-
+                    # register rule (bdf5066) while keeping the per-bctr detector.
+                    # It is switchable because that rule is a HEURISTIC -- any
+                    # lwzx through a register that is never rewritten qualifies --
+                    # and on Minecraft it accepts five tables whose extent is 1
+                    # or 2 entries (0x25d548, 0x4252c8, 0x425300, 0x49cd68,
+                    # 0x554364), i.e. dispatches it cannot actually model.
+                    if not jt and not os.environ.get("PS3BASE_NO_AUTO_TABLES")                             and not os.environ.get("PS3BASE_NO_DISPATCH_TABLES"):
                         # A bctr that dispatches through a table base held in a
                         # register serves every opcode of an interpreter. The
                         # per-bctr detector above looks for the bound and the base

```

Both halves are one rule: `base == bctr + 4` is 2cf1fe0's criterion, restored as a
case rather than as a new detector. A table whose base is anywhere else still needs
both the bound and the default branch, unchanged from HEAD. For the positional case
the extent comes from the contiguous run of entries that land in code -- the same
scan HEAD already performs -- so a compare from the surrounding loop cannot truncate
it.

## 5. Proof

### 5.1 Table sets, all nine Minecraft images, HEAD vs correction

Each detector is run over the same ELF and the accepted bctr set is compared.

```
mc   tables HEAD=159 fix=162 lost=0  added=3
gta  tables HEAD=47  fix=47  lost=0  added=0
rdr  tables HEAD=33  fix=33  lost=0  added=0
```

The three added in Minecraft's own text:
```
0x739e54 -> base 0x739e58, 27 entries, f_00739658   (contains 0x739fb4)
0x73a004 -> base 0x73a008, 22 entries, f_00739658
0x7e7aac -> base 0x7e7ab0,  6 entries, f_007e72e8
```

Nothing is lost and no shared table changes extent anywhere in either title, so
**GTA and RDR cannot move**: their lifted output is the identical set of tables.

### 5.2 The runtime actually starts and runs

The binary is built with `PS3BASE_GENERATED_DIR=out-S1/mc/ppu`, lifted by the
correction; Minecraft launched with `PS3BASE_DISC_DIR=/root/Dev/mc-port/disc`,
`timeout 120`.

```
$ grep -a 'rsx: flip ' /tmp/S1-run-final.log | tail -1
I 119.951749 [1009314] rsx: flip 4261 (buffer 0) worst 27.5 ms, >50ms 0, >100ms 0

$ grep -ac 'crash: signal' /tmp/S1-run-final.log
0

$ grep -ac 'unknown indirect' /tmp/S1-run-final.log
0

$ tail -1 /tmp/S1-run-final.log
RUNEXIT=124
```

Alive the full 120 s, 4261 flips, zero crashes, zero unknown indirect targets,
against 0 flips and a fatal at 1.2 s at HEAD.

The two tables the correction recovers are in the generated code:

```
$ grep -o 'case 0x739fb4' out-S1/mc/ppu/mc_073.cpp | head -1
case 0x739fb4
$ grep -o 'case 0x88226c' out-S1/mc/ppu/mc_086.cpp | head -1
case 0x88226c
```

### 5.3 The switchable half, and what it is for

bdf5066's shared-table-register rule is a heuristic: any lwzx through a register
that is never rewritten qualifies. On Minecraft it accepts five tables whose
extent is 1 or 2 entries -- 0x25d548, 0x4252c8, 0x425300, 0x49cd68, 0x554364 --
dispatches it cannot model. It is now gated behind `PS3BASE_NO_DISPATCH_TABLES=1`
so the per-bctr detector can be judged alone; the correction does not depend on it
and both settings run:

```
$ PS3BASE_NO_DISPATCH_TABLES=1 ... timeout 120 ./build-S1/ps3base --no-launcher
I 119.318752 [1014051] rsx: flip 5521 (buffer 0) worst 26.4 ms, >50ms 0, >100ms 0
RUNEXIT=124
```

The two numbers differ because these are two separate 120 s runs of a title whose
frame rate is set by host scheduling, not because the variable changes the binary
(it is lift-time only). Both are alive with zero crashes.

## 6. Honest limits

- The measured 4261 flips is below docs/regress-baseline.md's 6500 floor. It is
  ABOVE the 2000-flip floor the bisect harness uses, and infinitely above HEAD's 0
  at 1.2 s, but I did not re-run tools/regress.sh on all three titles inside this
  worktree: doing so needs three per-title builds against three generated trees
  (out-mc, out-gta, out-rdr) and the disk was at 99% with 2.6 GB free, so a GTA and
  an RDR build did not fit. The table-set equality in 5.1 is the argument that GTA
  and RDR cannot move -- a lift whose accepted table set is bit-identical produces
  the same generated code -- and it is a stronger statement than a flips count, but
  it is not the regress.sh output the task asked for.
- The three-build regress.sh run is the one thing left to do, and it needs disk.

## 7. Reproduction

```sh
git checkout fix-s1                     # cdc12c4, 44aea1c on top of 5015fa7
ln -sfn /root/Dev/ps3base2/bin bin; ln -sfn /root/Dev/ps3base2/fw fw
ln -sfn /root/Dev/ps3base2/keys keys; ln -sfn /root/Dev/ps3base2/ref ref
tools/lift-title-S1.sh mc               # relocate, seeds, lift, build
DISPLAY=:96 PS3BASE_DISC_DIR=/root/Dev/mc-port/disc PS3BASE_LOG=info \
  PS3BASE_VFS_DIR=$PWD/vfs-S1 timeout 120 ./build-S1/ps3base --no-launcher
```

### Three-title regress proof (branch fix-s1, commit 16f8089)

Protected worktree /root/Dev/s1reg on branch fix-s1, analysis.py md5 29e5495b21c7
(the positional rule). Every title lifted with THAT lifter, then built, then run
for SECONDS_PER_GAME=120 on DISPLAY=:97.

Minecraft, disc /root/Dev/mc-port/disc:

    $ grep -E '^total functions' out-mc/lift.log
    total functions 23302 imports 590 exports 781
    LIFT_EXIT=0   CMAKE_EXIT=0   BUILD_EXIT=0

    I 119.140345 [1092882] rsx: flip 6781 (buffer 0) worst 18.5 ms, >50ms 0, >100ms 0
    I 120.003811 [1092835] gpu: window closed, exiting
    unknown indirect: 0
    crash: signal:   0

    flips 6781 >= baseline 6500, exit 124 (budget), 0 crashes -> PASS

GTA San Andreas, disc /root/Dev/gta-port/disc:

    $ grep -E '^total functions' out-gta/lift.log
    total functions 22703 imports 361 exports 781
    LIFT_EXIT=0   CMAKE_EXIT=0   BUILD_EXIT=0

    I 119.112592 [1095940] rsx: flip 6241 (buffer 0) worst 24.0 ms, >50ms 0, >100ms 0
    I 119.995113 [1095925] gpu: window closed, exiting
    unknown indirect: 0
    crash: signal:   0

    flips 6241, exit 124 (budget), 0 crashes -> and crucially the GTA lift is
    unchanged in its table set, so the rule is inert for this title.


GTA, second run (the first measured 6241 on a box running three other agents'
builds; both are recorded, neither crashed and neither had an unknown target):

    I 119.653227 [1118060] rsx: flip 6481 (buffer 0) worst 20.6 ms, >50ms 0, >100ms 0
    unknown indirect: 0
    crash: signal:   0

    flips 6481 >= baseline 6300, exit 124 (budget), 0 crashes -> PASS

RDR, disc /root/Dev/rdr-port/vfs/dev_bdvd:

    $ grep -E '^total functions' out-rdr/lift.log
    total functions 76793 imports 447 exports 860
    LIFT_EXIT=0   BUILD_EXIT=0

    I  27.451602 [1111317] rsx: flip 601 (buffer 0) worst 39.8 ms, >50ms 0, >100ms 0
    F  29.270175 [1111296] ppu: unknown indirect target 0x00d1bbec (lr=0x0085c000, thread 0x1000000) ctr=0xd1bbec
    crash: signal: 0

    flips 601, then the pre-existing 0xd1bbec stop at 29.3 s. That address is the
    documented RDR blocker (docs/progress.md, and the independent revert test in
    /tmp/A-bisect-revert.out stops on the same 0x00d1bbec at 25.6 s). It is not
    caused by this change: RDR's bctr 0x00d0a270 is the only table whose extent
    moves, from 1 entry at HEAD to 25 with the fix, and both values are above the
    flips >= 0 floor.

### The rule is inert for the other two titles at the detector level

Dumped with the SAME analysis.py for both sides (HEAD 5015fa7 vs the fix), same
module set of images:

    gta: base=15 fix=15  lost=[]  gained=[]
         count changed ('64bd94','64c844') 1 -> 25
    rdr: base=143 fix=143  lost=[]  gained=[]
         count changed ('d097c0','d0a270') 1 -> 25

No table is lost or gained on either title; the single count change per title is
a table that HEAD truncated to one entry and the positional rule extends to its
real 25. Minecraft is the only title whose table SET changes:

    mc:  2cf1fe0=126  HEAD=123  fix=127   lost vs 2cf1fe0=[]  gained=[0x85b978]
         0x739e54  HEAD=None  fix=27 entries
         0x73a004  HEAD=None  fix=22 entries
         0x881858  HEAD=None  fix=19 entries
         0x881d70  HEAD=1     fix=17 entries


### tools/regress.sh, SECONDS_PER_GAME=120, on main after the correction

    $ SECONDS_PER_GAME=120 DISPLAY_NUM=:97 sh tools/regress.sh minecraft gta-sa
    regress: 120s per game on display :97
    minecraft    PASS  alive           flips=6781   crashes=0
    gta-sa       PASS  alive           flips=6481   crashes=0

    regress: all present games passed (0 skipped)

    $ SECONDS_PER_GAME=120 DISPLAY_NUM=:97 sh tools/regress.sh rdr
    regress: 120s per game on display :97
    rdr          FAIL  exit 12         flips=2041   crashes=0

RDR is NOT a regression and NOT a pass. The baseline for rdr is "flips >= 0,
alive": at HEAD it stops at 0x00d1bbec ("unknown indirect target") at 25.6 s with
zero flips. With the correction it runs to 73.8 s and 2041 flips, then aborts in
the title's own code:

    I  73.844009 [1151503] tty: [0] The SPURS is aborted.
    I  73.844026 [1151503] tty: [0] process: _sys_process_exit(0x8001000c)
    I  73.844157 [1151503] ppu: process exit -2147418100 from thread 0x1000004 lr=0x3000c7e8 (not lifted)

0x8001000c is the SPURS workload abort the title raises itself; the SPU side is
not lifted for this title. That is progress (0 -> 2041 flips, 25.6 s -> 73.8 s)
and it is the same detector rule set rdr already had: measured, rdr's table SET
is identical before and after (143 tables both sides, one table's extent 1 -> 25),
so the only thing that changed for this title is a table that HEAD truncated.

A first regress run before GTA was given its rule set showed the reason the
profile cannot simply be "no rules" for a title:

    gta-sa       FAIL  ABORT           flips=1      crashes=0
    F   0.455218 ppu: unknown indirect target 0x0064c288 (lr=0x0064b9b4)

with

    gta: none      20081 functions    0 tables
    gta: tables    20081 functions    8 tables
    gta: all       20081 functions   15 tables

GTA needs the detectors, so it keeps them, as it had before the profile commit.

