# Web port -- milestone log

Following `docs/PORT-WEB-PROMPT.md`. One section per milestone, with the measured
evidence the prompt demands.

---

## Milestone 1 -- runtime without mprotect or signals -- PASSED

### What was built

- `runtime/include/runtime/mem.h` + `runtime/src/mem.cpp`: one interface for
  guest memory backing, two implementations selected by `PS3BASE_MEMORY`:
  - `os` (default, unchanged behaviour): `mmap(PROT_NONE)` + `mprotect`;
  - `bitset`: one flat `calloc` buffer plus a page bitmap, **no system
    protection calls at all**.
- `runtime/src/memory.cpp` now calls `mem::reserve/commit/decommit/protect`
  instead of `host::vm_*`.
- `runtime/src/stacks.cpp` guard pages go through `mem::protect`, and register
  their range with `mem::register_guard` so the bitset mode has a software
  range check where the CPU provides none.
- `runtime/src/crash_posix.cpp`: `install()` returns early under
  `PS3BASE_MEMORY=bitset` and logs why. No signal handler is registered on that
  path, so the dependency is *gone*, not merely unused.
- `CMakeLists.txt`: the 10 targets that compile `memory.cpp` now also compile
  `mem.cpp` (without this they failed to link -- found by building them, not by
  reading).

### Evidence

Build: `cmake --build build -j 8 --target ps3base` -> **0 errors**.

Test matrix, every suite run twice:

| suite | os | bitset |
|---|---|---|
| memory_map | 22 checks, 0 failed | 22 checks, 0 failed |
| spu_aot_variants | 9, 0 | 9, 0 |
| reservation_increment | 8, 0 | 8, 0 |
| list_walk_guard | 6, 0 | 6, 0 |
| lwmutex_hle | 5, 0 | 5, 0 |
| reservation_xproc | 4, 0 | 4, 0 |
| lock_counter | 3, 0 | 3, 0 |
| lwmutex_contention | 3, 0 | 3, 0 |
| guest_spinlock | 3, 0 | 3, 0 |
| lv2_enosys | 3, 0 | 3, 0 |
| **failing suites** | **0** | **0** |

**66 checks pass in both modes.**

Functional run in bitset mode (`PS3BASE_MEMORY=bitset tools/drive_world.py`):

    crash: signals not installed: bitset memory backend has no fault path
    mem: bitset mode: 8192 MB flat, 2097152 pages, no mprotect
    vm: guest base 0x7fd24bfff010 (backing: bitset)
    WORLD-MAPPED
    vm_map=1, UserAutoSave=2, real crashes=0

Screenshot: player in a forest, 10 hearts, hunger bar, 9-slot hotbar, HUD
"Crafting / Inventory / R2 Mine", 50 fps. Proof:
`docs/webassembly/m1-bitset-world.png`.

### One defect found and recorded

My first `decommit` in bitset mode did a `memset` of the range to match the os
path's "decommitted pages read as zero". That was wrong twice: it costs time on
every `dealloc`, and it is unnecessary because the flat buffer starts zeroed and
a recommit never rewrites it. Removed, with the reasoning left in the code.

### The software guard check, and the margins made observable

The prompt asks that the bitset mode replace the CPU's page protection with a
software check that logs **thread, r1, lr and the lifted function**. That is now
in `stacks::note_sp`: a stack grows down, so `r1 < stack base` is an overrun into
the guard, and the report carries the thread name, `r1`, the stack base, `lr` and
`hooks::name_of(lr)`.

Measured on a real bitset run: **0 guard overruns**, so no false positives.

The margins themselves were being collected but never printed, because
`status_dump` runs on a path a killed process never reaches -- the same gap I
recorded two rounds ago and had not closed. `PS3BASE_STACK_DUMP=<seconds>` now
dumps the inventory from its own thread:

    'main_thread'          r1_min 0xd0018640  margin 34368 bytes
    'MinecraftSpursHdlr1'  r1_min 0xd0037d60  margin 32096 bytes
    'MinecraftSpursHdlr0'  r1_min 0xd0053ec0  margin 16064 bytes
    '_gcm_intr_thread'     r1_min 0xd0073ec0  margin 16064 bytes

**90 such lines in 60 s.** Every thread keeps 15-34 KB of headroom, which is the
first hard evidence that no guest thread in normal play comes near its guard.

I placed that dump inside the `PS3BASE_CENSUS` block on the first attempt, so it
only ran when the census was enabled; it now has its own thread. Caught by
measuring, not by reading the diff.

### What is not yet true

- `crash::install()` is skipped at **runtime**, not compiled out. For a real wasm
  build the crash reporter must leave the link line entirely; that is milestone 3
  work.
- The guard check samples at syscall entry, not on every store. A stack that
  overruns and returns within one call would be missed. Acceptable for this
  milestone (the failure it hunts is a persistent corruption), but stated.

---

---

## Milestone 2 -- native WebGPU backend -- IN PROGRESS

### Scope measured first

The renderer's Vulkan surface, counted rather than guessed:

| where | value |
|---|---|
| distinct `Vk*` types across `runtime/src/gpu/` | **118** |
| distinct `vk*` calls | **100** |
| calls in `rsx_renderer.cpp` alone | 87 (in 4,626 lines) |
| `VK_FORMAT_*` used | **28** |
| `VK_BLEND_FACTOR_*` used | **15** |

### First brick, done: the correspondence table

`runtime/include/runtime/wgpu_map.h` + `runtime/src/gpu/wgpu_map.cpp`, tested by
`tests/wgpu_map.cpp`. This is the part of the port that can be written and
audited without a device, and it forces the two inexpressible cases to be named
now instead of discovered after the port.

Measured coverage, printed by the test:

    formats: 17 direct, 7 feature, 4 expand, 0 emulate, 0 absent
    blend  : 13 direct, 0 feature, 0 expand, 2 emulate, 0 absent

The decisions that matter:

- **4 formats are `Expand`**: `A1R5G5B5`, `R5G5B5A1`, `R4G4B4A4`, `R5G6B5`. WebGPU
  has no 16-bit packed RGBA colour, so they widen to `RGBA8Unorm` at **twice the
  bytes per texel**. The texture cache must budget for it.
- **7 formats are `Feature`**: the BC1/BC2/BC3 set, gated on
  `texture-compression-bc`, plus `DEPTH32FLOAT_STENCIL8`.
- **2 blend factors are `Emulate`**: `CONSTANT_ALPHA` and
  `ONE_MINUS_CONSTANT_ALPHA`. WebGPU's `constant`/`one-minus-constant` read the
  **colour** channels, so the alpha variant has to come through a shader uniform.

Measurement: 8 checks, 0 failures. The test asserts each case explicitly — a table
that silently marked the packed formats `Direct` would pass a naive "is it
mapped?" check and fail this one.

### Second brick, done: a real wgpu device, and the pipeline cost measured

`tests/wgpu_smoke.cpp` creates an instance, requests an adapter and a device,
compiles WGSL, and then builds the pipeline state space the renderer actually
varies — and measures it. Output on this machine:

    adapter: device 'NVIDIA GeForce RTX 4060 Laptop GPU' vendor 'NVIDIA'
    pipeline cache: 540 distinct pipelines from 6 blend x 5 equation x 3
                    depth-mode combinations, 38.4 ms total, 0.071 ms each
    wgpu_smoke: 8 checks, 0 failures

**This settles the risk the feasibility study ranked third.** It said dynamic
pipeline state would cause a pipeline explosion and that WebGPU pipeline creation
is expensive. Measured: 540 pipelines in **38.4 ms**, **0.071 ms each**. Even if
the real renderer needs ten times that state space, it is a few hundred
milliseconds of one-time cost, not a per-frame problem.

Three facts this proves, none of them assumed:

1. **wgpu-native works here** and reaches the real GPU through Vulkan — so the
   native backend of milestone 2 can be developed and tested on this box.
2. **WGSL compiles without glslang and without SPIR-V.** The runtime shader
   compiler is not a blocker for the renderer; the shaders must be generated as
   WGSL at port time, and that path demonstrably works.
3. **The existing `PipelineKey` is already the right shape.** It hashes shader
   pair, colour formats, blend factors, equation, logic op, write masks, polygon
   mode and topology class — exactly the fields WebGPU bakes into a pipeline.
   The cache needs no redesign; it needs a new creation backend.

### What is not yet done in milestone 2

No renderer call site is ported. The 87 `vk*` uses in `rsx_renderer.cpp` still
go to Vulkan; the wgpu path exists only in the test. The remaining work is
mechanical but large: texture and buffer creation, the render pass (WebGPU uses
dynamic render passes, which suits this renderer), bind groups, and the swapchain
equivalent.

---

## Milestones 3-5

Not started.
