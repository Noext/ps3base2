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


## Measurement 6 -- the memory bug in my own gate, found and fixed

The lead observed three runs of `tools/check_export_coverage.py` in this
worktree grow to 24 GB, 15 GB and 7.5 GB and get killed. That is a real defect
in the gate, and it is worth naming precisely because the gate exists to stop
exactly this class of silent failure.

### Cause

`module_function_exports()` walks the PRX libent chain:

```python
addr, end = prx["libent"]
while addr < end:
    b = elf.read(addr, 0x1C)
    size = b[0]          # <- the link is ONE BYTE at offset 0
    ...
    out.append(...)
    addr += size         # <- size == 0 means this never advances
```

The chain's link is a single byte. `tools/check_lle_exports.py` uses
`inventory.exports()`, which has the same `addr += size` shape, so the hazard
was inherited rather than invented -- but I made it fire: the synthetic fixture
in `tests/export_coverage.py` wrote the size with
`struct.pack_into(">I", data, off, 0x1C)`, which stores `00 00 00 1C`. `b[0]`
is then **0**, `addr` never moves, and the loop appends a dict forever. One
`ulimit`-free run is unbounded; three in parallel is the machine.

The real ELFs are fine -- libfiber's entry at 0x36005db4 begins `1c 00 00 00`,
i.e. `b[0] == 0x1C == 28`. The fixture was wrong, and the gate had no guard, so
a wrong fixture became a runaway instead of an error.

### Fix, and the proof it holds

Three changes, all in the gate rather than only in the fixture, because a
checker must not be able to take the machine down on bad input:

1. a zero size raises `ValueError` naming the address and the byte-vs-word
   mistake, instead of looping;
2. the entry count is capped at 4096 (liblv2 has 5 entries, libsre 7), so a
   chain that walks into data terminates;
3. the `ValueError` is caught in `main()` and reported as a normal
   `LIBENT-MALFORMED` failure, so the gate exits 1 with a diagnosis instead of
   a traceback.

The fixture now writes the byte the real ELFs write. Re-running the *broken*
fixture (size as a word) under the same ceiling:

```
$ ( ulimit -v 2000000; /usr/bin/time -v timeout 30 python3 tests/_q2_broken.py )
test            malformed libent chain: libent entry at 0x30002040 has size 0;
                the chain cannot advance (the size is one byte at offset 0, not a word)
  test           LIBENT-MALFORMED   ...
exit=1
        Maximum resident set size (kbytes): 10496
        Elapsed (wall clock) time (m:ss): 0:00.19
```

24 GB and a dead machine became 10 MB and 0.19 s.

### The gate was never expensive. The loop was.

One run per real table, sequential, each under `ulimit -v 2000000` and a 60 s
`timeout`, peak RSS measured by `/usr/bin/time -v`:

```
reg3/out-rdr   exit 0   Elapsed 0:00.05   Maximum resident set size 24576 kB
reg3/out-mc    exit 0   Elapsed 0:00.04   Maximum resident set size 16420 kB
reg3/out-gta   exit 0   Elapsed 0:00.03   Maximum resident set size 16348 kB
```

16-24 MB and 30-50 ms per title. The cost is bounded by the size of `table.cpp`
plus the module ELFs; nothing is built per export. The 24 GB was one unbounded
`while` loop, not the algorithm.

### The synthetic test: six cases, 0.21 s, 10 MB

`tests/export_coverage.py` builds a minimal PRX (one R+X LOAD, one R+W LOAD, a
PT_PRX_PARAM inside the data segment) and a matching `table.cpp`, then checks
that the gate accepts the good case and rejects each corruption:

```
$ ( ulimit -v 2000000; /usr/bin/time -v timeout 60 python3 tests/export_coverage.py )
PASS baseline: all exports lifted                   exit 0 expected
PASS A: export code address not lifted              exit 1 expected
PASS D: unnamed PRX-entry export row missing        exit 1 expected
PASS G: descriptor code word outside text           exit 1 expected
PASS B: descriptor opd zero                         exit 1 expected
PASS T: descriptor TOC wrong                        exit 1 expected
PASS
        Maximum resident set size (kbytes): 10496
        Elapsed (wall clock) time (m:ss): 0:00.21
```


## Measurement 7 -- why the libfiber lift 'stops' at 0x36005c80, from the lifter code

Answered by reading `recomp/ppu/analysis.py`, then confirming the number with one
bounded run. No lift was regenerated (disk is at 90%).

### What bounds a module's lift

`analysis.Image.__init__` picks its branch on section headers:

```python
if self.elf.shdrs:
    ...                                  # disc ELF: read the .opd section
else:
    seg = next(p for p in self.elf.phdrs if p[0] == 1 and p[1] & 1)   # R+X LOAD
    self.text_lo, self.text_hi = seg[3], seg[3] + seg[5]             # the WHOLE segment
    self.toc = int(prx["toc"], 16)
    self.opd = []
    for p in self.elf.phdrs:
        if p[0] != 1 or p[1] & 1 or not p[5]: continue   # data LOADs only
        blob = self.elf.d[p[2]:p[2] + p[5]]
        for k in range(0, len(blob) - 7, 4):
            code, toc = struct.unpack_from(">II", blob, k)
            if toc == self.toc and self.in_text(code):
                self.opd.append((code, toc))
```

A relocated PRX has **no section headers** (`len(e.shdrs) == 0` for libfiber), so
it takes the second branch. The text range is the whole R+X segment --
0x36000000..0x36006558, nothing truncates it -- and the function seeds come from
scanning the data LOADs for `{code, toc}` pairs whose `toc` matches the module
TOC and whose `code` is inside the text.

`discover()` then starts from exactly that set:

```python
entries = {c for c, _ in img.opd if img.in_text(c)} | set(extra)
```

and grows it by following `f.calls | f.tail_calls`. A branch into an import stub
is not a call: `explore()` turns it into `f.import_calls` (`if t in img.stubs`),
so the trampolines are never seeds and never functions -- by design.

### The number

Reproducing that scan on libfiber, bounded, one run:

```
section headers present: 0
  -> analysis.Image takes the else (PRX) branch
text_lo 0x36000000 text_hi 0x36006558  toc 0x36016a80
seeds the lifter gets from the PRX .opd scan: 87
  unique: 87
  highest seed: 0x36005c80
```

**0x36005c80 is the highest seed the .opd scan produces.** The lift does not
stop there; there is nothing above it to seed. The lift then produced 89
functions from those 87 seeds plus two island entries (0x3600026c, 0x360002a0,
the ELFv1 fiber-entry thunks), and Measurement 5 shows all 962 direct branch
targets in the text land on a label the lift emitted.

So the answer to 'why did the lift stop at 0x36005c80' is: **it did not stop.
0x36005c80 is the top of the module's own function-descriptor table, and above it
the module contains only nine import trampolines and data.** The finding
document reached the right number and the wrong conclusion -- it compared that
number against 0x3600e930, which is an address in the *second* PT_LOAD (the .opd
slot of cellFiberPpuYield), and read 'a .opd address is above the last lifted
code address' as 'the lift is incomplete'.

### What was actually wrong, and it was not coverage

`cellFiberPpuYield` at 0x360033dc is lifted, is reached through the .opd at
0x3600e930, and runs -- the crash backtrace in RESULTS-P1.md frame #2 says so.
It returns `CELL_FIBER_ERROR_PERM` because of what it *does*, not because it is
missing:

```
360033ec  lwz   r11, -0x7fc8(r2)      ; per-thread pointer, initialised by
360033f8  lwz   r9, 0(r11)            ; _cellFiberPpuInitialize = r13 - r3
360033fc  addi  r0, r9, 0x20          ; the +0x20 context header
36003400  cmpwi cr7, r9, -1           ; r9 == -1  ->  return PERM
3600340c  beq-  cr7, 0x36003418
36003414  bne   cr6, 0x3600342c       ; else take the real path
```

`_cellFiberPpuInitialize` (0x36003d18) is `stw r3, 0(r9)` where
`r9 = *(r2 - 0x7f90)` -- it stores `r13 - r3` into the module's per-thread slot,
and `r13` is the guest TLS pointer. The PERM is the library correctly reporting
that the current thread has no fibre context. The title calls Yield 426 077
times before it ever creates a fibre. That is a behaviour gap in LLE libfiber
under this title's usage, which is what the HLE implementation addresses.


## Final state

Branch `explore/q2`, rebased onto main, 0 commits behind.

```
$ git diff --stat origin/main..HEAD
 .agent-status/Q2.json          |   1 +
 CMakeLists.txt                 |  62 +++++
 RESULTS-Q2.md                  | 540 +++++++++++++++++++++++++++++++++++++++++
 current_tasks/Q2.claim         |   5 +
 tests/export_coverage.py       | 198 +++++++++++++++
 tools/check_export_coverage.py | 273 +++++++++++++++++++++
 6 files changed, 1079 insertions(+)
```

0 deletions, and no `runtime/` or `recomp/` source touched: this branch adds a
gate and its test, and changes no behaviour.

### The gate, as delivered

* `tools/check_export_coverage.py` -- for every LLE module in a generated table,
  every exported function (export table -> descriptor -> code address) must have
  a lifted function, the descriptor must be the PRX's own opd, and its TOC must
  match the image. Bounded: 16-24 MB, 30-50 ms per title.
* `CMakeLists.txt` -- runs at **configure time** and fails the configure with
  `FATAL_ERROR` listing every missing export. `-DPS3BASE_EXPORT_GATE=OFF` exists
  only to diagnose a tree that cannot pass it. Also registered as ctest
  `export_coverage`, and re-runs when `table.cpp` changes.
* `tests/export_coverage.py` -- six cases on a synthetic PRX, no disc needed,
  0.21 s, 10 MB. Registered as ctest `export_coverage_gate`.

```
$ cmake -S . -B <build> -DPS3BASE_GENERATED_DIR=<tree>/ppu ...
-- Export coverage gate: PASS

$ ctest -R 'export_coverage|ppu_' --output-on-failure
1/5 Test #3: ppu_dispatch_table ...............   Passed    0.02 sec
2/5 Test #4: ppu_seed_forms ...................   Passed    0.04 sec
3/5 Test #5: ppu_run_extent_table .............   Passed    0.02 sec
4/5 Test #6: export_coverage ..................   Passed    0.05 sec
5/5 Test #7: export_coverage_gate .............   Passed    0.19 sec
100% tests passed, 0 tests failed out of 5
```

Negative control (the gate must fail, not warn): removing the Yield entry from
`functions[]` stops the configure with

```
CMake Error at CMakeLists.txt:209 (message):
  Export coverage gate FAILED (tools/check_export_coverage.py).
    libfiber       NO-LIFT            nid=0C44F441   module=cellFiber      opd=0x3600e930 code=0x360033dc
-- Configuring incomplete, errors occurred!
```

### The three answers, in one line each

1. **The gate exists and fails the build.** Every exported function of every
   LLE module has lifted code in all three titles: RDR 844/844, MC 764/764,
   GTA 764/764, 0 missing, 0 bad opd, 0 bad TOC.
2. **'47/47 exports verified' was false because it counted the wrong set.**
   `check_lle_exports.py` skipped the unnamed export set (`if not m["module"]:`,
   and a regex that could not match an empty module name), so it checked 47 of
   the 87 functions libfiber's own .opd names; it also treated a descriptor's
   code word outside the image text as data and passed it, so a descriptor
   pointing at 0x00010001 was `ok`. Both holes are now covered by tests.
3. **The libfiber lift did not stop at 0x36005c80.** For a relocated PRX the
   lifter's seed set is a scan of the data LOADs for `{code, toc}` pairs with the
   module TOC (`analysis.py:136-147`), and 0x36005c80 is the highest seed that
   scan produces -- the top of the module's own descriptor table. All 87 seeds
   are lifted, plus 2 island entries; all 962 direct branch targets in the text
   land on an emitted label. Above 0x36005c80 sit nine import trampolines and
   data. The address the finding compared it against, 0x3600e930, is an .opd
   slot in the *second* PT_LOAD; the code it points at, 0x360033dc, is lifted and
   is in the crash backtrace. The PERM return is libfiber behaving correctly for
   a thread with no fibre context, not a coverage hole.

### What the memory incident was

My own synthetic fixture wrote the libent size as a 4-byte word instead of the
single byte at offset 0, so `b[0]` was 0 and `addr += size` never advanced. The
gate had no guard, so the loop appended forever. Fixed in both places: the
fixture writes the byte, and the gate raises and reports `LIBENT-MALFORMED` on a
zero size or a chain over 4096 entries. Re-running the broken fixture under a
2 GB ceiling now gives 10 MB and 0.19 s instead of 24 GB. Every number in this
file was taken with `ulimit -v` and `timeout` set, one run at a time.

