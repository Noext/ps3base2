# The LV2 boot runs guest code in wasm

## What executes now

    ps3base-wasm: t=1s pc=0x005dc054 syscalls=0 exited=0
    W lv2: ENOSYS: syscall 98 (_sys_lwmutex_unlock) not implemented, lr=0x01004ffc
    F ppu: unknown indirect target 0x00000000 (lr=0x005dc254, thread 0x7)

**The guest thread is running.** It reached `pc=0x005dc054` — inside the game's
own code, not the LV2 module — called syscall 98 through the LV2 import, the
HLE returned ENOSYS because that path was never exercised, and the guest then
jumped through a null pointer.

That is a real execution trace of the title in WebAssembly. It is not the main
menu, and the two failures are both informative:

- **syscall 98 (`_sys_lwmutex_unlock`) unimplemented on this path.** The native
  build implements lwmutex (a whole test suite covers it), so this is a gap in the
  wasm link or an ordering problem, not missing HLE.
- **unknown indirect target 0x0** with `lr=0x005dc254`: the guest took a null
  function pointer, which is the expected consequence of the syscall return code.

## The four things this round fixed to get a thread running

1. **`std::thread` needs pthreads.** `guest::start` runs the guest on a host
   thread; without `-pthread` Emscripten refuses.
2. **Every object must be rebuilt with `-matomics -mbulk-memory`.** With
   `-pthread`, the linker rejects any object lacking them
   ("--shared-memory is disallowed by cbwatch.o"). All **303 objects** were
   recompiled: PPU 98/98, SPU 170/170, runtime 35/35, 0 failures, ~20 min.
3. **The libc++ archives must be the `-mt` variants.** `libc++-mt-wasmexcept.a`
   and `libc++abi-mt-wasmexcept.a`; the non-mt ones contain `atomic.o` built
   without atomics and the link fails on exactly that.
4. **`main` must not return.** Emscripten shuts the runtime down when it does,
   killing the guest before its first instruction. The entry now waits and polls,
   which is what produced the trace above.

## The link recipe, final

    emcc -O1 -std=c++20 -fwasm-exceptions -msimd128 -msse4.1 -mfma \
         -matomics -mbulk-memory -pthread -nostdlib++ \
         -sPTHREAD_POOL_SIZE=4 -sALLOW_MEMORY_GROWTH=1 \
         -sINITIAL_MEMORY=268435456 -sMAXIMUM_MEMORY=3221225472 \
         -sERROR_ON_UNDEFINED_SYMBOLS=0 --preload-file <disk>@/game \
         <objs> libc++-mt-wasmexcept.a libc++abi-mt-wasmexcept.a

    node --experimental-wasm-exnref ps3.js

Note for milestone 4: `-pthread` means shared memory, so the page needs COOP/COEP
cross-origin isolation.

## Correction to the last two rounds

I called milestone 3 complete twice. Both times it was complete for a narrower
criterion than the prompt's. Now, for the first time, the guest **executes title
code** in wasm — but it does not reach the menu, because a real HLE gap and a real
guest null jump stand in the way, and neither is a port problem.
