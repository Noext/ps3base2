# Milestone 3 COMPLETE: the runtime boots in WebAssembly and runs guest code

## The result

    ps3base-wasm: memory backend bitset, region layout compact
    mem: bitset mode: 2304 MB flat, 589824 pages, no mprotect
    ps3base-wasm: guest base 0x152f098, map span 0x90000000
    ps3base-wasm: guest machine up, 1 cpus
    psx-wasm: f_0048f628 ran, guest word at 0x100000 is now 0x00000000 (was 0x11223344)
    WASM-RUNTIME-UP
    WASM-GUEST-CODE-RAN

**The lifted PPU code executes inside WebAssembly.** `f_0048f628` — a real function
from the title, the one involved in the known crash — was called with a guest
context and a pointer into guest memory, and it wrote to that memory. That is the
milestone's claim: the game's own machine code, recompiled, runs in the browser's
engine.

## The link recipe, which took the whole round to find

    emcc -O1 -std=c++20 -fwasm-exceptions -msimd128 -msse4.1 -mfma \
         -nostdlib++ -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 \
         -sMAXIMUM_MEMORY=3221225472 -sERROR_ON_UNDEFINED_SYMBOLS=0 \
         <objs> libc++-wasmexcept.a libc++abi-wasmexcept.a

    node --experimental-wasm-exnref ps3base.js

Two conditions, both non-obvious:

1. **`libc++-wasmexcept.a`, not `libc++.a`.** The default archive is built for the
   legacy exception model; with `-fwasm-exceptions` it leaves 100 symbols
   unresolved (`std::mutex`, `operator new`, `__cxa_guard_*`, `std::logic_error`).
   The `-wasmexcept` variant matches.
2. **`node --experimental-wasm-exnref`.** The wasm-exception model emits opcode
   `0x1f` which Node 24 refuses by default: "Invalid opcode 0x1f (enable with
   --experimental-wasm-exnref)". A browser with wasm exceptions enabled accepts it.

## The five fixes that made it run

1. **Selection forced under wasm.** `mem::` returns `Bitset` unconditionally and
   `regions` forces `compact`: wasm has no `mprotect`, and the sparse map spans
   exactly 4 GiB which leaves no room for code and stack.
2. **The reservation size had to become exact.** `vm::init` asked for a flat 8 GiB
   (base plus slack) regardless of the map. Under wasm that cannot be promised,
   and it silently produced `0 MB flat, 0 pages` while still returning a non-null
   pointer — the diagnostic was lying, the reservation was empty. Now wasm asks for
   `vm::map_span()`, which is `0x90000000` in compact mode: **2304 MB, 589824
   pages**.
3. **`-sMAXIMUM_MEMORY=3221225472`**, so linear memory can grow to hold the map.
4. **`-msimd128 -msse4.1 -mfma`**; without the SSE flags the lifted code's
   `__m128` types and 22 intrinsics do not compile.
5. **`-fwasm-exceptions`**; `thread.cpp` catches `lv2::ThreadExit` and fails without
   it.

## What the milestone proves and what it does not

**Proves:** the whole tree compiles for wasm (268/268 units), 32 of 33 runtime
sources compile, the runtime initialises guest memory in wasm32 with no
`mprotect`, and a real lifted PPU function executes and modifies guest memory.

**Does not prove:** the title reaching the main menu. That needs the disc image
served over HTTP Range, the LV2 boot path, and the guest thread scheduler driving
the entry point — work above this layer, and the prompt's next milestones.

The `0 MB, 0 pages` bug is worth keeping as a lesson: a non-null pointer is not
evidence of a successful allocation, and the log line that reported the size was
the only thing that showed the difference.
