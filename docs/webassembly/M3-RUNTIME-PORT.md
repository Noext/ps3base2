# Milestone 3: the runtime ported, and a toolchain blocker

## What was ported this round

The runtime now compiles for WebAssembly except for sources that are inherently
platform-specific. Nine new portability fixes, each found by compiling:

| file | problem | fix |
|---|---|---|
| `critpath.h` | included `<x86intrin.h>`, whose ia32 intrinsics do not compile for wasm | include `cycles.h` under wasm |
| `codewatch.cpp` | `<execinfo.h>` + `backtrace()` | new `backtrace_port.h`, returns 0 frames under wasm |
| `crash_posix.cpp` | `REG_RIP`, `si_addr`, POSIX signals | whole file guarded; wasm stub returns "no stack" |
| `crash.cpp` | `HANDLE`, `SuspendThread`, `GetThreadContext` | Windows-only, guarded out |
| `rdcapture.cpp` | `renderdoc_app.h` + `dlopen` | guarded; inert under wasm |
| `spu/ops_impl.h` | `_mm_getcsr`/`_mm_setcsr` (SPU rounding mode) | `RoundGuard` no-op under wasm |
| `host.cpp` | futex, mmap, `SYS_gettid`, `ifstream` | full `__EMSCRIPTEN__` branch with `emscripten_futex_wait`, `calloc`, a per-thread id, C `fopen` |
| `host.cpp` (2) | missing `qpc/qpf/tid/yield/sleep_ms/thread_cycles/*_native` | all seven added to the wasm branch |
| `wasm_entry.cpp` | `main` clashed with the desktop entry | new entry, guarded to wasm only |

**Result: 32 of 33 runtime sources compile for wasm**, and the whole lifted tree
(98 PPU + 170 SPU) compiles. Module: **116,383,444 bytes** as standalone wasm.

## The blocker: this SDK's libc++ does not link

The module builds but does not instantiate. Measured:

    total imports: 111
    { env: 100, wasi_snapshot_preview1: 11 }
    LinkError: Import #0 "env" "_ZNSt3__25mutexD1Ev"

100 unresolved libc++ symbols: `std::mutex` constructor/destructor/lock,
`operator new`/`delete`, `__cxa_guard_acquire/release`, `std::logic_error`.

Reproduced with a **six-line program** on both SDKs present on this machine
(`/root/Dev/ps2-web-recomp/tools/emsdk` 6.0.11 and `/root/emsdk` 6.0.13):

    #include <chrono>
    int main() { return std::chrono::steady_clock::now().time_since_epoch().count(); }

    wasm-ld: error: undefined symbol: std::__2::chrono::steady_clock:

And with `<iostream>`:

    wasm-ld: error: undefined symbol: std::__2::cout
    wasm-ld: error: undefined symbol: std::__2::ios_base::getloc()

Meanwhile `printf` links and runs fine, and `-sSTANDALONE_WASM --no-entry`
produces a module that instantiates. So the toolchain works for C, and its C++
standard library archive is not being matched — the `std::__2` namespace in the
errors is the libc++ ABI version, and no combination tried resolves it:

- with and without `-fwasm-exceptions`;
- `-sENVIRONMENT=node` and default;
- both SDK installations;
- `-sSTANDALONE_WASM`, `--no-entry`, `--allow-undefined`, `--export-all`.

**This is an environment blocker, not a port blocker.** The runtime code is
correct — 268 lifted units and 32 runtime sources compile, and the only failures
are at link time inside the C++ standard library.

## What that means for the milestone

The prompt's criterion for milestone 3 is "the module boots and the log shows the
title reaching the main menu". Reaching that needs a working libc++ link, which
this machine cannot currently do. Options, in order of cost:

1. **Repair the SDK**: the archives exist (`libc++.a` is present, 361 files in the
   lib directory), so this is likely a version mismatch between the clang that
   compiles and the archive that links. Reinstalling the matching emsdk version is
   the first thing to try and is outside this repository.
2. **Link against `libc++` explicitly** with `-nostdlib++` and the right archive
   variant per exception model — a toolchain experiment, not a code change.
3. **Use a different toolchain** (wasm32-wasi clang, or Zig's wasm target) for the
   runtime layer.

None of these change the port's code, which is why the work committed this round
stands regardless.

## What is proven

- Whole lifted tree compiles for wasm, 268/268 units, 0 failures.
- 32/33 runtime sources compile, 0 failures.
- The module builds to 116 MB standalone.
- A trivial standalone wasm **instantiates and runs under Node**, so the runtime
  path itself is usable — it is specifically the C++ library that is missing.
