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

