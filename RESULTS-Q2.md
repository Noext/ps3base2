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


## Measurement 4 -- what `tools/check_lle_exports.py` actually checked, and why 47/47 was false

### It checked the wrong denominator

libfiber's own libent chain declares 50 function exports (47 named + 3 unnamed
PRX-entry stubs), and its .opd names 87 code addresses. The tool reported
`cellFiber 47 exports`. It counted the named function exports only:

```
function exports: 50 total = 47 named + 3 unnamed
  unnamed: ['BC9A0086->0x36005c78', 'AB779874->0x36005c80', '3AB9A95E->0x36005c80']
  of those, lifted: 50 of 50

OPD-named code addresses: 87
  of which are function exports: 49
  of which are NOT exports (internal, .opd-only): 38
  of the internal ones, lifted: 38 of 38
```

So 47/47 was true and useless: it was 47 of the 87 functions the module's own
descriptor table names, and it never looked at the other 40. (All 87 are in fact
lifted -- see Measurement 3 -- but the tool could not have told you that.)

### The four holes, each measured with an A/B on a corrupted input

`tools/check_lle_exports.py` vs `tools/check_export_coverage.py`, same machine,
same minute, each case a single deliberate corruption:

| case | corruption | old tool | new gate |
|---|---|---|---|
| D | the unnamed PRX-entry export row removed from `exports[]` | **PASS** | FAIL |
| G | the Yield descriptor's code word set to 0x00010001 (outside every image text) | **PASS** | FAIL |
| A | `{0x360033dc, f_360033dc}` removed from `functions[]` | FAIL | FAIL |
| B | Yield's table opd replaced by 0 | FAIL | FAIL |
| C | Yield's table opd moved to another .opd slot | FAIL | FAIL |
| E | Yield's table opd pointed at a data address | FAIL | FAIL |

Case G is the decisive one, and it is the exact shape of the RDR failure. The
old tool's rule was:

```python
in_text = any(lo <= code < hi for lo, hi in ranges)
if in_text and code not in funcs:   # <- a code word OUTSIDE text is never checked
```

so a descriptor whose first word is `0x00010001` -- a data value, or zero, or
anything not inside an image's text -- was classified 'this export is data' and
counted as `ok`. The runtime does not classify:

```
runtime/src/ppu_runtime.cpp:845-847
  const image::ExportEntry& e = image::exports[st];
  uint32_t code = ld32(base, e.opd);
  ...
  call_indirect(ctx, base, code);
```

It calls `ld32(e.opd)` unconditionally. A descriptor pointing at 0x00010001 is a
call to 0x00010001. Measured:

```
G: the Yield descriptor code word set to 0x00010001 (outside every image text)
  OLD check_lle_exports.py exit 0
     cellFiber       47 exports: 47 ok, 0 missing, 0 zero-opd, 0 mismatched, 0 no-code
     PASS
  NEW check_export_coverage.py exit 1
     libfiber       CODE-NOT-IN-TEXT   nid=0C44F441   module=cellFiber      opd=0x3600e930 code=0x00010001
```

Case D is the second hole: the tool's regex was
`\{"([^"]+)", ...` -- one-or-more characters for the module name -- so it never
parsed an unnamed export row at all, and then skipped the rows it could not have
parsed with `if not m["module"]: continue`. Deleting the PRX entry export from
the table was invisible to it.

### And the 954 figure

RESULTS.md:689 records `total functions 77026 imports 494 exports 954` for the
12-module recipe. The 12 module lines the checker printed sum to 1196, not 954,
because the checker's line counts every entry of each module it was given
(including the 22+63+137+... named ones) while 954 was the table's total export
row count for a different tree. The two numbers were never the same quantity and
nothing compared them.


## Measurement 5 -- the lift's true extent, proved by tiling

Two independent measures agree that libfiber's lifted region ends exactly where
its code ends, and that nothing above it is code.

**(a) The generated source labels tile the code region.** Every basic block the
lifter emitted is a `L_xxxxxxxx:` label. In `out-rdr/ppu/libfiber_000.cpp`:

```
labels in libfiber text: 530, lowest 0x36000000, highest 0x36005c80
first import stub: 0x36005c88
highest label to first stub: 0x36005c80..0x36005c88 = 8 bytes
  that gap is: 38600000 4e800020        <- li r3,0 ; blr  (the last function's tail)
stub region: 0x36005c88..0x36005da8 = 288 bytes = 9 trampolines x 32 bytes
```

530 labels, 89 of them function entries and 441 in-function blocks. Every direct
branch target inside the text lands on one of them:

```
$ # walk every b/bc in the text, resolve the target, look it up in the label set
direct branch targets inside libfiber text: 962
targets with no label and no stub (i.e. not in the lifted source): 0
```

962 of 962. There is no branch in libfiber that jumps to an address the lift did
not produce.

**(b) The .opd covers every function the module exports, and the lift covers the
.opd.** 87 descriptors name code in the text; 87 are lifted; the two extra lifted
entries (0x3600026c, 0x360002a0) are the ELFv1 fiber-entry thunks, which the
.opd does not name but a call can reach.

What is above 0x36005c88, byte for byte:

| range | what |
|---|---|
| 0x36005c78..0x36005c88 | `li r3,0; blr` x2 -- the last two leaf functions, both lifted |
| 0x36005c88..0x36005da8 | 9 import trampolines, one per PRX import stub, deliberately not lifted |
| 0x36005da8..0x36005db4 | zero padding |
| 0x36005db4..0x36005dec | the two libent export-table entries |
| 0x36005dec..0x36006558 | export name/NID/address tables and .rodata (`libfiber`, `fiber_ppu.c`, `*NO NAME*`) |
| 0x36006560..0x3600eb50 | the second PT_LOAD: .opd (0x3600e7bc..0x3600eb50) and the pointer table |

The trampolines are not a lifter gap: `recomp/ppu/analysis.py:604` models a
branch into one as `ppu::call_import(ctx, base, N)` (`if t in img.stubs`), which
is what the runtime wants -- lifting them as guest functions would run the
guest's own stub code against a host-side import table that is not there.

So: the lift is complete for this module. The fibre crash was not a missing
lift. It was the LLE implementation of a scheduler being driven by a title that
calls `Yield` 426 077 times before it ever creates a fibre, and the fix that
landed (HLE cellFiber) addresses the behaviour, not a coverage hole.

### The one thing the lift genuinely does not cover

`0x3600e930` is not code and never was. It is the .opd slot of
`cellFiberPpuYield`; the code it points at, 0x360033dc, is lifted and runs.
Reading a .opd address and asking 'is it inside the lifted text' is a category
error, and it is the error the finding document made when it wrote
'le lift de libfiber s'arrete a 0x36005c80'. 0x36005c80 is the highest address
the .opd names; it is not where the lift stopped.

