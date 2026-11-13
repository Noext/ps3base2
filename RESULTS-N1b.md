# RESULTS N1b -- the SPU branch that selects the zero effective address

Agent: N1b. Task: with PS3BASE_SPU_REFUSAL_DUMP=1, find the branch that leads
to the zero in the effective address, the value that decides it, and where that
value comes from. Disassemble SPU code around 0x6600-0x6960 and 0x63e0-0x6440.

Status: running.

## Method and ground truth

The block capture in out-rdr/gen/blocks/run_capture.bin CANNOT be used for this
window: overlapping captures of the same LSAs conflict. Reconstruction of
0x63e0-0x6960 from it yields 634485 conflicting words over 696 addresses. The
262144-byte local-store dump the runtime itself wrote at the DMA fault
(spu_fault_00000200_06424.ls, SPU thread 0x200, the exact thread and pc of the
refusal in /tmp/N1-dump.log) is the ground truth. Its block at 0x36200 is
byte-identical to the block in the refusal dump:

    36200: 70cdfa80 017bfe80 00000060 70c7de00
    36210: 00000000 70d78f80 70c99400 70d78b80
    36220: 70cd0880 70cd0900 00000000 00000001
    36230: 00000012 70dd8f80 70ce0380 70d78380
    36240: 0177cd00 ffffffff 00000000 00000004
    36250: 00000008 00000000 12190000 00000008
    36260: 00000000 00000000 70dd9000 70de8000   <- +0x60 = 0
    36270: 00000005 00000000 0177cb80 00000000

    +0x58 = 0x12190000   (valid audio port address, from cellAudioPortOpen)
    +0x60 = 0            (the value that becomes the EA)

### pc correction: the dump's mfc_hist pcs are +4, branch_ring pcs are exact

mfc_hist says 'ch16 <- 0x1a100 at pc 0x06400'; the image's wrch ch16 is at
0x063fc. 'ch18 <- 0 at pc 0x06408'; the image's wrch ch18 is at 0x06404.
'ch19 <- 0x930 at pc 0x068ec'; the image has wrch ch19 at 0x068e8. Every entry
is consistent with a constant +4. The branch_ring pcs need no correction
('pc 0x06900 ... TAKEN' is the brz at 0x06900).

## Measurement 1 -- the failing transfer is a leaf helper, entered with EA=0

Verbatim from spu_fault_00000200_06424.ls:

    063e0: 327fbc00  br r0,0x061c0
    063e4: 00200000  lnop
    063e8: 04000187  ori r7,r3,0        r7 = r3 = MFC_LSA
    063ec: 24004080  stqd r0,16(r1)
    063f0: 40804006  il r6,0x80         size = 0x80
    063f4: 24ffc0d0  stqd r80,-16(r1)
    063f8: 40802005  il r5,0x40         cmd = 0x40 (GET)
    063fc: 21a00807  wrch ch16,r7       MFC_LSA
    06400: 0400018a  ori r10,r3,0
    06404: 21a00904  wrch ch18,r4       MFC_EAL  <<< THE ZERO (r4 on entry)
    06408: 40800004  il r4,0x0
    0640c: 21a00986  wrch ch19,r6       MFC_Size = 0x80
    06410: 4020007f  nop
    06414: 24ff4081  stqd r1,-48(r1)
    06418: 1cf40081  ai r1,r1,-48
    0641c: 21a00a04  wrch ch20,r4       MFC_TagID = 0
    06420: 12000309  hbrr r9,0x6
    06424: 21a00a85  wrch ch21,r5       MFC_Cmd = 0x40   <<< refused

This helper has EXACTLY ONE caller in the whole local store (scan of every
rel16 branch in the image):

    06900 brz r104,0x06b20
    06b30 brsl r0,0x063e8     <- the only call of 0x63e8
    06b34 br r0,0x06904

## Measurement 2 -- the caller reads the EA straight out of the guest block

    06b20: 42d08003  ila r3,0x1a100     LSA  = 0x1a100 (the DMA scratch buffer)
    06b24: 33dee7b5  lqr r53,0x36260    r53  = block + 0x60
    06b28: 408000e8  il r104,0x1        r104 = 1
    06b2c: 3f811a84  rotqbyi r4,r53,4   r4   = low word of block+0x60 = 0
    06b30: 337f1700  brsl r0,0x063e8    -> GET 0x80 bytes from EA = 0
    06b34: 327fba00  br r0,0x06904

The EA is not computed and not masked: it is the guest descriptor field
block+0x60, passed verbatim as the MFC_EAL.

## Measurement 3 -- the branch, and the premise it corrects

    06868: 40824833  il r51,0x490
    0686c: 3f8082b2  rotqbyi r50,r5,2
    06870: 43b1805f  ila r95,0x36300
    06874: 33df31b1  lqr r49,0x36200    r49 = block
    06878: 4082482d  il r45,0x490
    0687c: 32800050  fsmbi r80,0x0
    06880: 04002fb4  ori r52,r95,0
    06884: 788cd930  mpy r48,r50,r51
    06888: 4084982a  il r42,0x930
    0688c: 21a00834  wrch ch16,r52      LSA 0x36300
    06890: 43d88070  ila r112,0x3b100
    06894: 00200000  lnop
    06898: 40849822  il r34,0x930
    0689c: 3f8318af  rotqbyi r47,r49,12
    068a0: 0400382c  ori r44,r112,0
    068a4: 180c17ae  a r46,r47,r48
    068a8: 21a0092e  wrch ch18,r46      EA = block+0x0c + 0x490*n    (VALID)
    068ac: 21a009ad  wrch ch19,r45      size 0x490
    068b0: 21a00a50  wrch ch20,r80      tag 0
    068b4: 21a00ad6  wrch ch21,r86      cmd 0x40
    068b8: 21a0082c  wrch ch16,r44      LSA 0x3b100
    068bc: 33dfe8ab  lqr r43,0x36800
    068c0: 33df2a27  lqr r39,0x36210    r39 = block + 0x10
    068c4: 33a6b9a8  lqr r40,0x19e90
    068c8: 3f8095a9  rotqbyi r41,r43,2
    068cc: 3f8213a4  rotqbyi r36,r39,8
    068d0: 181a5426  a r38,r40,r105     r105 = 0x490 (bump the list pointer)
    068d4: 00200000  lnop
    068d8: 788a94a5  mpy r37,r41,r42
    068dc: 23a6b6a6  stqr r38,0x19e90
    068e0: 18095223  a r35,r36,r37
    068e4: 21a00923  wrch ch18,r35      EA = block+0x10 + 0x930*n    (VALID)
    068e8: 21a009a2  wrch ch19,r34      size 0x930
    068ec: 21a00a50  wrch ch20,r80      tag 0
    068f0: 21a00ad6  wrch ch21,r86      cmd 0x40
    068f4: 33a6b3a1  lqr r33,0x19e90
    068f8: 181a90a0  a r32,r33,r106     r106 = 0x930
    068fc: 23a6b2a0  stqr r32,0x19e90
    06900: 20004468  brz r104,0x06b20   <<<<< THE BRANCH
    06904: 21a00bd0  wrch ch23,r80      (not-taken path: tag wait)

**Correction to the brief's premise.** The dump line 'ch18 <- 0x70c95400 at
pc 0x068e8' is the wrch at 0x068e4 (the +4 rule). It is a DIFFERENT instruction
from the refused one: 0x68e4 writes block+0x10 + 0x930*n, an RSX-local address,
and 0x6404 writes r4. Those two are not alternatives chosen by a branch -- they
are sequential in the same loop body. The only call of the 0x63e8 helper is the
one at 0x6b30, so nothing else ever reaches 0x6404 with a valid r4. There is no
'valid-address path' through 0x6408; there is a valid EA path that does not go
through 0x6408 at all.

What the 0x6900 branch does is decide whether an EXTRA 0x80-byte GET of the
descriptor at block+0x60 is issued. Its input r104 is set entirely inside the
same routine:

    067a4: 32800068  fsmbi r104,0x0     <- 0 on entry to this descriptor run
    ...
    06b28: 408000e8  il r104,0x1        <- 1 only after the 0x6b20 path runs

Scan of every writer of r104 in the 256 KB image (31 sites; the only two in
this routine are 0x67a4 and 0x6b28):

    00bd0 lqa  00dd8 ila  03674 stqd 0373c il  03a90 lqd  041bc rotqbyi
    06634 stqd 066e4 lqd  067a4 fsmbi  06b28 il  079e8 stqd 07cdc fsmbi
    07d1c a   08090 andi 081e8 ai     083b8 lqr  093c8 clgti 0962c lqr
    0de7c stqd 0df44 andi 0e44c lqd  10958 stqd 10c5c lqd  11400 shli
    13038 stqd 13250 ceqbi 13408 il   1385c rotqbyi 13928 a  13bc8 il  13cb8 lqd

So at 0x6900 on the first descriptor element r104 is 0 and the branch is TAKEN;
after 0x6b28 writes 1, subsequent elements skip it. Both facts are decided
locally by the kernel's own code and register file -- no guest memory, no
channel, no input from the PPU participates. **The branch is therefore the
kernel's own first-pass action and the console executes it identically. The
branch is not the divergence.**

## Measurement 4 -- where the block comes from, and what +0x60 is

The 0x90-byte block at LS 0x36200 is DMA'd from guest EA 0x0177cc00 by the
SPURS kernel's system-service dispatcher:

    07698: 43b1001d  ila r29,0x36200    LSA
    0769c: 3fe0021c  shlqbyi r28,r4,0
    076a0: 4080481b  il r27,0x90        size 0x90
    076a4: 32800017  fsmbi r23,0x0
    076a8: 4080201a  il r26,0x40
    076ac: 21a0081d  wrch ch16,r29
    076b0: 21a0091c  wrch ch18,r28      EA = the posted command payload
    076b4: 21a0099b  wrch ch19,r27      size 0x90
    076b8: 21a00a17  wrch ch20,r23
    076bc: 12000308  hbrr r8,0x6
    076c0: 21a00a9a  wrch ch21,r26      cmd 0x40

The runtime's own tracker confirms it (thread.cpp:1182): a GET of exactly
ea 0x0177cc00 size 0x90 sets block_ls, and the refusal dump printed that block.
The command dispatch immediately above it compares the command word against
0xfafa / 0xdead / 0xfdfe (0x7650-0x7694), i.e. 0x0177cc00 is a posted SPURS
system-service command payload of 0x90 bytes.

So the failing GET's EA is `*(uint32 *)(command_payload + 0x60)`, a field of the
payload the PPU fills before posting the command. The PPU writer, verbatim from
out-rdr/ppu/rdr_092.cpp (f_0091fd18, __cellMSSystemInitBasic, r30 = 0x0177cc00):

    ctx.gpr[30] = ld32(base, gpr[2] + -32164);    r30 = 0x0177cc00
    ctx.gpr[18] = ld32(base, gpr[2] + -32116);    r18 = 0x0177cda8
    ctx.gpr[25] = ld32(base, gpr[18] + 0);        r25 = *0x0177cda8
    ppu::st32(base, gpr[30] + 96, gpr[25]);       payload+0x60 = *0x0177cda8

and the store to 0x0177cda8 that would give it a value is the one behind the
count gate at 0x00920054 (M1's D-013 chain):

    0x00920040  lwz   r0,0(r22)     r22 = 0x0177caa8 (count)
    0x00920048  cmpwi cr7,r0,0
    0x00920050  stw   r9,0(r28)     r28 = 0x0177cda4
    0x00920054  bc 4,30 -> skip    taken when count != 0
    0x00920064  stw   r9,0(r18)    0x0177cda8 = size  <- SKIPPED

and the gate is taken, so *0x0177cda8 stays 0, so payload+0x60 is written as 0.

## Answer

- **The branch is `brz r104,0x06b20` at SPU LS 0x6900.** Target 0x06b20 is the
  block+0x60 descriptor GET whose helper refills MFC_EAL from r4 at 0x6404.
- **The deciding input is r104, and r104 = 0.** It is set to 0 at 0x67a4 and to 1
  at 0x6b28 by the same routine, from no memory and no channel. Hence r104 is
  not where the divergence is: the console takes this branch too.
- **The value that decides the effective address is the guest field
  payload+0x60 = *(uint32*)0x0177cc60 = 0**, read by `lqr r53,0x36260` and
  rotated into r4 at 0x6b2c. The zero is a value, not a computation and not a
  branch outcome.
- **Where that value comes from:** the PPU writes it in __cellMSSystemInitBasic
  (f_0091fd18) as a copy of global 0x0177cda8, and 0x0177cda8 is left 0 because
  the count gate at PPU 0x00920054 skips the store (count 0x0177caa8 != 0).

## Proposed fix (to be measured before it is claimed)

The SPU is executing correct kernel code on a payload field the PPU left 0.
Fixing the SPU branch would be fixing the wrong machine: making 0x6900 not
taken would suppress a transfer the console issues, and the 0x90-byte payload
GET at 0x76b0 would still carry the same zero.

The candidate fix is therefore on the PPU producer, and it has one open question
the measurement above does not settle: whether the console also takes the
0x920054 gate (M1 argues yes, from bit 6 of the literal 0x10ca, with no load in
the deciding expression) -- in which case the defect is one level further up,
in what fills 0x0177caa8, not in the gate.

## Next measurement (not yet taken)

One RDR run on this branch's binary with, in the same run:
PS3BASE_SPU_REFUSAL_DUMP=1 (to confirm 0x6900 TAKEN with r104=0 and the block
at 0x36200), PS3BASE_BLOCKWATCH=0177ca00-0177ce00 (to capture every write to
0x0177cda8, 0x0177cc60 and 0x0177caa8), and GOWA_DUMP=0177cda8,0177caa8 at the
abort (to read the live values the gate decided on). Then a same-minute A/B
with the candidate PPU change.
