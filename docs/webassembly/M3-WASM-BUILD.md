# The whole tree compiled for WebAssembly

Milestone 3's build, measured. No display: this is the code, not the page.

## Result

| part | units | failures | time (serial) | objects |
|---|---|---|---|---|
| PPU | **98 / 98** | **0** | **492 s** | 27.0 MB |
| SPU | **170 / 170** | **0** | **217 s** | 7.8 MB |
| **total** | **268 units** | **0** | **709 s (11.8 min)** | **34.8 MB** |

Link, all code kept: **113,024,610 bytes (107.8 MiB)** of `.wasm`.
Link with dead-code elimination and nothing referenced: 1,339 bytes — correct,
because the test entry point calls nothing. A real module whose entry drives the
game will land between the two, and the reference figure from
`ps2-web-recomp`'s shipped games is **33.4–41.1 MB**.

**Every unit of the tree compiles for wasm with no source changes beyond four
fixes.** That is the milestone's central claim and it is measured, not estimated.

## The flags that turned out to be required

    -std=c++20 -fwasm-exceptions -msimd128 -msse4.1 -mfma

`-fwasm-exceptions` is **not optional**: `runtime/src/thread.cpp` uses
`try`/`catch` to unwind a guest thread exit, and without it the compile fails with
"cannot use 'try' with exceptions disabled". Emscripten's native wasm exceptions
handle it.

## The four fixes the build needed

1. **`_tzcnt_u64`** (BMI1) in `reservation.cpp` → `std::countr_zero`.
2. **`__rdtsc`** (81 sites) → a wasm-only macro to `wasm_rdtsc()` backed by
   `emscripten_get_now()`.
3. **`_mm_getcsr`/`_mm_setcsr`** in `spu/ops_impl.h` — the SPU rounding-mode guard.
   Wasm has fixed round-to-nearest and no FCSR. `RoundGuard` becomes a no-op under
   wasm and the four conversions that need truncation say so explicitly, rather
   than depending on a CPU mode wasm does not have.
4. **`host.cpp`** needed a whole `#elif defined(__EMSCRIPTEN__)` branch:
   - `syscall(SYS_futex, …)` → `emscripten_futex_wait`/`wake`;
   - `mmap`/`mprotect` → `calloc` and no-ops, because the bitset backend never
     reaches them;
   - `SYS_gettid` → a per-thread counter;
   - `std::ifstream` in `read_line` → C `fopen`, because Emscripten's prebuilt
     libc++ does not instantiate `basic_ifstream` in this toolchain.

The Win32/POSIX implementation now sits behind `#if !defined(__EMSCRIPTEN__)`, so
both targets build from one source. Native build: **0 errors**, unchanged.

## Browser limits, against measured numbers

| limit | value | our position |
|---|---|---|
| module size | Chrome handles 100 MB+; 33–41 MB proven | **107.8 MB worst case**, needs trimming |
| function size | worst generated function 2.2 MB → 1.4 MB wasm | **proven to run (BIGFUNC-RAN)** |
| function count | no spec cap; 1 M+ fine | ~72,000 generated, fine |
| compile time | 11.8 min serial | fine at `-j 8` |
| memory | 4 GiB wasm32 | **2.25 GiB with PS3BASE_REGIONS=compact**, 1792 MB spare |

The one that needs attention is **module size**: 107.8 MB with everything kept is
above what is comfortable, and the fix is dead-code elimination driven by a real
entry point — which is what `ps2-web-recomp` does implicitly because its harness
only links what the game reaches.

## What is not done

- No module with a real entry point that boots the title. The link that produced
  107.8 MB has a stub `main`.
- `-j 8` parallel compile was not run for the whole tree; the 709 s is serial.
- The 34.8 MB of objects is with `-O1`; `-O2` on the whole tree is unmeasured.
