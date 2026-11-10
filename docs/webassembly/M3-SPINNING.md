# The wasm boot runs: 192k syscalls, no crash

## What changed this round

The boot now reaches my wait loop and stays there:

    WASM-RUNTIME-UP
    WASM-GUEST-CODE-RAN
    WASM-BOOT-IMAGES-LOADED
    WASM-BOOT-STARTED
    WASM-FILE-PROVIDER-DRIVES-LOADER
    WASM-BOOT-ALIVE

    ps3base-wasm: t=30s pc=0x01307a08 syscalls=192167 exited=0

**The guest executes continuously for the full 30-second window, 192,167 syscalls,
no abort, no missing function, no crash.** That is the first time the wasm build has
run this long without dying.

## The four things that unblocked it

1. **`gpu::rsx::special_methods()` and `registers()` extracted** into a new
   `runtime/src/gpu/rsx_special_methods.cpp`. Both are pure data tables; they lived
   in the 4,626-line Vulkan renderer only by history. The guest calls them during
   RSX init, before any draw. The renderer keeps its own copy under
   `PS3BASE_NO_RENDERER`, so a graphical build is unchanged.
2. **`edge::ovl::stats_on()` extracted** the same way. `spu/thread.cpp` calls it at
   SPU startup; its file includes `edge_ovl_gen.h`, generated from a **different
   title's ELF** (`bin/01.12/GOWA.patched.elf`) that this checkout does not contain.
   Being a `getenv` predicate with no dependency on that data, it can stand alone.
3. **`gowa_stubs.cpp` removed from the wasm link.** It defines no-op stand-ins for
   SPU job symbols "that exist only for this title", including `wj::info(int)` whose
   real definition is elsewhere. Linking it produced a **wasm signature mismatch at
   static-init time**, which is what killed every earlier run at
   `__wasm_call_ctors` with no message. Diagnosed with `-sASSERTIONS=2
   -sEXCEPTION_DEBUG=1`, which named it exactly:
   `GLOBAL__sub_I_jobs_modules.cpp -> wj::info(int) -> signature_mismatch`.
4. **`jobsys/jobs_modules.cpp` excluded**, matching CMake, which already removes it
   from the native build (`CMakeLists.txt:117`). Its `wj::info` call is the one the
   stubs were faking.

Also: `lv2/rsx.cpp` no longer starts its FIFO and vblank threads under wasm. Those
request a 32 MB pthread stack, which aborts in the browser.

## Where it stands now

The guest is **spinning** at `pc=0x01307a08`, inside `libgcm_sys`
(`0x01300000-0x0130ad2c`), at roughly 6,400 syscalls per second, without
progressing. That is a busy-wait, not a crash: the title is waiting for a
condition this build never satisfies.

The next step is to find which syscall it is repeating. `PS3BASE_SYSCALL_LOG` would
normally answer that in one run, but **`getenv` is inert under wasm**, so the
diagnostic has to be a compile-time constant like the null probe was.

## Honest position on milestone 3

The criterion is "the module boots and the log shows the title reaching the main
menu". **It does not reach the main menu.** It loads six firmware libraries, starts
the LV2 thread, allocates guest memory and runs 192k syscalls — real progress
compared to where this round started, and still short of the gate.

The milestone remains **open**, and the prompt's gate rule applies: if it cannot be
reached, that has to be reported as a failed gate rather than a completed milestone.
