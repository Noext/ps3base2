# RESULTS-Q2 -- the export coverage gate

Branch explore/q2, worktree /root/Dev/wt-Q2, from main HEAD d2269e5.

## Task

1. Build-time gate: for every LLE module, every exported function
   (export table -> descriptor -> code address) must have a lifted function;
   otherwise the build FAILS listing the missing ones.
2. Run it on all modules of all three games; give the table.
3. Prove why 47/47 exports verified was false -- what
   tools/check_lle_exports.py actually checked (it reported PASS on 12
   modules and 954 exports).
4. Prove why the libfiber lift stopped at 0x36005c80.

## Log

- 2026-11-13T12:25Z worktree created from main d2269e5, claim + status written.

## Measurement 1 -- the libfiber lift does NOT stop before Yield

`out-rdr/ppu/funcs.h` and `table.cpp` for libfiber (image 7, text 0x36000000..0x36006558):

```
$ grep -o 'f_360[0-9a-f]*' /root/Dev/ps3base2/out-rdr/ppu/funcs.h | sed 's/f_//' | python3 -c "..."
count 89 min 0x36000000 max 0x36005c80
entries >= 0x36005c80: 1
0x360033dc present: True
```

What the import table points at (out-rdr/ppu/table.cpp:78072):

```
{"cellFiber", 0x0C44F441u, "cellFiberPpuYield", 7, 0x3600e930}
```

0x3600e930 is the OPD (a 8-byte descriptor in .opd), not code. The descriptor is
`360033dc 36016a80` -- code 0x360033dc, TOC 0x36016a80. And 0x360033dc IS lifted:

```
funcs.h:76714:void f_360033dc(ppu::Context& ctx, uint8_t* base);
table.cpp:76760:    {0x360033dc, f_360033dc},
```

So the claim 'cellFiberPpuYield was never lifted' is false. The docs' own crash
stack says the same thing: RESULTS-P1.md:151 frame #2 is
`f_360033dc_body+0x1bc (= cellFiberPpuYield, 0x3600345c)` -- a lifted function
that is executing when the fault happens.


## Measurement 2 -- the export coverage gate, and the table on all three games

`tools/check_export_coverage.py` (new). It walks each PRX's own libent chain
(num_func first, so the unnamed module_start/module_stop/PRX-entry exports are
included), looks each (module, NID, image) up in the generated `exports[]`, and
requires: row present; table opd non-zero and equal to the PRX's own opd; the
descriptor's first word inside the image text and an entry in `functions[]`;
the descriptor's second word equal to the image TOC.

```
$ for g in rdr mc gta; do python3 tools/check_export_coverage.py \
    /root/Dev/ps3base2/out-$g/ppu/table.cpp --root /root/Dev/ps3base2 --imports; done
```

| game | module | func exports | lifted | miss | rowmiss | badopd | badtoc | own imports dead |
|---|---|---|---|---|---|---|---|---|
| rdr | liblv2 | 163 | 163 | 0 | 0 | 0 | 0 | 0 |
| rdr | libsysmodule | 20 | 20 | 0 | 0 | 0 | 0 | 0 |
| rdr | libsre | 267 | 267 | 0 | 0 | 0 | 0 | 0 |
| rdr | libgcm_sys | 105 | 105 | 0 | 0 | 0 | 0 | 0 |
| rdr | libfs | 96 | 96 | 0 | 0 | 0 | 0 | 0 |
| rdr | libnet | 103 | 103 | 0 | 0 | 0 | 0 | 0 |
| rdr | libfiber | 50 | 50 | 0 | 0 | 0 | 0 | 0 |
| rdr | librtc | 40 | 40 | 0 | 0 | 0 | 0 | 0 |
| mc | liblv2 | 163 | 163 | 0 | 0 | 0 | 0 | 0 |
| mc | libsysmodule | 20 | 20 | 0 | 0 | 0 | 0 | 0 |
| mc | libsre | 267 | 267 | 0 | 0 | 0 | 0 | 0 |
| mc | libgcm_sys | 105 | 105 | 0 | 0 | 0 | 0 | 0 |
| mc | libfs | 96 | 96 | 0 | 0 | 0 | 0 | 0 |
| mc | libvdec | 24 | 24 | 0 | 0 | 0 | 0 | **91** |
| mc | libsmvd2 | 23 | 23 | 0 | 0 | 0 | 0 | 0 |
| mc | libspurs_jq | 66 | 66 | 0 | 0 | 0 | 0 | 0 |
| gta | liblv2 | 163 | 163 | 0 | 0 | 0 | 0 | 0 |
| gta | libsysmodule | 20 | 20 | 0 | 0 | 0 | 0 | 0 |
| gta | libsre | 267 | 267 | 0 | 0 | 0 | 0 | 0 |
| gta | libgcm_sys | 105 | 105 | 0 | 0 | 0 | 0 | 0 |
| gta | libfs | 96 | 96 | 0 | 0 | 0 | 0 | 0 |
| gta | libvdec | 24 | 24 | 0 | 0 | 0 | 0 | **91** |
| gta | libsmvd2 | 23 | 23 | 0 | 0 | 0 | 0 | 0 |
| gta | libspurs_jq | 66 | 66 | 0 | 0 | 0 | 0 | 0 |

Totals: RDR 844 function exports / 844 lifted; MC 764/764; GTA 764/764.
Every descriptor's code word is a lifted function entry and every TOC matches.

The one real finding is **libvdec**: all 24 of its own exports are lifted, but
91 of its own imports resolve nowhere -- `libavcdec`, `libdivx311dec`,
`libdivxdec` are not lifted into the MC/GTA tables, so the first video call
goes to a null slot. That is exactly the class of fault the export-only check
cannot see, and it is why the gate has a dependency half.

### The gate fails on a broken table (A/B, same machine, same minute)

Four corrupted copies of `out-rdr/ppu/table.cpp`, each exercising one hole the
old tool had. `tools/check_lle_exports.py` vs `tools/check_export_coverage.py`:

| case | old tool | new gate |
|---|---|---|
| A: `{0x360033dc, f_360033dc}` removed from `functions[]` | FAIL (caught) | FAIL (caught) |
| B: Yield's table opd replaced by 0 | FAIL (caught) | FAIL (caught) |
| C: Yield's table opd moved to 0x3600e938 | FAIL (caught) | FAIL (caught) |
| D: the unnamed PRX-entry export row removed | **PASS -- missed** | FAIL (caught) |

Case D is the structural blind spot: `check_lle_exports.py` never even parses
the unnamed rows (its regex requires a non-empty module name, `[^\"]+`), and
then skips them explicitly with `if not m["module"]: continue`.


## Measurement 3 -- why the libfiber lift 'stopped' at 0x36005c80

It did not stop. 0x36005c80 is the highest function the module's own .opd
names, and the lift covers every one of them. The claim came from comparing two
different segments' addresses.

libfiber's two PT_LOADs (`out-rdr/gen/reloc/libfiber.elf`):

```
LOAD vaddr 0x36000000 filesz 0x6558 memsz 0x10000 flags 0x400005  <- R+X  text
LOAD vaddr 0x36006560 filesz 0x85f0 memsz 0x85f8  flags 0x600006  <- R+W  .opd/.rodata
```

Every descriptor in the .opd run that names libfiber code (`code` in text,
`toc` == 0x36016a80):

```
OPD descriptors naming libfiber code: 87
unique code addresses: 87
lowest  0x36000000
highest 0x36005c80
lifted entries in libfiber: 89, highest 0x36005c80
OPD code addresses NOT lifted: none
lifted entries NOT in the OPD: ['0x3600026c', '0x360002a0']
```

87 OPD-named functions, all 87 lifted, plus two island entries the .opd does not
name (0x3600026c and 0x360002a0, the ELFv1 fiber-entry thunk -- the very
function in the crash backtrace). 89 total, which is the `functions 89` line the
lift prints for libfiber. **The lift is complete with respect to the module's
own export descriptors.**

What sits above 0x36005c80 is not functions. The last real function ends at
0x36005c84 (`li r3,0; blr`); from 0x36005c88 the text is nine 8-instruction
import trampolines, one per import stub the PRX declares:

```
  0x36005c88: 39800000  li   r12,0          <- cellLibprof stub 0
  0x36005c8c: 658c3600  oris r12,r12,0x3600
  0x36005c90: 818c6560  lwz  r12,0x6560(r12)
  0x36005c94: f8410028  std  r2,0x28(r1)
  0x36005c98: 800c0000  lwz  r0,0(r12)
  0x36005c9c: 804c0004  lwz  r2,4(r12)
  0x36005ca0: 7c0903a6  mtctr r0
  0x36005ca4: 4e800420  bctr
```

with the pointer table at 0x36006560 holding 0x36005c88, 0x36005ca8, ...
0x36005d88 for the nine stubs. The lifter models a branch into one of these as
`ppu::call_import` (recomp/ppu/analysis.py:604, `if t in img.stubs`), so they are
deliberately not lifted as functions. Above them: .opd at 0x36005db4, the export
name/NID tables, and .rodata. No code.

### The address that was compared against it is in the other segment

```
  0x3600e930 is in LOAD 0x36000000 + 0xe930 -> CODE (executable)
  0x3600e930 is in LOAD 0x36006560 + 0x83d0 -> DATA (.opd)
  its descriptor: code 0x360033dc toc 0x36016a80
  code 0x360033dc lifted: True
```

`0x3600e930` is the **.opd slot** of cellFiberPpuYield (table.cpp:78072), i.e.
a data address in the second LOAD. The first LOAD's `memsz` (0x10000) also
covers it, but only as zero-fill: its `filesz` is 0x6558. Reading it as a code
address and asking 'is this inside the lifted text range 0x36000000..0x36006558'
compares a .opd address with a text bound. The descriptor at that slot says the
code is 0x360033dc, and 0x360033dc is lifted -- `funcs.h:76714`, `table.cpp:76760`.

### What the runtime does with it

`runtime/src/ppu_runtime.cpp:845-847` (the LLE arm of call_import_inner):

```
const image::ExportEntry& e = image::exports[st];
uint32_t code = ld32(base, e.opd);
ctx.gpr[2]  = ld32(base, e.opd + 4);
...
call_indirect(ctx, base, code);
```

e.opd = 0x3600e930 -> code = 0x360033dc -> `lookup(0x360033dc)` = f_360033dc.
The lifted Yield runs. The docs' own crash backtrace says the same: RESULTS-P1.md
frame #2 is `f_360033dc_body+0x1bc (= cellFiberPpuYield, 0x3600345c)`.

