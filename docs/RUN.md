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

## Known state at this commit

The title screen and menu render and are navigable. All text — buttons, prompt,
diagonal splash — renders as noise while textures loaded from the disc archive
are correct. That points at the glyph atlas path, which is the next thing to fix.
