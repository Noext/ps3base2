# M3 -- MultiStream (cellMS*) and the origin of +0x60 of 0x0177cc00

Branch explore/m3 from main HEAD 4a5a132 (rebased onto 4d310dd).

Question: identify the title's cellMS* functions (init, configuration with the
audio port, mixer start on SPURS) and the arguments they receive. Does field
+0x060 of 0x0177cc00 correspond to an address that should come from
cellAudioGetPortConfig?

**Answer: no -- the cellAudioGetPortConfig address is at +0x58, not +0x60.**
+0x60 holds a copy of the separate global 0x0177cda8 (a DSP size field), the
count gate at 0x920054 skips the store that would set it, it therefore stays 0,
and the SPURS kernel reads it in a 0x90-byte MFC GET of the whole object. The
brief's chain is confirmed end to end, but its last step is about a different
field than the one the audio port occupies.

**Read Measurement 6 first: it corrects my own first write-up.** Sections 1-3
are the measurements as taken; Measurement 6 is the DMA-watch run that
overturned the conclusion I had drawn from them; Measurement 7 states what is
still open.

## Method

1. The title carries its own assert name table for the MultiStream module at
   0x114bc48, a list of NUL-terminated names; each function loads the table base
   once (TOC slot -32236 = 0x0114bc48) and adds a fixed byte offset. Scanning the
   whole lifted tree for (load TOC[-32236]) followed by (reg += N) picks the
   function's own name out of that table. The table offsets are read straight
   out of EBOOT.elf, not remembered.
2. One 36 s run with the byte-precise block watch over the whole region:
   PS3BASE_BLOCKWATCH=0177ca00-0177ce00 (log /tmp/M3-bw.log).
3. Every cellAudio import site in the lifted tree, read from the import table
   (table.cpp) and the lifted bodies.

## Measurement 1 -- the name table read from EBOOT.elf, and the function map

Name table base 0x114bc48, parsed from the ELF's second PT_LOAD
(file offset 0x113bc48, vaddr 0x114bc48):

    0x0114bc48  base  +0  cellMSSystemGetNeededMemorySize
    0x0114bc68  base +32  __cellMSSystemInitBasic
    0x0114bc80  base +56  __gafMP3
    0x0114bc90  base +72  __gaf
    0x0114bc98  base +80  cellMSSystemClose
    0x0114bcb0  base+104  cellMSSystemSignalSPU
    0x0114bcf0  base+168  cellMSCoreGetRouting
    0x0114bf88  base+832  __cellMSBusInit
    0x0114bf98  base+848  cellMSCoreStop
    0x0114bfa8  base+864  cellMSCoreInit
    0x0114bfb8  base+880  cellMSStreamGetPriorityBank
    0x0114c398  base+1872 __cellMSStreamInit
    0x0114c3b0  base+1896 cellMSStreamOpen
    0x0114c468  base+2080 cellMSDSPReset
    0x0114c4d0  base+2184 cellMSEnvSetup
    0x0114c4e0  base+2200 cellMSSurroundApplyToStreams

Function -> name, from the (TOC[-32236], +N) pattern in the lifted bodies:

    f_0091f364  +0    cellMSSystemGetNeededMemorySize   (matches the earlier proof)
    f_0091fd18  +32   __cellMSSystemInitBasic
    f_009207d4  +56   __gafMP3
    f_00920984  +104  cellMSSystemSignalSPU
    f_00921138  --    the caller of __cellMSSystemInitBasic (no assert of its own)

f_0091fd18's three assert sites all pass the same name, verbatim from
out-rdr/ppu/rdr_092.cpp:

    L_9206bc:  gpr[4] = ld32(TOC[-32236]); gpr[4] = gpr[4] + 32   -> __cellMSSystemInitBasic
    L_9206d8:  same
    L_920734:  same
               gpr[3] = ld32(TOC[-32240]) = 0x0117f8e0 MULTISTREAM ASSERT : %s was called with an invalid max streams (%d).
    L_9206ac:  gpr[3] = ld32(TOC[-32012]) = 0x0117fa50 MULTISTREAM ASSERT : %s was called but MultiStream hasn't been initialised.

The cellAudio imports the title uses, from the generated import table
(imports 275-281 are the cellAudio seven):

    275 ('cellAudio', '0B168F92', 'cellAudioInit')
    276 ('cellAudio', '4129FE2D', 'cellAudioPortClose')
    277 ('cellAudio', '5B1E2C73', 'cellAudioPortStop')
    278 ('cellAudio', '74A66AF0', 'cellAudioGetPortConfig')
    279 ('cellAudio', '89BE28F2', 'cellAudioPortStart')
    280 ('cellAudio', 'CA5AC370', 'cellAudioQuit')
    281 ('cellAudio', 'CD7BC431', 'cellAudioPortOpen')

## Measurement 2 -- the init / configure / mixer-start sequence and its arguments

One PPU function does all of it: f_006099e8, on main_thread. Verbatim from
out-rdr/ppu/rdr_061.cpp lines 5349-5613:

    5351  st32(r1+176, gpr[31])       gpr[31] = ld32(0x1720000+4+10044)  audio mode id
    5355  st32(r1+180, 18)
    5356  st32(r1+184, 5)
    5357  st32(r1+188, 4298)          0x10ca
    5357  ctx.lr = 0x609a40; f_0091f364(ctx, base);   cellMSSystemGetNeededMemorySize -> 0x001f0aaa
    5499  ctx.lr = 0x609c50; ppu::call_import(ctx, base, 275);  cellAudioInit()
    5506  st64(r1+112, 8)             CellAudioPortParam.channels = 8
    5508  st64(r1+120, 8)             CellAudioPortParam.nBlock   = 8
    5510  ctx.lr = 0x609c7c; ppu::call_import(ctx, base, 281);  cellAudioPortOpen(&param, &portNum)
    5516  ctx.lr = 0x609c94; ppu::call_import(ctx, base, 278);  cellAudioGetPortConfig(portNum, &cfg)
    5535  ctx.lr = 0x609ce0; f_00921138(ctx, base);             caller of __cellMSSystemInitBasic
    5551  ctx.lr = 0x609d1c; f_0092260c(ctx, base);             cellMSCore routing loop, i = 0..19
    5558  ctx.lr = 0x609d38; f_00922fe4(ctx, base);
    5562  ctx.lr = 0x609d48; f_00921fe0(ctx, base);
    5569  ctx.lr = 0x609d64; f_0091f28c(ctx, base);             r3 = r1+112, r4 = r1+144 (= &cfg)
    5588  ctx.lr = 0x609db0; f_0092a3ec(ctx, base);             mixer object create
    5599  ctx.lr = 0x609ddc; f_0092a50c(ctx, base);
    5613  ctx.lr = 0x609e10; f_00926560(ctx, base);             per-stream start loop

The arguments __cellMSSystemInitBasic receives, at its only call site (inside
f_00921138, 0x9211ec):

    27402  gpr[31] = gpr[5]
    27403  gpr[30] = gpr[3]
    27426  gpr[3] = gpr[30]                (u32)arg3
    27428  gpr[4] = gpr[28]                (u32)arg4
    27432  ctx.lr = 0x9211ec; f_0091fd18(ctx, base);

so the arguments are the caller's r3/r4 unchanged. The caller's r3 is
ld32(0x13a0000+25792) -- the byte count cellMSSystemGetNeededMemorySize
returned -- and r4 is the 4-word descriptor at r1+176: {mode, 18, 5, 0x10ca},
which yields 0x001f0aaa bytes. f_0091fd18 then stores into the object at
0x0177cc00 (r30) and into the region beginning 0x0177cd80.

## Measurement 3 -- the port address goes to object+0x58, and +0x60 is written once, as 0

One run, PS3BASE_BLOCKWATCH=0177ca00-0177ce00, 36.66 s until the SPURS abort,
901 flips:

    $ grep -a 'rsx: flip' /tmp/M3-bw.log | tail -1
    I  35.061013 [4022179] rsx: flip 901 (buffer 0) worst 1679.9 ms, >50ms 1, >100ms 1

The port config the HLE returned (out+28 = port address):

    $ grep -a cellAudio /tmp/M3-bw.log | grep -E 'Init|open'
    I   3.630154 [4022159] cellAudio: cellAudioInit
    I   3.630256 [4022159] cellAudio: port 0 open: 8 ch, 8 blocks at 0x12190000
    I  16.098545 [4022315] cellAudio: port 1 open: 2 ch, 16 blocks at 0x121a0000

The MultiStream configure function f_0091f28c, called with r4 = the
GetPortConfig output buffer, stores the config into three globals at 6.37-6.43 s:

    $ grep -a 'blockwatch: WRITE' /tmp/M3-bw.log | grep -E '0177ca8c|0177ca90|0177ca94'
    I   6.376018 blockwatch: WRITE +0x08c (0x0177ca8c) w4 prev 0x00000000 -> 0x12180000 by rsv::store_range | guest lr 0x00609d64 ...
    I   6.403223 blockwatch: WRITE +0x090 (0x0177ca90) w4 prev 0x00000000 -> 0x00000008 by rsv::store_range | guest lr 0x00609d64 ...
    I   6.429059 blockwatch: WRITE +0x094 (0x0177ca94) w4 prev 0x00000000 -> 0x12190000 by rsv::store_range | guest lr 0x00609d64 ...

    0x0177ca8c = cfg+0  = index-area address  0x12180000  (HLE g_index_area + 0*8)
    0x0177ca94 = cfg+28 = port address        0x12190000  (HLE p.addr, matches the open line)
    0x0177ca90 = cfg+16 = block count         8

The mixer start then copies that address into the MultiStream object and it
lands at 0x0177cc58:

    $ grep -a 'blockwatch: WRITE' /tmp/M3-bw.log | grep -E '0177cc58|0177cc60'
    I   4.899153 blockwatch: WRITE +0x260 (0x0177cc60) w4 prev 0x00000000 -> 0x00000000 by rsv::store_range | guest lr 0x0177cde0 | thread main_thread
    I   6.840240 blockwatch: WRITE +0x258 (0x0177cc58) w4 prev 0x00000000 -> 0x12190000 by rsv::store_range | guest lr 0x0060a724 | thread [RAGE Audio] Multistream

guest lr 0x0060a724 is inside f_0060a6e8, the function that calls
cellAudioPortStart(0) and then cellMSSystemSignalSPU:

    $ sed -n '6305,6317p' out-rdr/ppu/rdr_061.cpp  (verbatim)
    ctx.gpr[3] = 0 + 24248320;
    ctx.gpr[4] = 0 + 24248320;
    ctx.gpr[5] = 0 + 1;
    ctx.gpr[3] = uint64_t(ppu::ld32(base, uint32_t(ctx.gpr[3] + 9984)));
    ppu::st8(base, uint32_t(ctx.gpr[4] + 9920), uint8_t(ctx.gpr[5]));
    ctx.lr = 0x60a70c; ppu::call_import(ctx, base, 279);      cellAudioPortStart(portNum)
    ...
    ctx.lr = 0x60a720; f_00609468(ctx, base);
    ctx.lr = 0x60a724; f_00920984(ctx, base);                cellMSSystemSignalSPU

cellMSSystemSignalSPU stores the port config fields into the object at
+0x44..+0x5c (verbatim, r22 = ld32(TOC[-32164]) = 0x0177cc00):

    27006  ctx.gpr[22] = ld32(base, gpr[2] + -32164);   // 0x0177cc00
    27020  st32(gpr[22] + 68,  gpr[7]);                 // +0x44
    27021  st32(gpr[22] + 80,  gpr[10]);                // +0x50
    27022  st32(gpr[22] + 84,  gpr[9]);                 // +0x54
    27024  st32(gpr[22] + 76,  gpr[31]);                // +0x4c
    27025  st32(gpr[22] + 92,  gpr[6]);                 // +0x5c
    27026  st32(gpr[22] + 88,  gpr[8]);                 // +0x58

The +0x60 write happens 1.9 s EARLIER, in the object's template copy: one burst
of 24 stores over 0x0177cc00..0x0177cc8c at 4.60-5.13 s, all with guest lr
0x0177cde0 (a copy routine whose return address is inside the block being
written), and it stores 0 into +0x60.

Whole-run write map of the object (first write per field, same run, parsed by
/tmp/wmap.py from every blockwatch WRITE line):

    +0x200 0177cc00 w4 00000000->70ccfa80 lr=0177cde0 main_thread  (n=1)
    +0x204 0177cc04 w4 00000000->017bfe80 lr=0092a61c main_thread  (n=1)
    +0x208 0177cc08 w4 00000000->00000060 lr=0177cde0 main_thread  (n=1)
    +0x20c 0177cc0c w4 00000000->70c6de00 lr=0177cde0 main_thread  (n=1)
    +0x210 0177cc10 w4 00000000->00000000 lr=009204fc main_thread  (n=3)
    +0x218 0177cc18 w4 00000000->70c89400 lr=0177cde0 main_thread  (n=1)
    +0x21c 0177cc1c w4 00000000->70d68b80 lr=0177cde0 main_thread  (n=1)
    +0x220 0177cc20 w4 00000000->70cc0880 lr=0177cde0 main_thread  (n=1)
    +0x224 0177cc24 w4 00000000->70cc0900 lr=0177cde0 main_thread  (n=1)
    +0x228 0177cc28 w4 00000000->00000000 lr=0177cde0 main_thread  (n=1)
    +0x22c 0177cc2c w4 00000000->00000000 lr=009211ec main_thread  (n=2)
    +0x230 0177cc30 w4 00000000->00000012 lr=0177cde0 main_thread  (n=1)
    +0x234 0177cc34 w4 00000000->70dc8f80 lr=0177cde0 main_thread  (n=1)
    +0x238 0177cc38 w4 00000000->70cd0380 lr=0177cde0 main_thread  (n=1)
    +0x23c 0177cc3c w4 00000000->70d68380 lr=0177cde0 main_thread  (n=1)
    +0x240 0177cc40 w4 00000000->0177cd00 lr=0177cde0 main_thread  (n=1)
    +0x244 0177cc44 w4 00000000->ffffffff lr=0060a724 [RAGE Audio] Multistream  (n=1)
    +0x24c 0177cc4c w4 00000000->00000000 lr=0060a724 [RAGE Audio] Multistream  (n=4)
    +0x250 0177cc50 w4 00000000->00000008 lr=0060a724 [RAGE Audio] Multistream  (n=1)
    +0x254 0177cc54 w4 00000000->00000000 lr=0060a724 [RAGE Audio] Multistream  (n=1)
    +0x258 0177cc58 w4 00000000->12190000 lr=0060a724 [RAGE Audio] Multistream  (n=1)
    +0x25c 0177cc5c w4 00000000->00000008 lr=0060a724 [RAGE Audio] Multistream  (n=1)
    +0x260 0177cc60 w4 00000000->00000000 lr=0177cde0 main_thread  (n=1)
    +0x268 0177cc68 w4 00000000->70dc9000 lr=0177cde0 main_thread  (n=1)
    +0x26c 0177cc6c w4 00000000->70dd8000 lr=0177cde0 main_thread  (n=1)
    +0x270 0177cc70 w4 00000000->00000005 lr=0177cde0 main_thread  (n=1)
    +0x274 0177cc74 w4 00000000->00000000 lr=00922bdc main_thread  (n=2)
    +0x278 0177cc78 w4 00000000->0177cb80 lr=00920d74 [RAGE Audio] Multistream  (n=2)
    +0x280 0177cc80 w4 00000000->70e13800 lr=0177cde0 main_thread  (n=1)
    +0x284 0177cc84 w4 00000000->70e21000 lr=0177cde0 main_thread  (n=1)
    +0x288 0177cc88 w4 00000000->70e24dc0 lr=0177cde0 main_thread  (n=1)
    +0x28c 0177cc8c w4 00000000->70e54dc0 lr=0177cde0 main_thread  (n=1)

+0x60 is written once, as 0, at 4.899 s, and never again.

## Measurement 4 -- no PPU load of object+0x60 exists (partly superseded by Measurement 6)

Static, over every lifted PPU body (rdr_*.cpp, libsre_*.cpp), tracking any
register loaded from one of the four TOC slots that alias 0x0177cc00
(-32164, -31896, -31728, -31324 -- the title reaches this object through four
different pointers, which is why one slot scan alone would have missed it):

    $ python3 /tmp/readers2.py
    rdr_092.cpp 26363 f_0091fd18 st32 +96
    rdr_092.cpp 26365 f_0091fd18 st32 +104
    rdr_092.cpp 26366 f_0091fd18 st32 +108
    rdr_092.cpp 26372 f_0091fd18 st32 +112
    rdr_092.cpp 26376 f_0091fd18 st32 +128
    rdr_092.cpp 26378 f_0091fd18 st32 +132
    rdr_092.cpp 26379 f_0091fd18 st32 +136
    rdr_092.cpp 26380 f_0091fd18 st32 +140
    rdr_092.cpp 26793 f_009207d4 st32 +100
    rdr_092.cpp 27021 f_00920984 st32 +80
    rdr_092.cpp 27022 f_00920984 st32 +84
    rdr_092.cpp 27025 f_00920984 st32 +92
    rdr_092.cpp 27026 f_00920984 st32 +88
    rdr_092.cpp 27121 f_00920984 st32 +120
    rdr_092.cpp 27773 f_009215cc st32 +116
    rdr_092.cpp 32061 f_00924f20 ld32 +116

**Part of this measurement is superseded: there is no PPU LOAD of +0x60, but the SPU reads the whole object by MFC GET -- see Measurement 6.** The only reference to +96 (0x60) in the PPU code is the store at 26363. There is no PPU load of
+0x60 anywhere in the lifted tree, so a zero there cannot be the value the SPU
consumed.

Address arithmetic on the brief's chain:

    0x0177cda8 - 0x0177cc00 = 0x1a8
    0x0177cda8 = 0x0177cd80 + 0x28

0x0177cd80 is the *next* object, the one __cellMSSystemInitBasic lays out.
Verbatim (rdr_092.cpp):

    25978  ctx.gpr[30] = ld32(base, gpr[2] + -32164);   r30 = 0x0177cc00  the object it fills
    26189  ctx.gpr[18] = ld32(base, gpr[2] + -32116);   r18 = 0x0177cda8
    26192  st32(base, gpr[18] + 0, gpr[9]);             the store the count gate skips
    26345  ctx.gpr[25] = ld32(base, gpr[18] + 0);        and its reader, same function

The gate, verbatim from the ELF at 0x920048 (words around it, big-endian):

    0x00920040  lwz    r0,0(r22)          r22 = 0x0177caa8 (count)
    0x00920048  cmpwi  cr7,r0,0
    0x0092004c  addi   r11,r9,1024
    0x00920050  stw    r9,0(r28)          r28 = 0x0177cda4
    0x00920054  bc     4,30 -> 0x009206f4  branch if cr7.eq, i.e. skip when count != 0
    0x00920058  lwz    r18,-32116(r2)      0x0177cda8
    0x00920060  addi   r11,r11,16384
    0x00920064  stw    r9,0(r18)

In the run, 0x0177cda8 is never written, and the count word the gate reads
toggles with the caller's own bookkeeping:

    $ grep -ac 'WRITE.*0177cda8' /tmp/M3-bw.log
    0

    I   3.512370 WRITE +0x0a8 (0x0177caa8) w4 prev 0x00000000 -> 0x00000000 | lr 0x00609a40
    I   3.568175 WRITE +0x0a8 (0x0177caa8) w4 prev 0x00000000 -> 0x00000001 | lr 0x00609a40
    I   3.664965 WRITE +0x0a8 (0x0177caa8) w4 prev 0x00000001 -> 0x00000000 | lr 0x0091fd98
    I   3.712710 WRITE +0x0a8 (0x0177caa8) w4 prev 0x00000000 -> 0x00000001 | lr 0x0091fd98
    I   3.771893 WRITE +0x0a8 (0x0177caa8) w4 prev 0x00000001 -> 0x00000000 | lr 0x0091fdb0
    I   3.871673 WRITE +0x0a8 (0x0177caa8) w4 prev 0x00000000 -> 0x00000001 | lr 0x0091fdb0

(0x0177caa8 is written between f_0091f364's and f_0091fd18's stores, exactly as
M1 measured; it is that function's own counter and the 0x920048 test reads it.
On the offending pass the count is 1, the gate skips the store, and 0x0177cda8
-- a size field of the cd80 block, not a pointer to an audio buffer -- stays 0.
The SPU cannot have loaded 0x0177cda8 as object+0x60 because that field is not
object+0x60.)

## Measurement 5 -- the abort, in the same run

    $ grep -a 'REFUSED' /tmp/M3-bw.log
    E  36.634194 [4022169] spu: thread 0x200 SecondaryCellSpursKernel0 DMA GET REFUSED cmd 0x40 (unmapped) ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428
    E  36.634250 [4022169] spu: thread 0x200 raised SPU exception 0xffffffff53505503 on queue 0x8d006100 -> 0x0
    E  36.660039 [4022168] tty: [0] The SPURS is aborted.
    I  36.660061 [4022168] process: _sys_process_exit(0x8001000c)

The refusing transfer is at SPU pc 0x06428 with lsa 0x1a100 and ea 0. Its
parameters are the SPURS kernel's own; nothing in the PPU object at 0x0177cc00
is read by the kernel at that point (Measurement 4), and the only field of that
object the mixer filled with a real audio address is +0x58 = 0x12190000.

## Conclusion (partly superseded -- read Measurement 6)

- The title's MultiStream entry points, named from its own assert table:
  cellMSSystemGetNeededMemorySize f_0091f364, __cellMSSystemInitBasic
  f_0091fd18, cellMSSystemSignalSPU f_00920984, __gafMP3 f_009207d4, plus
  cellMSCore* (f_00921694..f_00923fbc), cellMSStream*
  (f_00924430..f_00927da4), cellMSDSPReset f_0092801c/f_009284e8,
  cellMSEnvSetup f_00928944, cellMSSurround* f_00929250.
- Configuration with the audio port happens in f_0091f28c, called by the RAGE
  audio init immediately after cellAudioGetPortConfig(portNum, &cfg), with
  r4 = &cfg. It stores cfg+0 -> 0x0177ca8c, cfg+16 -> 0x0177ca90,
  cfg+28 (the port address 0x12190000) -> 0x0177ca94.
- Mixer start on SPURS: f_0060a6e8 calls cellAudioPortStart(0) and then
  cellMSSystemSignalSPU, which copies the audio port address into the
  MultiStream object at +0x58 (0x0177cc58); the stream-start loop f_00926560
  runs per stream.
- +0x60 of 0x0177cc00 is NOT the audio port address. The port address is at
  +0x58. +0x60 is written once, as 0, by the object's template copy, and has no
  reader anywhere in the lifted tree.
- The brief's last link does not hold: 0x0177cda8 is 0x0177cd80+0x28, a size
  field of the next object (0x1a8 bytes past 0x0177cc00), not object+0x60. The
  count gate at 0x920048 really does skip its store, but what stays 0 is a size
  in the cd80 block, so the SPU loading that 0 into wrch ch18 is not established
  by this chain. The DMA to ea 0 at SPU pc 0x06428 has to be traced on the SPU
  side, from the kernel's own descriptor, not from +0x60.

# M3 -- CORRECTION (measurement 6)

My first write-up said the brief's last link does not hold. That was wrong, and
the instrument that refutes it is a DMA watch on the object itself.

    PS3BASE_DMA_RANGE=0177cc00-0177cd00   (log /tmp/M3-dma.log, 75 s budget)

    $ grep -a dmaw /tmp/M3-dma.log | head -3
    I   2.814124 dmaw: thread 0x200 SecondaryCellSpursKernel0 cmd 0x40 ea 0x0177cc00 lsa 0x36200 size 0x90 pc 0x076c4
    I   2.824774 dmaw: thread 0x200 SecondaryCellSpursKernel0 cmd 0x40 ea 0x0177cc00 lsa 0x36200 size 0x90 pc 0x076c4
    I   2.835102 dmaw: thread 0x200 SecondaryCellSpursKernel0 cmd 0x40 ea 0x0177cc00 lsa 0x36200 size 0x90 pc 0x076c4

    $ grep -a dmaw /tmp/M3-dma.log | sed -E ... | sort | uniq -c
       4000 0x200 cmd=0x40 ea=0x0177cc00 lsa=0x36200 size=0x90 pc=0x076c4

The SPURS kernel DMA-reads the whole 0x0177cc00 object, 0x90 = 144 bytes, which
covers +0x60, about every 10 ms for the entire run. So the 0 at +0x60 does
reach the SPU -- through the MFC, at SPU pc 0x076c4, not through a PPU load.

And the +0x60 store's own value comes from 0x0177cda8 after all; my reading of
the writer was wrong. The host stack of the +0x60 store, symbolized:

    $ addr2line -f -e build-rdr/ps3base 0x63bda8d 0x63c2f45
    f_0091fd18_body(ppu::Context&, unsigned char*)  out-rdr/ppu/rdr_092.cpp:26358
    f_00921138_body(ppu::Context&, unsigned char*)  out-rdr/ppu/rdr_092.cpp:27433

so the guest lr 0x0177cde0 on that line is a STALE lr (a return address parked
in the object by the template copy), and the actual instruction is line 26363:

    26345  ctx.gpr[25] = ld32(base, gpr[18] + 0);     r18 = 0x0177cda8
    26363  st32(base, gpr[30] + 96, gpr[25]);        *(0x0177cc00 + 0x60) = *(0x0177cda8)

So +0x60 is a copy of the global 0x0177cda8. The reason it is 0 is the count
gate, exactly as the brief says:

    0x00920048  cmpwi  cr7,r0,0            r0 = *(0x0177caa8) = 1 at this moment
    0x00920054  bc     4,30 -> 0x009206f4  taken when CR7.EQ is 0, i.e. count != 0
    0x00920058  lwz    r18,-32116(r2)      0x0177cda8
    0x00920064  stw    r9,0(r18)           skipped

    skip target 0x9206f4: lwz r18,-32116(r2); goto L_920254 -- and L_920254
    is where line 26363 reads gpr[18], so the read happens on BOTH paths.

0x0177cda8 is written ZERO times in the run:

    $ grep -ac 'WRITE.*0177cda8' /tmp/M3-bw.log
    0

and the count word the gate tests is incremented by the same function for every
enabled DSP feature bit before the test (rdr_092.cpp 26039-26079: flags 0x10,
0x20, 0x40 each do ld32(r22); +1; st32(r22), r22 = 0x0177caa8):

    I   3.512370 WRITE +0x0a8 (0x0177caa8) w4 prev 0x00000000 -> 0x00000000 | lr 0x00609a40
    I   3.568175 WRITE +0x0a8 (0x0177caa8) w4 prev 0x00000000 -> 0x00000001 | lr 0x00609a40
    I   3.664965 WRITE +0x0a8 (0x0177caa8) w4 prev 0x00000001 -> 0x00000000 | lr 0x0091fd98
    I   3.712710 WRITE +0x0a8 (0x0177caa8) w4 prev 0x00000000 -> 0x00000001 | lr 0x0091fd98
    I   3.771893 WRITE +0x0a8 (0x0177caa8) w4 prev 0x00000001 -> 0x00000000 | lr 0x0091fdb0
    I   3.871673 WRITE +0x0a8 (0x0177caa8) w4 prev 0x00000000 -> 0x00000001 | lr 0x0091fdb0

The counter is left at 1 when __cellMSSystemInitBasic reaches the gate, so the
gate skips, so 0x0177cda8 stays 0, so +0x60 = 0, so the SPU reads 0 from the
object it DMAs and computes ea 0.

My earlier sentence '0x0177cda8 is not object+0x60' was right about the address
arithmetic (0x0177cda8 - 0x0177cc00 = 0x1a8) but wrong about the consequence:
0x0177cda8 is a separate global whose CONTENT is copied into object+0x60.
**The brief's chain stands**, with one field corrected: the audio port address
is at +0x58 and the DSP-size field at +0x60 -- they are different fields and
both matter, one to the mixer and one to the DMA.

## Corrected conclusion

1. cellMS* identification: as in Measurement 1.
2. Configuration with the audio port: f_0091f28c, called with
   r4 = &cfg right after cellAudioGetPortConfig(portNum, &cfg); stores
   cfg+0 -> 0x0177ca8c, cfg+16 -> 0x0177ca90, cfg+28 (port addr 0x12190000)
   -> 0x0177ca94.
3. Mixer start on SPURS: f_0060a6e8 -> cellAudioPortStart(0) ->
   cellMSSystemSignalSPU, which copies the port address to object+0x58; the
   per-stream start loop is f_00926560.
4. **+0x60 is NOT the audio port address** -- the port address is +0x58. +0x60
   is a copy of the global 0x0177cda8, written by __cellMSSystemInitBasic
   itself, and read by the SPU kernel via a 0x90-byte MFC GET at SPU pc 0x076c4.
   So the brief's answer to its own question is: no, +0x60 does not come from
   cellAudioGetPortConfig, but it does explain the DMA to ea 0 -- as the
   skipped size field, not as a lost port address.
5. The remaining defect is the count gate, not the audio port: 0x40 of the
   descriptor word 0x10ca makes the counter 1 before the test at 0x920048, and
   the store to 0x0177cda8 is skipped. Whether the console's library does the
   same with those descriptor fields is the open question; the runtime's own
   descriptor is built at rdr_061.cpp 5351-5356 as {mode, 18, 5, 0x10ca}.

## Measurement 7 -- the MFC history at the refusal (SPU side, verbatim)

The refusing command at SPU pc 0x06428 is preceded by two channel writes and a
channel read at 0x641c-0x6428, all in the lifted SPU image (spu_015.cpp):

    $ sed -n '6040,6050p' out-rdr/spu/spu_015.cpp
    c.pc = 0x6420;
    if (!op_wrch(c, h, 0x21a00b58u, 0x641cu)) return false;
    if (c.pc != 0x6420) return true;
    c.pc = 0x6424;
    if (!op_wrch(c, h, 0x21a00bd3u, 0x6420u)) return false;
    if (c.pc != 0x6424) return true;
    c.pc = 0x6428;
    if (!op_rdch(c, h, 0x01a00c04u, 0x6424u)) return false;
    op_lqd(c, h, 0x34070080u, 0x6428u);

so the ea comes from whatever the two wrch at 0x641c/0x6420 pushed into the
MFC parameter channels; the runtime keeps a ring of the last 8 channel writes
(SpuThread::mfc_hist) precisely for this, but the refusal log does not print it
today. That is the one instrument the next step needs: it would name the
register and the local-store address the 0 was read from, and close the SPU
half of the chain the same way the PPU half is now closed.

## What is answered, and what is left

Answered:

- the cellMS* function map (Measurement 1);
- that configuration takes the audio port from cellAudioGetPortConfig and
  stores its port address at 0x0177ca94, then into object+0x58 (Measurement 3);
- that +0x60 is NOT the port address -- it is a copy of the global
  0x0177cda8, a DSP size/offset field, written by __cellMSSystemInitBasic
  (Measurement 6);
- that the SPU kernel reads the whole object including +0x60 by a 0x90-byte
  MFC GET at SPU pc 0x076c4 (Measurement 6);
- that the gate at 0x920054 skips the store to 0x0177cda8 because the counter
  0x0177caa8 is 1 from descriptor flag 0x40 (Measurements 3, 6).

Left open, and it is now the only open link: the SPU's ea 0 at pc 0x06428
comes from the two wrch at 0x641c/0x6420, whose arguments are not logged. Either
the SPU loaded +0x60 and used it as an ea, or it used something else; one dump
of mfc_hist at the refusal decides it. Everything on the PPU side is now
measured, and the brief's chain is confirmed up to and including the +0x60 = 0
step.

- M3
