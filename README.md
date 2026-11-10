# ps3base v2

A generic static-recompilation runtime for PS3 titles.

The game's PowerPC code is translated to C++ ahead of time and compiled for
x86-64. The PS3 system libraries the title calls are lifted from the user's own
firmware or reimplemented, and the RSX command stream is drawn with Vulkan. It is
not an emulator: no CPU emulator runs at play time.

**It knows no game.** The runtime contains no title identity, no disc serial, no
hardcoded path. What a title needs beyond the generic machine arrives through a
game profile (`runtime/include/runtime/game_profile.h`); a build with no profile
gets the generic machine alone, which is the supported way to measure a foreign
disc.

This is a hobby project. It is not affiliated with Sony Interactive Entertainment,
and it contains no game code or data. You need your own copies.

## What runs on it

Measured, not claimed. Each title was lifted from its own disc and played on this
same runtime with no title-specific code except an optional profile:

| Title | Disc | Reached |
|---|---|---|
| Minecraft: PlayStation 3 Edition | BLES01976 | menus, world creation, gameplay, world generation; 99.9% of SPU code compiled ahead of time |
| Grand Theft Auto: San Andreas | BLES02219 | title screen, intro cutscene, in game at Grove Street with a full HUD |

Neither is finished. Their status lives in their own projects;
`docs/progress.md` here records what each one taught the runtime.

The port this tree was forked from (God of War: Ascension) still selects its layer
through `profile_gowa`, kept as the reference profile and as the regression that
Phase 0.4 checks.

## Status

`docs/progress.md` is the record: one section per milestone, each with the command
and the output that proves it. What it currently says:

- **Phase 0 -- the fork.** A build with no game-derived inputs stops at once and
  names the missing step. The runtime contains no title identity:
  `grep -rn 'BCES01741' runtime/` returns 0. A title with no profile starts, runs
  and presents frames.
- **Phase 1 -- what the old tree kept.** The LV2 syscall table is generated from
  the specification and reports its own coverage:
  `lv2: 451 listed, 180 implemented (39.9%)`. An unimplemented number returns
  `0x8001003F` with its name rather than a fabricated success.
- **The memory map, tested:** `memory: 22 checks passed (0 failed)`.

The full build currently also carries a WebAssembly milestone track; see
`docs/PORT-WEB-PROMPT.md`.

## Setup

ps3base compiles nothing by itself. The code it runs is generated from **your
own** copy of a disc and of the firmware. None of that material is tracked.

```bash
python3 tools/extract_keys.py <rpcs3>/rpcs3/Crypto/key_vault.cpp keys/self_keys.json
python3 tools/decrypt_all.py bin                 # your SELF payloads -> plain ELF
python3 tools/setup.py --steps ppu --elf bin/<title>/EBOOT.elf
python3 tools/setup.py --steps spu --elf bin/<title>/EBOOT.elf
```

`bin/`, `fw/`, `keys/` and `out/` are git-ignored. `docs/setup.md` has the full
sequence, including how the decryption works and why the KLIC is never stored.

To lift a second title without touching the first, point the generated-code and
build directories somewhere else:

```bash
python3 tools/setup.py --steps ppu --images recomp/images-<title>.json \
    --elf bin/<title>/EBOOT.elf --out-root out-<title>
cmake -S . -B build-<title> -G Ninja \\
      -DPS3BASE_GENERATED_DIR=$PWD/out-<title>/ppu \\
      -DGOWA_SPU_GENERATED_DIR=$PWD/out-<title>/spu
```

## Building

```bash
cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \\
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build -j 8 --target ps3base
```

clang only, RelWithDebInfo only, and `-j 8` at most: this machine is shared, and
higher values make it unusable for anything else.

## Known defects

- The LV2 surface is about 40% implemented. Everything else refuses explicitly
  rather than pretending.
- `f_0064c044` crashes Minecraft's `(4J) Server` on long sessions: a corrupted
  linked-list pointer read as if it were a node. The writer is unidentified.
- Loading an existing Minecraft world returns `-2` from the file callback after
  `THUMB`.

## Documentation

| File | What it answers |
|---|---|
| `docs/progress.md` | what each milestone proved, with the command and its output |
| `docs/decisions.md` | why the structure is this way (D-001 to D-006), and the abandon criterion |
| `docs/RUN.md` | the exact command that launches a run, and how to judge one |
| `docs/setup.md` | discs, firmware and key material -> liftable sources |
| `docs/REFERENCES.md` | where a fact comes from: the file that answers a question |
| `docs/forcings.md` | every active forcing, as a debt to be replaced |
| `docs/PLAY.md`, `docs/recon/` | the Minecraft and GTA San Andreas tracks |
| `docs/PORTING.md`, `docs/RECIPE.md` | what porting a title taught, and the recipe for the next one |

## Licence

MIT, see `LICENSE`. It covers the code in this repository only, not any game.
Third-party components keep their own licenses, see `THIRD_PARTY_NOTICES.txt`.
God of War, Minecraft and Grand Theft Auto are trademarks of their respective
owners; none of their code or data is included here.
