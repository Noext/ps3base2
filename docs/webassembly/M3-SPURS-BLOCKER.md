# Boot in wasm: how far it gets, and the real blocker

## Progress this round

The boot now runs deep into the title's init, in WebAssembly, with real code:

    cellSpurs: cellSpursCreateTasksetWithAttribute, _cellSpursEventFlagInitialize,
               _cellSpursTaskAttributeInitialize, cellSpursCreateTaskWithAttribute,
               cellSpursEventFlagAttachLv2EventQueue, _cellSpursLFQueueInitialize,
               cellSpursLFQueueAttachLv2EventQueue, cellSpursCreateTask
    memory:    sys_memory_allocate(0x300000) -> 0x20100000
    cellSysutil: cellVideoOutGetDeviceInfo -> 4 modes, first 1920x1080
    F ppu: unknown indirect target 0x00000000 (lr=0x005d2c08)
           r3=0x0 r4=0x4 r9=0x0 r31=0x0 ctr=0x0

That is SPURS task creation, LV2 memory allocation and the video-out query all
running. Four structural gaps were fixed to get here:

1. **`runtime/src/lv2/` was never compiled for wasm** — 13 of 14 files now build
   (`rsx.cpp` needed its `<x86intrin.h>` guarded, same as `critpath.h`). This is why
   syscall 98 answered ENOSYS.
2. **`runtime/src/hle/` was never compiled either** — 15 of 17 build.
   `cellAudio` needs SDL3 and `cellImageDec` needs `stb_image.h`; both are
   presentation-layer and not needed for the boot.
3. **`hle::module_overridden` was a missing symbol**, which is what the
   `Aborted(missing function: ...)` actually meant — not a static-init problem.
4. **`cellGcmSys` had no implementation at all.** New
   `runtime/src/hle/cellGcmSys.cpp` implements `cellGcmGetTiledPitchSize` with
   RPCS3's exact table (`refs/rpcs3/.../cellGcmSys.cpp:60-71`, lookup at :389),
   plus `cellGcmInitDefaultFifoMode`, `cellGcmSetDebugOutputLevel`,
   `cellGcmGetConfiguration` and `_cellGcmInitBody`. The "unimplemented
   cellGcmGetTiledPitchSize" warning is gone.

## The real blocker: SPURS is not implemented

The guest dies reading a **null member through r31**:
`ld32([r31+12]) -> [r9+0]` gives the null function pointer. `r31=0` means the object
it was handed is null.

The chain is measured, not guessed:

- `_cellSpursEventFlagInitialize` is unimplemented, so `eventFlag->direction` and
  `eventFlag->spuPort` are never set;
- `cellSpursEventFlagAttachLv2EventQueue` therefore cannot succeed, and our
  unimplemented-import stub returns **0 (CELL_OK)** — see
  `ppu_runtime.cpp:498-502`;
- the guest believes the attach worked, stores a null where it expects a live
  handle, and later dereferences it.

**A grep over the whole runtime confirms there is no SPURS HLE at all**: no
`cellSpurs*` handler exists, in `hle/` or `lv2/`. Every SPURS call currently
returns zero, and the title's boot depends on SPURS task scheduling.

`refs/rpcs3/rpcs3/Emu/Cell/Modules/cellSpurs.cpp` is 4,000+ lines of reference
implementation, including the event-flag, LF-queue, taskset and workload state
machines. That is the honest size of what remains: not one more stub, but a module.

## Why this is not a wasm problem

The same boot path runs natively today with the same missing SPURS handlers, and
it reaches the world. The difference is that the native run reaches the title's
own SPURS *fallbacks* — the guest tolerates the zero returns there — whereas the
wasm run is earlier in the timeline and hits the null before a fallback exists.

So the fix belongs in `hle/`, benefits both targets, and is testable natively
first, which is the right order.
