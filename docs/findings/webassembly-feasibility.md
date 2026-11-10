# Feasibility: running this game in a browser (WebAssembly + WebGPU)

Study only. **No runtime code was modified.** Everything below is either measured
on this machine or read from the two code bases.

Model studied: `/root/Dev/ps2-web-recomp` (not in `refs/`, it lives in `/root/Dev/`).

---

## 0. The model, in the terms this port would need

ps2-web-recomp puts the game's main CPU in WebAssembly (statically recompiled to
C++ then `emcc`), and:

| Concern | How ps2-web-recomp does it |
|---|---|
| Emulation thread | a pthread worker; `-sPTHREAD_POOL_SIZE=2` |
| GPU thread | the page thread owns the WebGPU device |
| Hand-off | a **ring buffer** of GS commands from the emulation worker to the page thread |
| Disc | **HTTP Range** requests via `fetch(…, {headers:{Range:…}})`, so only the ELF is fetched up front |
| Audio | `AudioWorklet` fed from shared memory (`ps2web_audio_ring()`) |
| Memory | `-sINITIAL_MEMORY=268435456` (256 MB) with `-sALLOW_MEMORY_GROWTH=1` |
| Blocking | `emscripten_futex_wait`, `-sJSPI` |
| Shaders | microprograms compiled **ahead of time** to SIMD C++; unseen ones translated to a wasm module on the spot |
| Module size produced | **33.4 – 41.1 MB** `.wasm` for real games (measured on its builds here) |

That is the template. The question is how much of it our runtime can follow.

---

## 1. Inventory of non-portable dependencies

### 1.1 `mmap` / `mprotect` — the hardest one

| Where | What |
|---|---|
| `runtime/src/host.cpp:268` | `mmap(nullptr, size, PROT_NONE, MAP_PRIVATE\|MAP_ANONYMOUS\|MAP_NORESERVE)` reserves the whole guest space |
| `host.cpp:272/276/280` | `vm_commit`, `vm_decommit`, `vm_protect` are `mprotect` |
| `runtime/src/memory.cpp` | per-page commit/decommit drives the guest's own memory model |
| `runtime/src/stacks.cpp` | 64 KB `PROT_NONE` guard under every guest stack |

**Replacement:** WebAssembly has no `mprotect`. Two options, both real work:
- keep a `std::vector<bool>`/bitset of committed pages and check it on every
  guest access — correct, and the cost is a branch per access on the hottest path
  in the emulator;
- or drop fine-grained protection and allocate everything up front (see 2).

The **guard pages cannot exist in wasm at all**: an out-of-range access traps on
the whole memory, not per page. The stack guards we added this campaign would
become a *software* range check.

### 1.2 Signal handlers — gone

`runtime/src/crash_posix.cpp` installs `SIGSEGV`, `SIGBUS`, `SIGILL`, `SIGFPE`
and inspects `si_addr` against `vm::g_base .. g_base + 0x100000000`. There are no
POSIX signals in a browser. **Replacement:** `-sEXCEPTION_CATCHING` does not cover
wasm traps from a null/out-of-bounds access usefully; the practical answer is to
remove the crash reporter from the web build and rely on explicit bounds checks.
That is acceptable — it is a debugging aid, not a feature.

### 1.3 Reserved address space — the hard constraint

Measured on the live process (`/proc/<pid>/status`, `maps`):

    VmSize  12,260,848 KB  = 11.7 GB reserved
    VmRSS    1,088,856 KB  =  1.04 GB resident
    PROT_NONE           9,917 MB reserved, not committed
    rw-p                1,455 MB committed

The guest space is **4 GiB by construction** (`g_base + 0x100000000ull` appears
throughout `memory.cpp`). wasm32 gives exactly 4 GiB of address space for
*everything* — guest, host stack, code, tables, and the shadow stack. **It does
not fit as a flat map.**

### 1.4 Threads and blocking

| Where | What |
|---|---|
| `host.cpp:261-265` | `syscall(SYS_futex, …)` directly — Linux-only, three calls |
| guest threads | `runtime/src/thread.cpp`, one host thread per guest PPU thread |
| `runtime/src/lv2/*` | lwmutex, semaphores, event queues built on futex |
| `std::thread` | also in `codewatch`, `critpath`, `hooks`, `crash` — diagnostics only |

**Replacement:** `emscripten_futex_wait/wake` are drop-in for the three calls, and
pthreads work under Emscripten with a fixed worker pool. The real constraint is
that **the pool must be sized up front** (`-sPTHREAD_POOL_SIZE`), and the title
creates one host thread per guest thread. ps2-web-recomp sidesteps this by making
thread switches unwind the host stack — our runtime does not.

### 1.5 Vulkan — 100 entry points

`grep -rhoE 'vk[A-Z][A-Za-z]+' runtime/src/gpu/` → **100 distinct functions**,
concentrated in `rsx_renderer.cpp` (4,626 lines) and `presenter.cpp` (1,126).
Hottest: `vkCmdPipelineBarrier` ×14, `vkAllocateMemory` ×11, `vkDeviceWaitIdle` ×9.

### 1.6 Shader compilation at runtime

`runtime/src/gpu/shader_compiler.cpp` calls `glslang_initialize_process()` and
builds SPIR-V **at run time**, caching to `shader_cache/<key>.spv`. glslang does
not build for wasm without effort, and the browser cannot compile SPIR-V anyway —
WebGPU takes **WGSL only**. **Replacement:** precompile every shader variant at
port time to WGSL, or ship a WGSL backend in the shader generator. This is the
same trick ps2-web-recomp uses for vector-unit microprograms.

### 1.7 SDL

**142 distinct `SDL_*` symbols**: window, gamepad (`SDL_AttachVirtualJoystick`,
`SDL_GAMEPAD_AXIS_*`), audio, events. **Replacement:** Emscripten provides SDL2/3
shims for window+events+gamepad; audio becomes an `AudioWorklet`. The virtual
joystick used by `tools/play.sh` has no web equivalent and would be dropped.

### 1.8 File access

38 call sites in the VFS layer. **Replacement:** `IDBFS` for saves (as
ps2-web-recomp does) and **HTTP Range** for the disc image, exactly as modelled.

---

## 2. Memory: what is really needed, and the two options

Measured during a session: `sys_vm_memory_map -> 0x100c0000 (used 0x62a0000 / 0xd500000)`
— **98.6 MB used of 213 MB declared**. The declared regions:

    main RAM   0x00010000..0x10000000   256 MB
    user64k    0x10000000..0x40000000   768 MB

**Option A — Memory64 (`-sMEMORY64`)**
Pros: the guest keeps a truthful flat 4 GiB map, so `g_base + ea` stays a plain
pointer and *no* runtime code changes. Cons: Memory64 modules require Wasm 64-bit
memory support (Chrome 133+, not universal), every pointer doubles in size
(roughly +15-25 % on code size and cache pressure), and Emscripten's Memory64
support is still marked experimental. **Recommended path if the browser floor can
be raised.**

**Option B — 32-bit with translated addresses**
Keep wasm32's 4 GiB but do not map the guest flat: place *only the regions the
title actually touches* (≈100 MB measured) and translate `guest_ea -> host
offset` through a page table on every access. Pros: works everywhere today.
Cons: a lookup on the hottest path in the emulator; our PPU ops are written as
`base[ea]` and would all change. **This is the expensive option in engineering
terms, not in memory terms.**

A middle road exists and is worth naming: map the guest at **offset 0 with only
the used windows bound**, and give the runtime a page-fault-free "committed"
bitset. That keeps `base[ea]` syntactically but needs the 4 GiB to be the same
4 GiB as the wasm heap. It does not fit alongside code and stack.

---

## 3. Size: measured, not estimated

I compiled **one real lifted PPU unit** (`out/ppu/mc_000.cpp`, 1.33 MB of source)
with Emscripten 6.0.11 (clang 24):

    emcc -O2 -std=c++20 -msimd128 -msse4.1 -mfma -c out/ppu/mc_000.cpp
    → 5.32 s, 1,033,569 bytes of object, 272 MB peak RSS

Extrapolating to the whole tree (98 PPU units, 170 SPU units, 4,327,753 lines,
22,685 PPU functions, 49,474 SPU blocks):

| | measured / extrapolated |
|---|---|
| PPU compile, serial | **8.7 min**, ~101 MB of objects |
| SPU compile, serial | ~6.8 min (scaled by source size) |
| generated source | 41 MB (23 MB PPU + 18 MB SPU) |

Cross-check against real modules: ps2-web-recomp ships **33.4–41.1 MB** `.wasm`.

**Browser limits approached:**

| Limit | Value | Our position |
|---|---|---|
| module size | no hard spec cap; Chrome handles ~100 MB+, 33-41 MB proven | fine |
| function size | no spec cap; V8's practical ceiling is tens of MB of code per function | **at risk**: some lifted functions are large |
| number of functions | no spec cap; 1 M+ is fine | fine (≈72 k) |
| 4 GiB memory | hard for wasm32 | **binding constraint** |

The genuinely unknown one is **function size**: V8 bails out of optimizing very
large functions, and our units are generated per-function-not-per-unit.

### What broke, and how it was fixed to get the measurement

Two real portability findings, discovered by trying:

1. `ppu/context.h` uses `__m128i`/`__m128`. They are **available** under
   Emscripten with `-msimd128 -msse4.1 -mfma`, but **not** with `-msimd128` alone.
2. `ppu/ops.h` uses SSE4.1 intrinsics (`_mm_shuffle_epi8`, `_mm_blendv_ps`) —
   **22 distinct intrinsics total**. Emscripten's compat headers cover all of the
   ones used; my initial shim was unnecessary. So the SIMD path is **not** a
   blocker, which was the single most pleasant finding of this study.

---

## 4. Rendering: Vulkan → WebGPU

28 distinct `VK_FORMAT_*` in the renderer. Mapping, by category:

| RSX/Vulkan need | WebGPU equivalent | Verdict |
|---|---|---|
| `R8G8B8A8`, `B8G8R8A8` UNORM/SRGB | `rgba8unorm`, `bgra8unorm` (+ `-srgb`) | direct |
| `R16G16B16A16_SFLOAT`, `D32_SFLOAT` | `rgba16float`, `depth32float` | direct |
| `D24_UNORM_S8_UINT` | `depth24plus-stencil8` | direct |
| `A1R5G5B5` | no 16-bit RGBA format in WebGPU | **needs expansion to `rgba8unorm`** (2× bandwidth) |
| `BC1/BC2/BC3` | `bc1/2/3-rgba-unorm` — **feature-gated** `texture-compression-bc` | conditional |
| blend factors, 15 used (`rsx_renderer.cpp:2358`) | most map to `GPUBlendFactor` | **partial** |
| `BLEND_FACTOR_CONSTANT_COLOR` / `CONSTANT_ALPHA` and their `ONE_MINUS` forms | `GPUBlendFactor` has `constant`/`one-minus-constant` but **no constant-alpha variant, and no constant blend colour in blend state** | **no direct equivalent** |
| blend ops `ADD/SUBTRACT/REVERSE_SUBTRACT/MIN/MAX` | `GPUBlendOperation` has all five | direct |
| `TRIANGLE_LIST` topology | `triangle-list` | direct |
| MRT (multiple render targets) | supported, up to `maxColorAttachments` (8) | direct |
| dynamic state | `dynamicState` not in WebGPU; **pipeline per state combination** | **cost**: pipeline cache explosion |
| `vkDeviceWaitIdle` ×9 | `device.queue.onSubmittedWorkDone()` | slower, avoidable |
| runtime glslang → SPIR-V | **WGSL only** | **must precompile** |

**Without a direct equivalent:** 16-bit packed colour formats, the constant-alpha
blend factors, and dynamic pipeline state. The first costs bandwidth, the second costs pipeline objects —
RSX games vary blend/depth state per draw, so this needs a pipeline cache keyed on
state, which WebGPU has no built-in equivalent of.

### Intermediate step: a native WebGPU backend

Building the renderer on **wgpu-native** (or Dawn) as a native binary first is
clearly worth it: it proves the API translation without the browser, lets the
shaders be written once in WGSL, and ps2-web-recomp's own `host/window.cpp` shows
the native runner shares the wgpu path with the web one. **Recommended as jalon 2.**

---

## 5. Plan in one-day milestones

| # | Milestone | Deliverable | Risk |
|---|---|---|---|
| 1 | **Runtime without `mprotect` or signals, tested natively** | `host::vm_*` behind an interface with a bitset implementation; crash reporter compiled out. Game still boots to menu and plays on Linux. | **Low.** Purely internal; the campaign's own tests can gate it. |
| 2 | **Native WebGPU backend** | `rsx_renderer` on wgpu-native, WGSL shaders precompiled from the existing generator. Menus and a world render natively. | **Medium.** 100 Vulkan calls, dynamic state → pipeline cache. |
| 3 | **Wasm build, no display, to the menu** | `emcc` build of PPU+SPU+LV2, Memory64 or windowed map, futex→`emscripten_futex`, files over IDBFS+Range. Log shows the title reaching the main menu. | **High.** Memory model and function size are the unknowns. |
| 4 | **Full web page** | worker + ring buffer + AudioWorklet + canvas, playable. | **Medium** once 3 works (it is ps2-web-recomp's proven shape). |
| 5 | **Performance** | 30 fps target; measure wasm vs native. | **Unknown.** |

### Risks, ranked

1. **4 GiB address space** (jalon 3). The guest is exactly 4 GiB by design. Memory64
   raises the browser floor; the 32-bit path costs a translation on the hottest path.
2. **Function size in V8** — unmeasured until jalon 3. If lifted functions exceed
   what V8 will optimize, the fix is splitting generated functions, which a codegen
   change makes mechanical.
3. **Dynamic pipeline state → pipeline explosion** (jalon 2). Mitigation: a state
   cache like RPCS3's, but WebGPU's pipeline creation is expensive.
4. **Thread pool sizing** — the title creates host threads per guest thread; the
   Emscripten pool is fixed. Likely needs the ps2-web-recomp unwind-on-switch trick
   for at least the SPU threads.
5. **Runtime shader compilation** must become precompiled WGSL; any shader the
   title generates with parameters we did not foresee becomes a hole.

---

## 6. Recommendation

**Feasible, but it is a project, not a patch.** My estimate:

- **jalons 1 and 2 quietly: 3–5 days** for one engineer who knows this runtime
  (jalon 1 is small; jalon 2 is where the 100 Vulkan calls and the pipeline cache
  live).
- **jalon 3 is the real gate: 2–3 weeks**, dominated by the memory model, and it is
  where the project can fail. Everything after it is following a proven shape.
- **jalons 4–5: another 2–3 weeks** to a playable page.
- **Total: roughly 6–9 weeks** with the browser floor at Chrome 133+ (Memory64), or
  **8–12 weeks** if 32-bit only and address translation has to be built.

**The first jalon to launch is 1, not 3.** It is the only one that is both
independently valuable — a runtime with no `mprotect` and no signals is easier to
test, easier to embed, and removes two of the six non-portable dependencies with
zero behavioural change — and a hard prerequisite for the rest. Crucially it can
be **verified natively with the existing 51-test suite and the guarded-hour
regression**, so it costs no browser debugging to know whether it is right.

Do **not** start with the wasm build. The 4 GiB question is answerable only after
the memory layer is an interface, and starting at jalon 3 means debugging the
memory model, the threads, the shaders and the browser toolchain at once.
