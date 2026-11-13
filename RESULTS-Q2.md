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

