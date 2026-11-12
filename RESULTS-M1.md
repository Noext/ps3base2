# M1 -- the 0x920054 branch, down to the leaf

**Answer, one line: the `0x40` is not written by anyone at run time. It is bit 6 of
the literal `li r6, 0x10ca` at `0x00609a28`, the ONLY occurrence of that
immediate in the whole 89 MB executable segment; f_0091f364 turns it into
`count = 1` with pure arithmetic on the descriptor, so the console computes the
same 1, takes the same `bne` at 0x920054, and skips the same store -- the skip is
CORRECT and the defect is downstream, in the value the SPU consumes.**

---

## 1. Every call site, by direct-branch scan of the image

`tools/ms_descriptor_sites.py bin/rdr/EBOOT.elf`:

```
cellMSSystemGetNeededMemorySize f_0091f364 callers:
  bl at 0x00609a3c -> return address 0x00609a40
    stw r31, 0xb0(r1) at 0x00609a24
    stw r4, 0xb4(r1) at 0x00609a30
    stw r5, 0xb8(r1) at 0x00609a34
    stw r6, 0xbc(r1) at 0x00609a38
  bl at 0x0091fd94 -> return address 0x0091fd98
cellMSSystemInit f_0091fd18 callers:
  bl at 0x009211e8 -> return address 0x009211ec

every instruction forming the immediate 0x10ca in the executable segment:
  0x00609a28 op14 0x38c010ca
```

Two callers of the memory-size function, and one is the init function calling it
on its own argument:

    f_006099e8 --- bl 0x91f364 @0x609a3c --- descriptor {numStreams, 0x12, 5, 0x10ca}
    f_0091fd18 --- bl 0x91f364 @0x91fd94 --- descriptor = its own r4 (same object)
    f_00921138 --- bl 0x91fd18 @0x9211e8 --- the init entry point

A scan of every 4-aligned word in every image for the four code addresses found
**no function-pointer table entry** holding any of them, so these are all the
paths; there is no indirect third caller.

## 2. The descriptor is a stack object filled by four `stw`, verbatim

    0x609a1c li r4, 0x12
    0x609a20 li r5, 5
    0x609a24 stw r31, 0xb0(r1)      ; word0 = numStreams, lwz [0x1722740] at 0x609a04
    0x609a28 li r6, 0x10ca
    0x609a2c addic r3, r1, 0xb0
    0x609a30 stw r4, 0xb4(r1)       ; word1 = 0x12 (18)
    0x609a34 stw r5, 0xb8(r1)       ; word2 = 5   (dsp page size)
    0x609a38 stw r6, 0xbc(r1)       ; word3 = 0x10ca
    0x609a3c bl 0x91f364

Measured live, not inferred -- `GOWA_HOOK='91f364:d3=16'`:

    I  15.115069 hook: -> f_0091f364 #0 lr 00609a40 [PPU main_thread] r3=d004ed30 r4=12 r5=5 r6=10ca ...
    I  15.115699 hook:    r3@d004ed30: 00000060 00000012 00000005 000010ca
    I  15.115870 hook: <- f_0091f364 #0 r3=1f0aaa

r3=0x1f0aaa is the `0x001f0aaa` already established for this descriptor; word0 is
0x60 = 96 streams, so word0 is the only run-time-dependent field and word3 is
not derived from it.

## 3. Where `count = 1` comes from: three bit tests, no memory, no imports

    0x91f3a4 lwz r5, 0xc(r3)         ; r5 = word3 = 0x10ca
    0x91f3a8 li r10, 0               ; count = 0
    0x91f3b0 rlwinm r0, r5, 0, 0x1b, 0x1b   ; bit 4 (0x10)
    0x91f3b8 cmpwi cr7, r0, 0
    0x91f3bc li r0, 0x400            ; size = 0x400
    0x91f3c8 beq cr7, 0x91f3dc
    0x91f3cc li r10, 1 ; li r0, 0x80 ; stw r10,0(r11) ; stw r0,0(r9)   ; count=1, size=0x80
    0x91f3dc rlwinm r0, r5, 0, 0x1a, 0x1a   ; bit 5 (0x20)
    0x91f3e4 beq cr7, 0x91f570              ; count += 1, size = 0x100
    0x91f3fc rlwinm r0, r5, 0, 0x19, 0x19   ; bit 6 (0x40)
    0x91f404 beq cr7, 0x91f41c              ; count += 1, size = 0x200

0x10ca = 0b1_0000_1100_1010: bit4=0, bit5=0, bit6=1. One increment only.

Measured at the global the branch reads, twice per run, from both call sites
(`PS3BASE_BLOCKWATCH=177ca90-177cab0`):

    I  9.916742 blockwatch: WRITE +0x018 (0x0177caa8) prev 0x00000000 -> 0x00000000 | guest lr 0x00609a40
    I 10.013322 blockwatch: WRITE +0x014 (0x0177caa4) prev 0x00000000 -> 0x00000400 | guest lr 0x00609a40
    I 10.100120 blockwatch: WRITE +0x018 (0x0177caa8) prev 0x00000000 -> 0x00000001 | guest lr 0x00609a40
    I 10.190165 blockwatch: WRITE +0x014 (0x0177caa4) prev 0x00000400 -> 0x00000200 | guest lr 0x00609a40

0x0177caa8 = count, 0x0177caa4 = byte size. Final state after the call:
**count = 1, size = 0x200**. There is no `lwarx`/`stwcx`, no import call and no
load other than the descriptor inside f_0091f364, so this is a pure function of
word3; the console computes the same 1 from the same literal.

## 4. What the branch actually skips -- and it is not +0x60

    0x920038 lwz  r28, -0x7d78(r2)   ; r28 = &0x0177cda4   (= block + 0x1a4)
    0x920048 cmpwi cr7, r0, 0        ; r0 = [0x0177caa8] = count = 1
    0x92004c addi r11, r9, 0x400
    0x920050 stw  r9, 0(r28)         ; [0x0177cda4] = r9      <- taken, always
    0x920054 bne  cr7, 0x9206f4      ; count != 0 -> taken
    0x920058 lwz  r18, -0x7d74(r2)   ; r18 = &0x0177cda8   (= block + 0x1a8)
    0x92005c mr   r9, r11
    0x920060 addi r11, r11, 0x4000
    0x920064 stw  r9, 0(r18)         ; [0x0177cda8] = r9      <- SKIPPED
    0x920068 ...
    0x9206f4 lwz  r18, -0x7d74(r2)   ; the taken arm reloads the same slot
    0x9206f8 b    0x920068

So the skipped slot is **block + 0x1a8**, i.e. `0x0177cda8`, not block + 0x60.
`0x0177cda8 - 0x0177cc00 = 0x1a8`. The earlier note's "+0x60 of 0x0177cc00" is the
dereference: 0x0177cda8 is a *pointer*, and the SPU reads `[ptr + 0x60]`; with
the store skipped ptr is 0 and that read is the GET at guest 0.

Two adjacent words, a ring: `[0x1a4] = base`, `[0x1a8] = base + 0x400`. In a
1-stream configuration only the first 0x400 buffer exists, so the second happens
to stay 0 -- on the console as well.

Also: block + 0x60 IS written, which is what the old note got wrong about the
block itself:

    I  20.579604 blockwatch: WRITE +0x060 (0x0177cc60) w4 prev 0x00000000 -> 0x00000000 | guest lr 0x0177cde0 (no image)

## 5. Nothing ever writes 0x0177cda8 -- measured, 70 s

`PS3BASE_BLOCKWATCH=177cc00-177d000`, run to the abort. Distinct offsets written
in the whole 0x400 line: 0x000-0x08c, 0x100-0x120, 0x180-0x1a4, 0x1ac-0x214,
0x280, 0x300-0x3f8. **+0x1a8 is absent; +0x1a4 and +0x1ac are present.**

    $ grep -a 'WRITE +0x1a8' /tmp/M1run6.log
    (nothing)

and the abort is unchanged:

    E  28.422809 spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET REFUSED cmd 0x40 (unmapped) ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428
    I  28.438501 tty: [0] The SPURS is aborted.
    I  28.438744 process: _sys_process_exit(0x8001000c)

Control run with no hooks and no watch, same machine, same minute:

    E  34.969310 spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET REFUSED cmd 0x40 (unmapped) ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428
    I  34.977208 tty: [0] The SPURS is aborted.

## 6. The answer to the three questions asked

**Who writes bit 0x40 of word3?** The title's own `li r6, 0x10ca` at 0x00609a28.
No run-time value decides it: it is not an import return and not a structure
field. It is the only occurrence of that immediate in the executable segment, so
no call site anywhere passes a descriptor without bit 0x40 in a different form --
both callers of f_0091f364 hand it the identical descriptor object.

**Which descriptor does each call site pass?** 0x609a3c passes
`{numStreams, 0x12, 5, 0x10ca}` built on its own stack frame; 0x91fd94 passes the
same object straight through from its own r4. Both were dumped live and are
byte-identical. 0x9211e8 is the only caller of the init function and supplies the
port object at 0x0177cd98 as r3.

**On the console, does the same literal produce count = 1?** Yes. f_0091f364
computes count from three `rlwinm`/`cmpwi` pairs on word3 and nothing else --
no load, no lock, no import. The same 0x10ca gives bit4=0, bit5=0, bit6=1 on any
machine, so count=1, so `bne cr7` at 0x920054 is taken, so the store to
0x0177cda8 is skipped. **The skip is correct.** The defect is that the SPU
consumes a word the title itself leaves zero in a one-stream configuration, i.e.
the next question is on the SPU side: which struct does it read, and should that
field be the 0x1a4 buffer instead.

## 7. Why this is a scan and not a reading

* `tools/ms_descriptor_sites.py bin/rdr/EBOOT.elf` -- the call sites, the four
  descriptor stores and the immediate scan above, recomputed from the ELF.
* No ctest: the tool needs `bin/rdr/EBOOT.elf`, which is gitignored derived
  material, the same reason `tests/ppu_dispatch_table.py` gives in its header for
  using synthetic words. The proof is the command and its output, and both are
  reproduced above.
* Every number here is an in-run measurement (`GOWA_HOOK`, `PS3BASE_BLOCKWATCH`)
  or a byte read from the guest image, not a transcription.
