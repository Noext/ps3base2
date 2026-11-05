# Setup

ps3base v2 compiles nothing by itself. The runtime is generic; the code it runs
is generated from **your own** copy of a disc and of the firmware. None of that
material is tracked by git.

## What you supply

| Path | What it is |
|---|---|
| `bin/<title>/` | the decrypted executable of your disc, as a plain ELF |
| `fw/<version>/` | the decrypted `.prx` modules of your own firmware dump |
| `ref/nid_db.json` | the NID database, regenerable from RPCS3 sources |

Nothing decrypts anything here, and no key is stored or accepted. Producing
those files is your own business, done with your own tools.

## Sequence

```bash
python3 tools/nid_db.py <rpcs3>/rpcs3/Emu/Cell ref/nid_db.json
python3 tools/setup.py --steps ppu --elf bin/<title>/game.elf
python3 tools/setup.py --steps spu --elf bin/<title>/game.elf
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build -j 8
```

`setup.py` writes `out/ppu/`, `out/spu/` and `out/gen/`. If they are absent,
`cmake` stops immediately and names the missing step rather than failing at
link time.

## Firmware modules are lifted, not emulated

`tools/prx_relocate.py` places each firmware module at a fixed base and
`recomp/ppu/codegen.py` lifts it like any other guest image. The LV2 syscall
surface is the exception: it is reimplemented in `runtime/src/lv2/`, generated
from the LV2 specification so that every number is accounted for.
