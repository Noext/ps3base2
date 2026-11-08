# SPU: from interpreted to ahead-of-time compiled

## Baseline, measured before any change

One session, same disc, same VFS seed, `GOWA_SPU_JOBS=1`:

```
spu: 20263 compiled SPU blocks
spujob: interpreted 74.55% of 226552351 insns,
        hot interpreted block starts:
        0c254:480256 0c210:480256 0c1d0:480256 131f8:478380
        13178:478380 0f3f4:478380 0f464:478380 0f460:478380...`
```

**74.55% of SPU instructions were interpreted.** The hot starts were clustered on
a handful of LSAs, each hit hundreds of thousands of times.

## Why those blocks were interpreted

They were not in the compiled table at all. `out/spu/spu_registry.cpp` is
generated from `out/gen/blocks/run_manifest.bin`, which is built from the
title ELF and the firmware modules. Blocks that only exist in *guest memory at
run time* -- SPU code uploaded by the title itself, which is most of a SPURS
workload -- are not in that manifest, so no compiled copy exists and the
interpreter runs them every time.

The runtime already had the two halves needed to close this:

- `spu::aot::record()`, behind `GOWA_SPU_RECORD=<path>`, hashes each interpreted
  block (LSA + word count + contents) and appends `{lsa, n, words...}` to the
  file, once per distinct hash;
- `recomp/spu/codegen.py <out_dir> <blocks.bin>...` compiles exactly that format
  into the AOT table the dispatcher consults before the interpreter.

## The loop

1. Run with `GOWA_SPU_RECORD=/tmp/spu-caps/run0.bin` and drive to a world.
   One session captured **1199 distinct blocks**.
2. Regenerate the table from the manifest **and** the capture:
   ```
   recomp/spu/codegen.py out/spu out/gen/blocks/run_manifest.bin /tmp/spu-caps/run0.bin
   ```
   This produced **21449 blocks** (up from 20263) and 147 generated sources.
3. Rebuild and re-measure.

## Result

```
spu: 21449 compiled SPU blocks
spujob: interpreted 0.00% of 231204497 insns, hot interpreted block starts: (empty)
spujob: region share 89.3%, 60.1 insns per dispatch
```

**0.00% interpreted, down from 74.55%.** The hot list is empty because nothing
falls through to the interpreter any more. Measured on the merged tree, with
the NP fixes, no forcing of any kind:

- `sys_vm_memory_map ->` present (a world was really created),
- 0 crashes,
- 60 fps at the front end (4201 flips in one session, stable),
- 231 million SPU instructions in the sampling window.

## Where the baseline number came from

Both numbers are the runtime's own counter, printed by the same code path
(`g_interpreted_insns` vs `g_aot_insns` in `runtime/src/spu/thread.cpp`), so the
comparison is like for like.

## Remaining work

The capture is a snapshot of one session at one seed. A different seed, a
different world, or a longer session can upload blocks this capture never saw;
each of those would show up again as a non-zero interpreted share. The loop
above is the mechanism to close them, and it is now cheap: record, regenerate,
rebuild. Repeating it to convergence is the remaining work.
