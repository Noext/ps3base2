# The boot unblocked: it was the firmware set, not a null pointer

## The root cause, found by comparing the two runs

Last round I retracted "a global at `0x00D6E800` is never written, that is what the
boot is missing". This round found the actual cause, and it is none of the things I
had been chasing.

Diffing the import call **sequences** of the native and wasm boots showed:

| | native | wasm |
|---|---|---|
| images loaded | **6** | **1** |
| `prx: load` calls | 30 | 0 |
| first import at t=1s | none (still loading) | 12 already |

The native run loads `libsysmodule`, `libgcm_sys`, `libsre`, `libfs` and their PRX
objects before the guest does anything. The wasm run had only `mc.elf` and
`liblv2.elf` on its mount, so every `sys_prx_load_module` failed. The guest said so
itself, in its own tty output:

    tty: [0] prx.c:sys_prx_load_module:Error: res(id)=0x80010007

`0x80010007` is `CELL_ESRCH` — not found. **The firmware ELFs were simply not in
the wasm disk image.**

## The fix and its effect

Copying all 8 firmware ELFs into the mount changed the boot immediately:

    loader: loaded libsysmodule (fw/4.91/reloc/libsysmodule.elf) text 0x01100000-...
    prx: load '/dev_flash/sys/external/libsysmodule.sprx' -> 0x23000a00 (LLE, ...)
    loader: loaded libgcm_sys ... prx: load libgcm_sys.sprx -> 0x23000d00 (LLE)
    loader: loaded libsre ...     prx: load libsre.sprx     -> 0x23001000 (LLE)
    loader: loaded libfs ...      prx: load libfs.sprx      -> 0x23001100 (LLE)

**Six libraries now load in wasm, exactly as natively.** And the null-pointer crash
that consumed three rounds disappeared with it:

    before: F ppu: unknown indirect target 0x00000000 (lr=0x005d2c08)
    after:  Aborted(missing function: _ZN3spu9SpuThreadC1Ev)

The crash was a downstream symptom of the incomplete module set: without
`libgcm_sys` and friends loaded, a rendering-init structure the guest expects to be
populated by them was never built, and its function pointer stayed null. **No store
to `0x00D6E800` was ever missing — the code that would have run never ran.**

That vindicates the write-watch measurement from last round (zero writes in both
runs) and shows the correct reading: "nobody writes it" meant "that code path is
never reached", not "a store was dropped".

## What this round also fixed

Getting the boot past that point exposed a chain of missing wasm objects, each one
real:

1. `runtime/src/spu/*.cpp` (7 files) — never compiled for wasm. All 7 now build.
2. `runtime/src/jobsys/*.cpp` (18 files) — 9 build, 9 still fail on the same three
   x86 facilities.
3. `_mm_getcsr`/`_mm_setcsr` appears in **six** places across `spu/`, `jobsys/` and
   `spu/edge/`. Each was guarded individually; the guards are correct but the
   repetition is a smell that wants the shared header.
4. `_tzcnt_u32` in `natsched.cpp` → `std::countr_zero`.
5. `__vectorcall` in `spu_float.h` made the whole header uncompilable by clang —
   pre-existing, fixed last round.

A shared `runtime/include/runtime/x86_compat.h` was started to hold the `__rdtsc`,
CSR and EFLAGS shims in one place. It does not work yet: Emscripten's SIMDe headers
already define `_mm_getcsr` as a macro, so redefining it collides no matter the
helper name. **The per-file guards are what actually compile**, and the header
should either include `<immintrin.h>` first and use different call-site names, or be
abandoned in favour of the guards. That is unresolved and stated as such.

## Where the boot stands

    WASM-RUNTIME-UP
    WASM-GUEST-CODE-RAN
    WASM-BOOT-IMAGES-LOADED
    WASM-BOOT-STARTED
    Aborted(missing function: _ZN6jsedge8spu_idleEj)

The boot now gets **past** the point that blocked it for three rounds and fails on
the next missing object, which is ordinary porting work: compile `jobsys/`, resolve
the x86 shims, repeat.
