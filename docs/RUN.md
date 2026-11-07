# Running the game

## The command

```
cd /root/Dev/ps3base2

DISPLAY=:0 \
XDG_RUNTIME_DIR=/run/user/0 \
PS3BASE_DISC_DIR=/root/Dev/mc-port/disc \
PS3BASE_LOG=info \
PS3BASE_DUMP_FRAMES=200 \
PS3BASE_FRAME_DIR=/tmp/frames \
./build/ps3base --no-launcher
```

`--no-launcher` skips the settings window and boots the disc directly. Drop it
to get the launcher (which is where the disc folder is picked and saved).

## Environment, and the two that must NOT be set

| Variable | Meaning |
|---|---|
| `PS3BASE_DISC_DIR` | Host path of the mounted disc. Without it every `/dev_bdvd` path resolves to `/PS3_GAME/...` and the title finds none of its files. |
| `PS3BASE_LOG` | `error`, `warn`, `info`, `debug`, `trace`. `info` is what the measurements in this repository were taken with. |
| `PS3BASE_DUMP_FRAMES` | Write every Nth presented buffer to disk. See *Looking at a frame*. |
| `PS3BASE_FRAME_DIR` | Where those frames go. Default `frames`. |
| `DISPLAY`, `XDG_RUNTIME_DIR` | Needed for a window and for the Vulkan loader. Use the real `/run/user/<uid>`, not a scratch directory. |

**Do not set `VK_ICD_FILENAMES`.** Without it the runtime picks the machine's
GPU (here: NVIDIA RTX 4060). Forcing `lvp_icd.json` selects lavapipe, the
software Vulkan implementation, and the run behaves differently.

**Do not set `GOWA_HEADLESS`.** It disables the Vulkan presenter, so the guest
display buffer is never written back and every dumped frame is black. A black
frame under this variable means "no renderer", not "nothing drawn".

## Looking at a frame

`PS3BASE_DUMP_FRAMES=200` writes a `.ppm` per 200th flip, from the exact buffer
the swapchain is fed: `LOCAL_BASE + offset`, `pitch` bytes per row, XRGB8888
big-endian. Each dump logs how many pixels were non-black:

```
frame: flip 0 1280x720 nonblack 0/921600 (0.00%)
```

Under `GOWA_HEADLESS` that number is always zero, for the reason above.

## Judging a run

**Do not grep for "crash".** The string appears on every line of a stack dump,
so a single event produces a dozen matching lines and looks like a dozen
crashes. Judge mechanically:

- **Alive**: the process reaches the timeout (exit 124) and the flip counter
  increased. A run is productive even when it is killed by the timeout.
- **Crashed**: the process ended early with a signal (139 for SIGSEGV, 134 for
  SIGABRT) or a non-zero code.

Count real events, never matches:

```
grep -ac 'crash: signal' log      # events, one line each
grep -a  'rsx: flip' log | tail -1
```

Some `crash: signal` lines are **repaired** page faults: `watch_fault` handles
them and execution continues. They are not failures. The reliable signals of a
dead run are the exit code and a flip count that stops rising.

## Generated files

| Path | Needed? | Notes |
|---|---|---|
| `shader_cache/*.spv` | no | Compiled shaders, keyed by hash. Regenerated on demand. |
| `shader_cache/pipelines.bin` | no | Vulkan pipeline cache. Regenerated. |
| `out/ppu`, `out/gen` | **yes** | Lifted guest code. Produced by `tools/setup.py`; the CMake configure fails with a message if missing. |
| `fw/4.91/reloc/*.elf` | **yes** | Relocated firmware modules, including `libspurs_jq`. Same generator. |
| `bin/mc/mc.elf` | **yes** | The decrypted disc executable. |
| `frames/`, `*.ppm` | no | Frame dumps. |
| `spu_*.ls` | no | SPU local-store dumps from a crash capture. |

Deleting the shader cache is safe. Deleting `out/`, `fw/` or `bin/` requires
re-running `tools/setup.py`.

## Building: never trust an incremental tree

```
cd /root/Dev/ps3base2
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build -j 8 --target ps3base
```

`-j 8` is a hard cap: this machine is shared and higher values make it
unusable for anything else.

**If the binary crashes and a fresh tree does not, the tree was stale.** This is
not hypothetical: a `build/` tree carried through dozens of edits, reverts and
re-applies produced a binary that segfaulted 5 times out of 5 in
`std::ostream::sentry`, while the same commit built into a clean tree ran to
the timeout 5 times out of 5 with 661-841 flips each. Nothing in the source
differed. The stale tree had a binary roughly a third of the correct size, which
is the tell:

```
90862096  build/ps3base      # stale: mixed objects and headers
263547760 build/ps3base      # clean
```

Before investigating a crash, do `rm -rf build` and rebuild. The sanitizer
builds (`build-asan`, `build-ubsan`, `build-tsan`) are configured separately
and are only for locating a fault, never for judging a run.

## Verified alive

The five-run check that a change has not broken anything:

```
for i in 1 2 3 4 5; do
  DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/$UID \
  PS3BASE_DISC_DIR=/root/Dev/mc-port/disc PS3BASE_LOG=info \
  timeout 50 ./build/ps3base --no-launcher > /tmp/r$i.log 2>&1
  echo "run $i exit=$? $(grep -a 'rsx: flip' /tmp/r$i.log | tail -1 | grep -oE 'flip [0-9]+')"
done
```

Five `exit=124` with a rising flip count is a pass.

## Driving the menus without a keyboard

The runtime polls SDL for the pad, and `SDL_GetKeyboardFocus()` does not return
our window under Xvfb or a bare WM. Synthetic X key events therefore never reach
`cellPadGetData`, which is why navigating by `xdotool key` is unreliable: the
selection appears to drift and the same menu is re-entered.

Two mechanisms inject at the pad frame instead, independent of the window:

```
GOWA_AUTOPAD='cross@44:0.4,cross@54:0.4,...'   # fixed timeline, brittle
PS3BASE_INPUT_CMD=/tmp/icmd                     # one command per file write
```

`PS3BASE_INPUT_CMD` is the reliable one: the runtime reads the file at the next
frame, truncates it, and holds the button for about four frames. Write a button
name into the file and nothing else:

```bash
rm -f /tmp/icmd; touch /tmp/icmd
echo cross > /tmp/icmd     # Cross / "X Select"
echo down  > /tmp/icmd     # d-pad
echo up    > /tmp/icmd
echo circle > /tmp/icmd    # Circle / "Back"
echo start > /tmp/icmd
```

Available names are the table in `runtime/src/hle/cellPad.cpp`: `select`,
`start`, `up`, `down`, `left`, `right`, `triangle`, `circle`, `cross`,
`square`, `l1`, `r1`, `l2`, `r2`, `l3`, `r3`.

### Verified sequence to "Create New World"

Drive one command at a time and capture between each; the PSN dialogue can
reappear after "Play Game" and has to be closed again each time.

| screen | what is highlighted | command |
|---|---|---|
| TERMS OF USE | Accept | `cross` |
| autosave notice | OK | `cross` |
| main menu | Play Game | `cross` |
| "Online ID currently not online" | Connect to PSN | `down` then `cross` |
| Start Game / Join Game | Create New World | `cross` |
| world creation | World Name (text field) | `down` |
| world creation | Game Mode | `down` |
| world creation | Texture Packs | `down` |
| world creation | Difficulty | `down` |
| world creation | More Options | `down` |
| world creation | **Create New World** | `down`, then `cross` |

From World Name the button list is Name → Seed → Game Mode → Texture Packs →
Difficulty → More Options → Create New World. The list wraps, and a text field
that has focus swallows a `down`, so count the captures rather than the presses.

If the PSN dialogue is on screen when you expected the main menu, send
`down` + `cross` and continue; it does not block progress, it only costs a
round trip.

Validation is mechanical: the log then shows

```
savedata: UserAutoSave v 0x1 user 0 ...
savedata: write u0_BLES01976-OPTIONS/ICON0.PNG  size 0x100e4
savedata: write u0_BLES01976-OPTIONS/OPTVER     size 0x2
savedata: write u0_BLES01976-OPTIONS/OPTDATA    size 0x3cc
```

and the files exist under
`vfs/dev_hdd0/home/00000001/savedata/u0_BLES01976-OPTIONS/`.

## Known state at this commit

The text renders legibly (commit `b20cf58`). The main menu, the PSN dialogue,
Start Game, and the full world-creation screen are all reachable and driven by
`PS3BASE_INPUT_CMD`. Creating a world writes its save. The game's internal
server then faults storing through a null pointer; that is the current wall.


## Forced run (reach the world)

PS3BASE_FORCE is inert unless the variable is set, so a normal run is unchanged.
This is the recipe that gets past the server's 66 ms exit and into spawn-area
generation:

```bash
DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 \
PS3BASE_DISC_DIR=/root/Dev/mc-port/disc \
PS3BASE_LOG=info \
PS3BASE_FORCE="f216258=1" \
PS3BASE_INPUT_CMD=/tmp/live.txt \
timeout 1200 ./build/ps3base --no-launcher
```

`f216258=1` overrides r3 to 1 on return from the lifted function at 0x216258
(`f_00216258`), which is the byte read at `CellServer+593` that otherwise ends
the server loop after 66 ms. The list of active forcings and what each one
unblocks lives in `docs/forcings.md`; each is a debt to be replaced by the real
fix.
