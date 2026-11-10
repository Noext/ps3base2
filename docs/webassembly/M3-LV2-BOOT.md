# The LV2 boot under wasm: reached, and the one flag left

## What now works, measured

    loader: loaded mc (bin/mc/mc.elf) text 0x00010200-0x008aa674 toc 0x00bd7998 entry 0x00bbe520
    loader: loaded liblv2 (fw/4.91/reloc/liblv2.elf) text 0x01000000-0x01013b68
    WASM-BOOT-IMAGES-LOADED
    ps3base-wasm: sdk 0x450001 prio 1001 stack 0x10000 entry 0xbbe520
    stacks: thread 16777216 'main_thread' requested 0x10000 allocated 0x10000
            range 0x60010000..0x60020000 guard 0x60000000..0x60010000 (adjacent)
    ps3base-wasm: CRT args sp=0x6001ff40 argv=0x6001ffc0 envp=0x6001ffb0
    ps3base-wasm: starting the LV2 entry...

**The real game executable and the real LV2 module load inside WebAssembly.** The
process info is parsed (`sdk 0x450001`, `prio 1001`, `entry 0xbbe520`), the main
guest thread is created with its stack and its **guard page adjacent below it**,
and the CRT argv is built on that stack exactly as the native boot does.

That is 13.6 MB of game data read through the provider seam and parsed in the
browser's engine, which is the part that had never been exercised.

## The one thing left

    ps3base-wasm: guest start threw: thread constructor failed: Not supported

`guest::start` runs the guest on a `std::thread`. Without pthreads, Emscripten
refuses to construct one.

Adding `-pthread` gets as far as:

    wasm-ld: error: --shared-memory is disallowed by cbwatch.o because it was not
            compiled with 'atomics' or 'bulk-memory'

**Pthreads require every object to be recompiled with `-matomics -mbulk-memory`.**
That is 303 objects (98 PPU, 170 SPU, 35 runtime) — a mechanical rebuild, not a
code change, and the exact next step.

## Correcting the previous round's claim again

Two rounds ago I called milestone 3 complete. It was complete for its build
criterion and for "lifted code executes". It was **not** complete for the boot:
the images did not load, no guest thread was created, and no CRT argv existed.
Those three now exist and are proven. What remains is a thread system, and the
prompt anticipated exactly this ("if the pool does not suffice, apply the
unwind-on-switch technique ... document").

## Why the flag matters beyond a rebuild

`-pthread` implies shared memory, which in a browser requires the page to be
cross-origin isolated (`COOP`/`COEP` headers). That is a page-layer constraint the
milestone 4 work must carry, and it is worth knowing now rather than discovering
when the page is written.
