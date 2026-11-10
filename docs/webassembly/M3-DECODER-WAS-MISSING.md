# The decoder was missing, and the boot moves again

## The root cause

The boot froze in `libfs` at a `sys_fs_stat` loop. A probe on the failing paths named
them:

    fsprobe: stat '.../font/Mojangles_7MipMapLevel2.png' -> ENOENT
    fsprobe: stat '.../misc/pumpkinblurMipMapLevel2.png' -> ENOENT

Only one `*MipMapLevel2*' file exists on the disc (`terrainMipMapLevel2.png`), so those
stats fail legitimately in **both** runs. The difference was what happens after:

| | PNG decodes |
|---|---|
| native | **202** |
| wasm | **0** |

**The wasm build never decoded a single image.** The reason is `runtime/src/hle/cellImageDec.cpp`
— the `cellPngDec`/`cellJpgDec` HLE — which **was never compiled for wasm** because its
`#include "stb_image.h"` was not on the include path. A title that cannot decode a PNG
treats every texture as missing, retries the stat, and waits.

## The fix

`third_party/stb/stb_image.h` is in the tree. Adding `-I third_party/stb -I third_party`
compiles `cellImageDec.cpp` (28 KB object) and `stb_image_impl.cpp` (42 KB) for wasm with
no source change at all.

## The effect

| | before | after |
|---|---|---|
| PPU threads created | 7 | **9** |
| PNG decodes | **0** | **101** |
| `pngdec: ReadHeader` / `DecodeData` | never | `h93..h103`, sizes from 16x320 to 256x256 |
| `main_thread` pc over time | frozen at `0x01401ee4` | **advancing**: `0x01215e5c` at t=8s, `0x0021cf80` at t=9s |

**The boot moves again.** It decodes the title's textures (fonts, UI art, terrain), creates
the event handler threads, and the program counter advances instead of sitting on one
instruction.

## Why this mattered more than it looks

Every round since 25 analysed the freeze as an emulator problem — a deadlocked
reservation, a wrong thread, a throughput gap. **It was a missing translation unit.**
The HLE that turns PNG bytes into textures was not in the link, so the guest's file
probes failed, its texture loads returned errors, and its loader polled.

The three lessons, now measured rather than asserted:

1. A missing HLE file is silent: no error, no crash, just a guest that retries.
2. `LOG_DEBUG` failures are invisible; a compile-time probe on the failing path is what
   named the cause.
3. The include path is part of the build. `cellImageDec` was "implemented" for four
   rounds and never compiled.

## Honest position

Milestone 3 is **not** reached: the menu is not up. What changed is that the boot is
**moving** through its asset-loading phase at 9 threads and 101 decoded textures, with
the PC advancing — the furthest this build has been, and the first time the engine has
been shown to decode real game textures under wasm.

## A note on the shared tree

The last verification showed **1 native build error** unrelated to this round:

    hooks.cpp:(.text+0x6176): undefined reference to `ppu::hook_flags`

`git stash` + rebuild reproduces it identically, so it **predates my changes**. The
working tree also contains modifications I did not make — `recomp/ppu/codegen.py`,
`CMakeLists.txt`, `runtime/include/ppu/ops.h`, `runtime/src/hooks.cpp`,
`tests/hook_flags.cpp` — which is a parallel effort on the per-image hook-flag table.

`out/ppu/table.cpp` (generated, dated Nov 5) does **not** define `hook_flags`, so the
declaration in `ops.h` has no storage until that codegen change lands. That is the
other track's work; I am not editing it, and I am recording it here so the next reader
knows the native link error is expected and owned elsewhere.

**My own change in `runtime/src/lv2/fs.cpp` (the `PS3BASE_FS_PROBE` block) is present and
committed**, and the wasm build links and runs with the decoder fix.
