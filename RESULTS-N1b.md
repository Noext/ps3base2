# RESULTS N1b -- the SPU branch that selects the zero effective address

Agent N1b, branch `explore/N1b` from main 202e9ef. Raw proof, appended per
measurement. The answer includes two corrections to the brief, both measured.

## 0. Ground truth: which disassembly is real

The shared block capture `out-rdr/gen/blocks/run_capture.bin` **cannot** be used
for this window: reconstructing LS 0x63e0-0x6960 from it yields 634485
conflicting words over 696 addresses (overlapping captures of the same LSA
disagree). The ground truth is the 262144-byte local store the runtime itself
wrote at the fault, `spu_fault_00000200_06424.ls` -- thread 0x200, the exact
thread and pc of the refusal. Its 0x36200 block is byte-identical to the block
printed by the refusal dump.

## 1. One RDR run reproducing the refusal

    cd /root/Dev/wt-N1b
    DISPLAY=:924 PS3BASE_DISC_DIR=/root/Dev/rdr-port/vfs/dev_bdvd \
    PS3BASE_LOG=info PS3BASE_SPU_REFUSAL_DUMP=1 \
    PS3BASE_BLOCKWATCH=0177ca00-0177ce00 \
    timeout 80 /root/Dev/ps3base2/build-rdr/ps3base --no-launcher > /tmp/N1b-run.log

    E  26.668815 spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET REFUSED
                     cmd 0x40 (unmapped) ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428
    E    -1: ch19 <- 0x00000080 at pc 0x06410
    E    -2: ch18 <- 0x00000000 at pc 0x06408      <- EA = 0
    E    -3: ch16 <- 0x0001a100 at pc 0x06400
    E    -5: ch18 <- 0x70c9d400 at pc 0x068e8      <- a VALID RSX-local EA
    E   -1: pc 0x06900 cmp 0x00000000 vs 0x00000000 -> TAKEN
    E   block: ls 0x36200, 0x90 byte(s)
    E    +0x040: 0177cd00 ffffffff 00000000 00000004
    E    +0x050: 00000008 00000000 12190000 00000008
    E    +0x060: 00000000 00000000 70ddd000 70dec000

pc rule: mfc_hist pcs are +4 of the instruction (ch16 "at 0x06400" is the
`wrch` at 0x063fc); branch_ring pcs are exact.

## 2. The failing transfer is a leaf helper entered with EA = 0

    063e8: 04000187  ori r7,r3,0        r7 = r3 = MFC_LSA
    063f0: 40804006  il r6,0x80         size 0x80
    063f8: 40802005  il r5,0x40         cmd 0x40 (GET)
    063fc: 21a00807  wrch ch16,r7       MFC_LSA
    06404: 21a00904  wrch ch18,r4       MFC_EAL  <- THE ZERO (r4 on entry)
    06408: 40800004  il r4,0x0
    0640c: 21a00986  wrch ch19,r6       MFC_Size
    0641c: 21a00a04  wrch ch20,r4       MFC_TagID
    06424: 21a00a85  wrch ch21,r5       MFC_Cmd 0x40   <- refused

Exactly one caller in a scan of every rel16 branch in the image:

    06900: 20004468  brz r104,0x06b20
    06b30: 337f1700  brsl r0,0x063e8
    06b34: 327fba00  br r0,0x06904

## 3. The branch, and its deciding input

    06874: 33df31b1  lqr r49,0x36200
    068a8: 21a0092e  wrch ch18,r46      EA = block+0x0c + 0x490*n   VALID
    068b8: 21a0082c  wrch ch16,r44      LSA 0x3b100
    068c0: 33df2a27  lqr r39,0x36210
    068e4: 21a00923  wrch ch18,r35      EA = block+0x18 + 0x930*n   VALID
    068e8: 21a009a2  wrch ch19,r34      size 0x930
    06900: 20004468  brz r104,0x06b20   <<<<<< THE BRANCH
    06b20: 42d08003  ila r3,0x1a100     LSA = 0x1a100
    06b24: 33dee7b5  lqr r53,0x36260    quad based at block+0x60
    06b28: 408000e8  il r104,0x1
    06b2c: 3f811a84  rotqbyi r4,r53,4
    06b30: 337f1700  brsl r0,0x063e8    -> GET 0x80 bytes from that word
    06b34: 327fba00  br r0,0x06904

**The branch is `brz r104,0x06b20` at SPU LS 0x6900. Deciding input r104 = 0.**
r104 is kernel-local: of all 31 writers of r104 in the 256 KB image, exactly two
are in this routine --

    067a4: 32800068  fsmbi r104,0x0     <- 0 on entry to the descriptor run
    06b28: 408000e8  il r104,0x1       <- 1 only after the 0x6b20 path runs

-- so the console takes this branch too on its first element. **The branch is not
the divergence**; it is the kernel's own first-pass action.

**Correction 1 to the brief.** "ch18 <- 0x70c95400 at pc 0x068e8" is the `wrch`
at 0x068e4, which is sequential with 0x6848..0x6900, not an alternative to
0x6404; and 0x63e8's only caller is 0x6b30. There is no "valid-address path
through 0x6408". The valid EAs are emitted at 0x68a8/0x68e4.

## 4. rotqbyi: the refused EA is payload+0x64, not payload+0x60

Which word `rotqbyi rt,ra,i7` puts in the preferred slot was settled in ONE
snapshot against the mfc_hist values of the same run:

| emitter | lqr quad | imm | predicted LS word | observed ch18 |
|---|---|---|---|---|
| 0x06740 | 0x36200 | 4  | block+0x04 = 0x017bfe80 | 0x017bfe80 |
| 0x0689c | 0x36200 | 12 | block+0x0c = 0x70c81e00 | 0x70c81e00 |
| 0x068cc | 0x36210 | 8  | block+0x18 = 0x70c9d400 | 0x70c9d400 |

Three of three match: `pref(rotqbyi(x,i7))` is the big-endian word at
**LS (quad base + i7)**, agreeing with refs/rpcs3 SPUInterpreter.cpp:972-978
(load at `buf + (16 - i7)` over the quadword repeated twice).

Therefore `0x6b24 lqr r53,0x36260` + `0x6b2c rotqbyi r4,r53,4` select
**block+0x64**. **Correction 2 to the brief: the refused EA is payload+0x64, not
+0x60.** Both are 0 in the failing run, which is why every earlier report of "the
zero at +0x60" reproduced. The sibling consumer `0x7f80 rotqbyi r89,r90,8`
reads block+0x68 = 0x70ddd000 -- a valid address that works.

## 5. Where payload+0x64 comes from -- and the causal A/B

LS block+0x64 is guest **0x0177cc64** (the block is DMA'd from 0x0177cc00, size
0x90, by the kernel dispatcher at 0x7698-0x76c0; thread.cpp:1182 keys on exactly
that EA and size).

Write census of the payload's 36 words over a whole run: **+0x48, +0x64 and +0x7c
are never written by any PPU code.** The only writer of +0x64 in the program,
from the real EBOOT.elf:

    0x009208f0  8122825c   lwz  r9,-32164(r2)   r9 = 0x0177cc00
    0x009208f4  801e0000   lwz  r0,0(r30)       r30 = 0x0177ce00 (TOC-32016)
    0x009208f8  38600000   li   r3,0
    0x009208fc  90090064   stw  r0,100(r9)      payload+0x64 = *(0x0177ce00)

inside f_009207d4 = **__gafMP3**, whose body is cellFsOpen/cellFsRead/cellFsClose
/seek (imports 206/204/202/213) around that store. Its entry guard:

    0x0092084c  4bff78c1  b 0x010d810c        -> cellFsOpen("/dev_flash/sys/external/flashMP3.pic")
    0x00920854  2f830000  cmpwi cr7,r3,0      rc of cellFsOpen
    0x00920858  409e00c4  bc 4,30 -> 0x0092091c   TAKEN WHEN rc != 0

The taken path prints and returns -1; the fall-through reaches the store at
0x9208fc.

### The A/B, same machine, same binary, same minute-scale pair

**Arm A** -- the default environment (no dev_flash; `/root/.config/rpcs3` does
not exist), GOWA_IMPORT_ARGS on:

    I  2.673963 import: sys_fs::cellFsOpen(0x117faa0, 0, 0xd004eb70, 0) lr 0x920850
    W  2.674047 fs: open '/dev_flash/sys/external/flashMP3.pic' -> ENOENT
    I  2.674063 import: sys_fs::cellFsOpen returned 0xffffffff80010006
    I  2.674077 tty: [0] Multistream: mp3 failed to load MP3 codec (are you using the correct flash?)
    ...
    E  23.276861 DMA GET REFUSED ea 0x00000000 ... pc 0x06428
    writes to 0x0177cc64: 0

**Arm B** -- identical, plus `GOWA_FLASH_DIR=/tmp/N1b-flash` holding a 64 KB
`sys/external/flashMP3.pic`:

    I  2.716417 import: sys_fs::cellFsOpen(0x117faa0, ...) lr 0x920850
    I  2.716759 fs: open '/dev_flash/sys/external/flashMP3.pic' flags 00 -> 6
    I  2.716775 import: sys_fs::cellFsOpen returned 0x0
    I  2.717045 blockwatch: WRITE +0x064 (0x0177cc64) w4 prev 0x00000000 -> 0x70dfb700
                            by rsv::store_range | guest lr 0x009208f0
    ...
    the payload block then reads +0x060: 00000000 70dfb700 70dc9000 70dd8000

So **the zero at payload+0x64 is caused by the missing firmware file**, and the
title's own tty line names it verbatim. Note what arm B also shows: the store now
happens, +0x64 = 0x70dfb700 is a real address, and the refusal is still there --
it has merely moved to the second descriptor element, `pc 0x06510`
(ch18 <- 0 at 0x06500), which reads block+0x78 at 0x6510.. rather than +0x64.
So this is a necessary fix on the chain, not the whole of it.

## 6. Answer

- **Branch: `brz r104,0x06b20` at SPU LS 0x6900**, selecting the extra 0x80-byte
  GET described by the payload quadword based at block+0x60.
- **Deciding input: r104 = 0**, written only by `fsmbi r104,0x0` (0x67a4) and
  `il r104,0x1` (0x6b28) -- kernel-local, no memory, no channel. The console
  takes it too; it is not the divergence.
- **The value that decides the EA: LS 0x36264 = guest 0x0177cc64 = 0**, read by
  `lqr r53,0x36260` + `rotqbyi r4,r53,4`. The brief's "+0x60" is the quadword
  base; the selected word is **+0x64**.
- **Where that value comes from:** the PPU's own `stw r0,100(r9)` at guest
  **0x009208fc** in __gafMP3 (f_009207d4). It does not execute because
  cellFsOpen("/dev_flash/sys/external/flashMP3.pic") returns 0x80010006
  (ENOENT) -- that firmware file is absent from this environment -- and the guard
  `bc 4,30` at 0x00920858 takes the early-return path. Measured by A/B above.

## 7. Proposed fix

Not an SPU change, and not a kernel branch change. In order:

1. **Provide `/dev_flash/sys/external/flashMP3.pic`** (and the rest of a real
   dev_flash) to the run, or set `GOWA_FLASH_DIR` to a tree that has it. That is
   a run-environment fix: it makes __gafMP3 complete and payload+0x64 a real
   address -- measured in arm B. RDR's own tty already says so.
2. **Every run in tools/regress.sh should set GOWA_FLASH_DIR**, and a missing
   flash tree should be reported as an environment problem (exit 2) rather than
   surfacing 20 s later as an MFC refusal. This is the "no fake success" rule:
   the title asked for a system file, did not get it, and the failure appeared
   three modules away.
3. After that, the refusal at `pc 0x06510` remains and is the next link: it reads
   the quadword at block+0x70 (+0x78 = 0x0177cbc0 in the failing run), a different
   payload field. That is where to continue.

## 8. Next measurement

One run with a real dev_flash (or the flashMP3.pic stand-in) plus the refusal
dump, to name the field behind the second refusal (block+0x78) the same way, and
a GOWA_FLASH_DIR-aware regress.sh.

## 9. Merge-queue gate (rebase + tests)

Rebased onto origin/main and re-checked before declaring done:

    $ git fetch /root/Dev/ps3base2 main:refs/remotes/origin/main -f && git rebase origin/main
    Successfully rebased and updated refs/heads/explore/n1b.
    $ git rev-list --count HEAD..origin/main
    0
    $ git diff --diff-filter=D --name-only origin/main...HEAD | wc -l
    0

The branch changes no source file -- it is a measurement branch:

    $ git diff-tree -r --name-only origin/main HEAD
    .agent-status/N1b.json
    RESULTS-N1b.md
    current_tasks/N1b.claim

So the rebased tree is source-identical to origin/main (0674552), and the test
gate is that tree's own build:

    $ cd /root/Dev/ps3base2/build-rdr && ctest --output-on-failure
    40/40 Test #40: cellAudio ........................   Passed    0.01 sec
    100% tests passed, 0 tests failed out of 40
    Total Test time (real) =   9.53 sec

The refusal this report explains was re-measured on main 202e9ef (section 1) and
on the same binary at the rebased base in the A/B (section 5); N3b and N5b, which
landed in between, report the same refusal at the same pc.
