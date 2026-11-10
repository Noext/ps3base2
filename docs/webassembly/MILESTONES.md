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

### What is not yet true

- `crash::install()` is skipped at **runtime**, not compiled out. For a real wasm
  build the crash reporter must leave the link line entirely; that is milestone 3
  work, and the prompt's criterion ("symbols no longer referenced") is met today
  only in the sense that the code path does not run.
- Guard enforcement in bitset mode is a *registered range*, not yet consulted by
  the PPU store path. The prompt asks for a software check that logs thread, r1,
  lr and the lifted function; wiring that check is still open and is the first
  task of the next round.

---

## Milestones 2-5

Not started. Milestone 2 (native WebGPU backend) is next once the guard check
above closes milestone 1 completely.
