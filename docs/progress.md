# Progress

Proof is a command and its output. A milestone without one is not done.

## Phase 0 -- the fork

### 0.1 Skeleton -- GREEN

**Criterion:** configuring without the game-derived inputs stops at once and
names the missing step.

```
$ cmake -S . -B build -G Ninja
CMake Error: No generated PPU code in .../out/ppu.
  python3 tools/setup.py --steps ppu --elf <decrypted title.elf>
```

Without the guard a GLOB over an empty directory produced a library with no
sources and a link error hundreds of lines later.

### 0.2 The game profile -- GREEN

**Criterion:** the runtime contains no title identity.

```
$ grep -rn 'BCES01741' runtime/ | wc -l
0
$ grep -cE 'BCES01741|"gowa"|GOWA\.SELF|God of War' runtime/src/main.cpp
0
```

Four leaks were silent ones: the VFS resolved the disc and update folders by a
literal title id, the launcher defaulted to that same path, the presenter named
its window after the game, and the cellSysutil cache path carried the id as a
fallback label. Each made every other disc fail quietly rather than loudly.

### 0.3 The empty profile boots a foreign title -- GREEN

**Criterion:** a title with no profile starts, runs and presents frames.

```
$ GOWA_HEADLESS=1 ./build/ps3base
I main: game profile: '(none)' -- generic runtime only
I loader: loaded mc (bin/mc/mc.elf) text 0x00010200 toc 0x00bd7998 entry 0x00bbe520
I spu: group_create 'MinecraftCellSpursKernelGroup' num 5
I ppu: _sys_ppu_thread_create 'MinecraftSpursHdlr1' entry 0x012313a4
I rsx: context at 0x40100000
I rsx: flip 841 (buffer 0)
```

841 flips, 4 PPU threads, 12 SPURS registrations, **0 crashes**, no stubs.

The earlier attempt at this measurement needed 46 no-op stubs and still
faulted, because two things install the God of War layer and only one was
gated: `hooks::init()` at startup and `install_builtin()` lazily from the first
SPU thread. Both now ask the profile.

**What the flips are:** `draw 0.0%`, `clear 0.0%`, `methods 0.0% (2k/s)`.
The game runs its loop and presents, but submits no geometry. That is the
next question, and it is the RSX front end, not the profile seam.

### 0.4 The GoWA profile still works -- GREEN

**Criterion:** the title that owns the special layer still selects it, and an
unknown name selects nothing.

```
$ ./profile_test
profiles: 1
find(gowa): yes
find(nope): no
first: 'gowa'
```

The check found a real defect: `namespace spu { void install_gowa_natives(); }`
sat inside the anonymous namespace that wraps the profile class, so it declared
a **different** `spu::` and the definition in `hle_builtin.cpp` was never
reached. Every build would have linked a profile whose SPU natives silently did
nothing. Declared at file scope, it resolves.

## Phase 1 -- what the old tree kept

### 1.1 The LV2 table, with ENOSYS -- GREEN

**Criterion:** every number the specification dispatches is accounted for, and
an unimplemented one refuses rather than succeeding.

```
$ ./lv2_enosys
lv2: 451 listed, 180 implemented (39.9%), 3 checks

$ python3 tools/lv2/gen_syscall_table.py
gen_syscall_table: 451 syscalls listed, 180 implemented (39.9%)
  process        5/17    29.4%
  ppu_thread    18/32    56.2%
  event_queue   10/10   100.0%
  filesystem     1/9     11.1%
  modules        0/43     0.0%
```

**The dispatch was returning a fabricated success.** In `ppu_runtime.cpp` an
unimplemented syscall logged a warning and then set `r3 = 0` -- `CELL_OK` from
something that did not run. Every caller believed its object had been created.
It now returns `0x8001003F` with the specification name of the number in the
log, which is what makes the gap actionable.

**Coverage is measured, not claimed.** The generator reads the LV2
specification for the numbers and names, then scans this tree's own
`LV2_SYSCALL(n, ...)` registrations to decide what is implemented. The first
run reported 0/451 because it was looking for a definition named after the
syscall and for `.c` files; this tree registers by number and its sources are
`.cpp`. Both fixed, and the numbers above are the real state.

### 1.2 The PS3 memory map, tested -- GREEN

**Criterion:** the map's guarantees about the PS3 address space are checked,
not assumed.

```
$ ./memory_map
memory: 22 checks passed (0 failed)
```

Covers what fails silently: two allocations must not alias, an allocation
honours its alignment, a store lands inside its own range and not in the
neighbour's, the big-endian accessors round-trip (a byte-swapped load still
looks plausible and is wrong), a range spanning an unmapped page is refused,
freeing an unknown address is refused, and immutability is per-range.

The test caught its own bad assumption first: it asserted that a range twice
an allocation's size is unmapped, but the allocator legitimately places the
next allocation there. The check now uses an address far from anything,
because a test that encodes a layout it does not control is a test that will
lie later.


## World creation: the wall at "Finalizing..." (rounds 18-85)

The crash that blocked world creation is **fixed**: `sys_vm_get_statistics` wrote
64 bytes into a 48-byte structure and clobbered the caller's saved registers.
Eight further defects were found and fixed on the same path:

| commit | defect |
|---|---|
| `0663ea9` | `sys_vm_memory_map` (300) missing -> null pool pointer -> crash |
| `d5f3ea5` | `sys_vm_get_statistics` 64 bytes instead of 48 |
| `611b678` | `sys_vm_return_memory` OK instead of `CELL_EBUSY` |
| `e9d4a54` | `sys_vm_append_memory` accepted 4 GiB behind a 64 MiB mapping |
| `28af660` | `pmem_total`/`pmem_used` are u32, not u64 |
| `f23af51` | `cellRtc` entirely absent (four functions) |
| `4857bd2` | `cellAudioOutSetCopyControl` missing |
| `ae01b76` | `pmem_total` reported vsize, so the title computed a bogus append |

The title now creates its world (`sys_vm_memory_map -> 0x100c0000`), the server
thread runs and exits cleanly, and no run of any length crashes.

**What remains**: the screen stays on "Finalizing..." with the bar full, and no
workload generating terrain ever reaches the SPUs. Measured, in order:

- the progress bar reaches 10140 green pixels and does not change over 15 samples;
- `cellSpurs` is fully initialised (60 calls, every one returning 0) and
  `cellSpursEventFlagSet` alone runs ~19922 times per run;
- SPUs are not dead: `spuIdling` cycles through `0x1f 0x0f 0x1e 0x17 0x1b 0x1d`,
  so at least one SPU is busy in about 31 % of samples, and the kernels execute
  tens of millions of instructions;
- the thread `(4J) Chunk update` waits on LV2 event flag `0x98046c00` for bits
  `0x3` while the single set on that flag carries `0x2`; bit 0 is never set by
  anything we execute, and `flag_match` matches RPCS3 exactly;
- no `UserAutoSave`, no world directory, no world textures and no chunk geometry
  are ever requested.

### A trap worth remembering

Three conclusions in this stretch were wrong **because the log was capped**:
`sys_fs_read` looked like a hang, `cellSpursEventFlagSet` looked like it was
never called, and the SPURS loop looked like it stopped at t=1.3 s. Each cap
(`n < 60`, `n < 30`) truncated the very evidence that contradicted the guess.
Sample a counter in time, and never read "the log ends here" as "the title
stopped".
