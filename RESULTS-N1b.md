# RESULTS N1b -- the SPU branch that selects the zero effective address

Agent N1b. Branch `explore/N1b` from main 202e9ef. Raw proof, appended per
measurement. Method first, because the first method failed.

## 0. Ground truth: which disassembly is real

The shared block capture `out-rdr/gen/blocks/run_capture.bin` **cannot** be used
for this window. Reconstructing LS 0x63e0-0x6960 from it produces 634485
conflicting words over 696 addresses (overlapping captures of the same LSA
disagree). Use instead the 262144-byte local store the runtime itself wrote at
the fault: `spu_fault_00000200_06424.ls` -- thread 0x200, the exact thread and
pc of the refusal. It is a full 256 KB LS image and its 0x36200 block is
byte-identical to the block printed by the refusal dump.

## 1. One RDR run reproducing the refusal (this branch, main 202e9ef)

    cd /root/Dev/wt-N1b
    DISPLAY=:924 PS3BASE_DISC_DIR=/root/Dev/rdr-port/vfs/dev_bdvd \
    PS3BASE_LOG=info PS3BASE_SPU_REFUSAL_DUMP=1 \
    PS3BASE_BLOCKWATCH=0177ca00-0177ce00 \
    timeout 80 /root/Dev/ps3base2/build-rdr/ps3base --no-launcher > /tmp/N1b-run.log

    E  26.668815 spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET REFUSED
                     cmd 0x40 (unmapped) ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428
    E  26.668848 spu:   mfc_hist: 64 write(s), most recent first
    E    -1: ch19 <- 0x00000080 at pc 0x06410
    E    -2: ch18 <- 0x00000000 at pc 0x06408      <- EA = 0
    E    -3: ch16 <- 0x0001a100 at pc 0x06400
    E    -5: ch18 <- 0x70c9d400 at pc 0x068e8      <- a VALID RSX-local EA
    E  26.669223 spu:   branch_ring: 64 taken record(s), most recent first
    E    -1: pc 0x06900 cmp 0x00000000 vs 0x00000000 -> TAKEN
    E    -7: pc 0x0666c cmp 0x017bfe80 vs 0x00000000 -> TAKEN
    E  26.669591 spu:   block: ls 0x36200, 0x90 byte(s)
    E    +0x000: 70ce3a80 017bfe80 00000060 70c81e00
    E    +0x010: 00000001 70d7cf80 70c9d400 70d7cb80
    E    +0x040: 0177cd00 ffffffff 00000000 00000004
    E    +0x050: 00000008 00000000 12190000 00000008
    E    +0x060: 00000000 00000000 70ddd000 70dec000
    E    +0x070: 00000005 00000000 0177cbc0 00000000

### pc rule: mfc_hist pcs are +4, branch_ring pcs are exact

mfc_hist "ch16 at pc 0x06400" <-> the image's `wrch ch16` is at 0x063fc;
"ch18 at pc 0x06408" <-> `wrch ch18` at 0x06404; "ch19 at pc 0x068ec" <-> at
0x068e8. Constant +4. branch_ring already reports the instruction's own pc.

## 2. The failing transfer is a leaf helper entered with EA = 0

Verbatim from spu_fault_00000200_06424.ls:

    063e0: 327fbc00  br r0,0x061c0
    063e8: 04000187  ori r7,r3,0        r7 = r3 = MFC_LSA
    063f0: 40804006  il r6,0x80         size 0x80
    063f8: 40802005  il r5,0x40         cmd 0x40 (GET)
    063fc: 21a00807  wrch ch16,r7       MFC_LSA
    06404: 21a00904  wrch ch18,r4       MFC_EAL  <- THE ZERO (r4 on entry)
    06408: 40800004  il r4,0x0
    0640c: 21a00986  wrch ch19,r6       MFC_Size
    0641c: 21a00a04  wrch ch20,r4       MFC_TagID 0
    06424: 21a00a85  wrch ch21,r5       MFC_Cmd 0x40   <- refused

A scan of every rel16 branch in the image finds **exactly one caller**:

    06900: 20004468  brz r104,0x06b20
    06b30: 337f1700  brsl r0,0x063e8
    06b34: 327fba00  br r0,0x06904

## 3. The branch, and its deciding input

    06868: 40824833  il r51,0x490
    06870: 43b1805f  ila r95,0x36300
    06874: 33df31b1  lqr r49,0x36200    r49 = the payload block
    06884: 788cd930  mpy r48,r50,r51
    0688c: 21a00834  wrch ch16,r52      LSA 0x36300
    068a8: 21a0092e  wrch ch18,r46      EA = block+0x0c + 0x490*n   VALID
    068ac: 21a009ad  wrch ch19,r45      size 0x490
    068b8: 21a0082c  wrch ch16,r44      LSA 0x3b100
    068c0: 33df2a27  lqr r39,0x36210
    068e4: 21a00923  wrch ch18,r35      EA = block+0x18 + 0x930*n   VALID
    068e8: 21a009a2  wrch ch19,r34      size 0x930
    06900: 20004468  brz r104,0x06b20   <<<<<< THE BRANCH
    06b20: 42d08003  ila r3,0x1a100     LSA = 0x1a100
    06b24: 33dee7b5  lqr r53,0x36260    r53 = the quad at block+0x60
    06b28: 408000e8  il r104,0x1
    06b2c: 3f811a84  rotqbyi r4,r53,4
    06b30: 337f1700  brsl r0,0x063e8    -> GET 0x80 bytes from that word
    06b34: 327fba00  br r0,0x06904

**The branch is `brz r104,0x06b20` at SPU LS 0x6900. The deciding input is
r104, and r104 = 0.** r104 is kernel-local: a scan of all 31 writers of r104 in
the 256 KB image leaves exactly two inside this routine --

    067a4: 32800068  fsmbi r104,0x0     <- 0 on entry to the descriptor run
    06b28: 408000e8  il r104,0x1       <- 1 only after the 0x6b20 path runs

-- so the console takes this branch too, on its first element. **The branch is
not the divergence.** It is the kernel's own first-pass action.

**Correction to the brief's premise.** The dump line "ch18 <- 0x70c95400 at pc
0x068e8" is the `wrch` at 0x068e4, and it is not an alternative to 0x6404: both
are sequential in the same loop body, and 0x63e8's only caller is 0x6b30. There
is no "valid-address path through 0x6408". The valid EAs are emitted at 0x68a8
and 0x68e4 by different instructions.

## 4. rotqbyi: which word actually becomes the EA (brief says +0x60; it is +0x64)

The EA is not computed -- it is a word of the payload read by the pair
`lqr r53,0x36260` / `rotqbyi r4,r53,4`. Which word `rotqbyi rt,ra,i7` puts in
the preferred slot was settled three ways, in ONE snapshot, against the
mfc_hist values of the same run:

| emitter | lqr quad | imm | predicted LS word | observed ch18 | verdict |
|---|---|---|---|---|---|
| 0x06740 | 0x36200 | 4  | block+0x04 = 0x017bfe80 | 0x017bfe80 | MATCH |
| 0x0689c | 0x36200 | 12 | block+0x0c = 0x70c81e00 | 0x70c81e00 | MATCH |
| 0x068cc | 0x36210 | 8  | block+0x18 = 0x70c9d400 | 0x70c9d400 | MATCH |

So `pref(rotqbyi(x, i7))` is the big-endian word at **LS (quad base + i7)**.
This agrees with the reference (refs/rpcs3, SPUInterpreter.cpp:972-978: buf is
the quadword twice, load at `buf + (16 - i7)`, so result byte 0 = a[16-i7] and
the preferred word is the one at byte offset i7).

Therefore:

    0x6b24  lqr r53,0x36260      quad base = block + 0x60
    0x6b2c  rotqbyi r4,r53,4     -> block + 0x64

**The refused effective address is guest payload+0x64, not payload+0x60.**
Both are 0 in this snapshot, which is why every earlier report of "the zero at
+0x60" reproduced and nobody caught the one-word offset. The sibling consumer
`0x7f80 rotqbyi r89,r90,8` reads block+0x68 = 0x70ddd000, a valid address, and
works -- the same quad holds working pointers at +0x68/+0x6c and a zero at +0x64.

## 5. Where payload+0x64 comes from: the PPU never writes it

The 0x90-byte block at LS 0x36200 is DMA'd from guest EA 0x0177cc00 by the
SPURS kernel's command dispatcher (0x7698-0x76c0: LSA 0x36200, EA = the posted
command payload, size 0x90, cmd 0x40), and the runtime's tracker confirms it
(thread.cpp:1182 keys on exactly ea 0x0177cc00 size 0x90). So
LS block+0x64 <-> guest 0x0177cc64.

Write census of the 36 words of that payload over the whole run
(PS3BASE_BLOCKWATCH, 507 distinct write keys):

    +0x00..+0x44  written      +0x48  NEVER WRITTEN
    +0x4c..+0x60  written      +0x64  NEVER WRITTEN
    +0x68..+0x78  written      +0x7c  NEVER WRITTEN
    +0x80..+0x8c  written

+0x64 is one of exactly three words no PPU code writes. Its only writer in the
entire program, found by disassembling the real EBOOT.elf, is

    0x009208f0  8122825c   lwz  r9,-32164(r2)   r9 = 0x0177cc00  (payload base)
    0x009208f4  801e0000   lwz  r0,0(r30)
    0x009208f8  38600000   li   r3,0
    0x009208fc  90090064   stw  r0,100(r9)      payload+0x64 = *(r30)   <-- only writer

inside f_009207d4 = __gafMP3 (name from the title's own assert table, M3), and
the disassembly shows its body is `cellFsLseek/cellFsRead/cellFsClose` (imports
213/204/202) around that store -- i.e. an asset loader fills +0x64.

That function never runs before the abort: blockwatch records **zero** writes to
0x0177cc00..0x0177cc90 from any pc in 0x9207d4..0x92093c, and none of the 507
write keys carries such an lr. Its only caller is
`rdr_093.cpp:6032 ctx.lr = 0x92a660; f_009207d4(...)`, in the mixer-create
region after cellAudioPortStart.

## 6. Answer

- **Branch: `brz r104,0x06b20` at SPU LS 0x6900.** It selects the extra
  0x80-byte GET of the payload quadword based at block+0x60.
- **Deciding input: r104 = 0**, set by `fsmbi r104,0x0` at 0x67a4 and cleared to
  1 at 0x6b28 -- entirely kernel-local, no memory and no channel. The console
  takes this branch too; it is not the divergence.
- **The value that decides the EA: LS 0x36264 = guest payload+0x64 = 0**, read by
  `lqr r53,0x36260` + `rotqbyi r4,r53,4`. (The brief's "+0x60" is the quadword
  base; the selected word is +0x64.)
- **Where that value comes from: nowhere -- the PPU never writes payload+0x64.**
  Its single writer is `stw r0,100(r9)` at guest 0x009208fc in f_009207d4
  (__gafMP3), and that function does not run before the SPURS abort. The field is
  BSS zero-fill (the ELF's PT_LOAD has no initial value there), so the SPU DMAs
  in a zero and issues a GET from EA 0.

## 7. Proposed fix

Fixing the SPU is fixing the wrong machine: the kernel code is conformant to the
console and the branch it takes is the console's. Making 0x6900 not taken would
suppress a transfer the console issues, and the 0x90-byte payload GET at 0x76b0
would still carry the same zero.

The defect is that the payload is posted before the field that describes its
buffer is filled:

1. **Find why __gafMP3 is not reached before the SPURS command is posted.** Its
   only caller is 0x92a660 in the post-cellAudioPortStart mixer setup. Either an
   earlier call in that sequence returns early on a path our HLE answers
   differently from the console, or the command is posted from a different
   sequence than the console's. This is the next measurement: log the calls of
   `f_0092a3ec`/`f_0092a50c`/`f_00926560` and of 0x92a660 in the same run as the
   refusal, and compare the ordering with the console's.
2. Only if __gafMP3 does run on the console after the post should a consumer-side
   change be considered, and then it belongs in the SPURS kernel's own
   sequencing, not in a guest field.

A guard that returns an explicit error instead of a fake success is appropriate
while this is open: the payload should be posted only with +0x64 populated, and
a post with +0x64 == 0 should be reported by name at the PPU site rather than
surfacing 20 s later as an MFC refusal.

## 8. Next measurement (not taken here)

One run, same conditions, logging: every call/return of f_009207d4 and of its
caller 0x92a660 with timestamps; the write to payload+0x64; the post of the
0x90-byte command; and the abort. That decides between "our HLE skipped a step"
and "the title really does post first", which is the only remaining fork in the
fix.
