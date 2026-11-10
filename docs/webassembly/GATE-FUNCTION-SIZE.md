# The gate measured: does the largest lifted function survive WebAssembly?

Milestone 3 of `docs/PORT-WEB-PROMPT.md` is the gate, and it names one unmeasured
risk: **V8 refuses to optimise very large functions**, and the codegen emits
functions without any size control. Everything after the gate depends on the
answer, so measuring it now is worth more than another brick of milestone 2.

## The size distribution, measured

Across `out/ppu/` (98 files, 130.8 MB of generated C++):

| threshold | functions |
|---|---|
| > 10 KB of source | **3,155** |
| > 50 KB | **177** |
| > 100 KB | **46** |
| > 500 KB | **3** |
| > 1 MB | **2** |

Total 22,685 functions. The largest is `f_0070d138_body` in `mc_071.cpp`:
**2,266,097 bytes of source**, **42,946 lines**, a single C++ function.

## The largest one taken through Emscripten, end to end

Extracted alone, with the generated declarations, and compiled:

| step | result |
|---|---|
| compile to wasm object, `-O1` | **17.75 s**, **1,604,424 bytes** of object, **490 MB** peak RSS |
| link, with stubs for the other lifted functions | **1,409,972 bytes** of `.wasm` |
| **run under Node (V8)** | **`BIGFUNC-RAN`** — V8 parsed, instantiated and executed it |

**This is the answer the gate needed, and it is yes.** A 2.2 MB single function
does not break V8, does not exceed the wasm function limit, and runs. The
feasibility study's third-ranked risk — "function size in V8, unmeasured until
milestone 3" — is now measured and cleared for the worst case in the tree.

## What compiling for wasm actually broke, and how

Three findings, all from trying rather than reading:

1. **`_tzcnt_u64` and `_tzcnt_u32` (BMI1) do not exist in WebAssembly.**
   `runtime/src/reservation.cpp` uses them. `std::countr_zero` is the portable
   replacement.
2. **`__rdtsc` appears 81 times**, across `critpath`, the job system, SPU timing
   and the codewatch. It is x86-only. New `runtime/include/runtime/cycles.h` +
   `runtime/src/cycles.cpp` give `host::cycles()`: `__rdtsc` natively, a steady
   clock under wasm. Every one of the 81 sites only ever subtracts two values, so
   a monotonic clock preserves their meaning.
3. **The 46 `__asm__` blocks in `gowa_stubs.cpp` are symbol aliases, not
   assembly** (`extern "C" int gowa_stub_0() __asm__("_ZN2pm5hooksERm")`). They are
   portable; no work needed. Worth stating because a raw count of `asm` would
   have suggested otherwise.

## What this does not prove

- The **whole tree** has not been built for wasm. 98 PPU units, 170 SPU units and
  the runtime are unbuilt; only the largest single function was carried through.
- Compile time for the full tree is extrapolated, not measured: at 17.75 s for the
  worst function and 5.32 s for a typical one, serial build is tens of minutes, so
  parallel build is required, not optional.
- The 490 MB peak RSS per large function matters in parallel: eight of those at
  once is 4 GB.

## Consequence for the plan

The gate no longer looks like it will fail on function size. The remaining gate
risk is the **4 GiB address space**, which is known-structural and has the two
options already costed (Memory64, or address translation). That is milestone 3's
real work, and it is now the only unmeasured serious risk.
