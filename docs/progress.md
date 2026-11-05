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
