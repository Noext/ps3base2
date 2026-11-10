# The 70-second gap was the disc content, and it is fixed

## The finding

The native run's 70-second gap is not idle work and not a mystery: it is **loading the
game's assets**.

    fs: open '/dev_bdvd/PS3_GAME/USRDIR/Common/Media/MediaPS3.arc' flags 00 -> 3
    sys_memory_allocate(0x1000000) -> 0x30400000
    prx: load libpngdec / libpngenc / libjpgenc ... cellPngDec: Create -> handle 1

The wasm image only carried `bin/` and `fw/` (the executables and firmware). **It had no
`PS3_GAME/USRDIR` at all** — 96 MB of textures, models, sounds and the archive that
holds them.

The guest said so plainly, in its own log:

    W fs: open '/dev_bdvd/PS3_GAME/USRDIR/PS3/PS3ProductCodes.bin' -> ENOENT
      (host '/PS3_GAME/USRDIR/PS3/PS3ProductCodes.bin')

and the native run, on the same line, has `-> 3` (success) because the file was there.

## The effect of fixing it

Mounting the disc tree changed the boot immediately:

| | before | after |
|---|---|---|
| PPU threads created | **3** | **7** |
| trophy_init / trophy_event | absent | **created** |
| `(4J) Tile update`, `MSSAsync` | absent | **created** |
| `MediaPS3.arc` | never opened | **opened, `-> 3`** |
| `colours.col` | never opened | **opened, `-> 3`** |
| syscalls at t=30 s | 856 | **8,470** |
| `main_thread` pc | `0x003a18b0` (worker loop) | `0x01401ee4` (`libfs`) |

**The game is no longer stuck in a worker loop.** It creates the trophy threads, opens
the asset archive, loads resources, and now waits inside `libfs` — a file-system wait,
which is the normal consequence of asking for assets and having them arrive.

## What this corrects

Four rounds of diagnosis pointed at the emulator: a missing SPU model, a deadlocked
reservation, a wrong thread. **The actual cause was that the build had no game data to
load.** Every measurement along the way was accurate — the threads, the waits, the pc
addresses — and every one of them was downstream of a directory that was not mounted.

The lesson is one I have had to learn repeatedly in this campaign: when the guest stops
at a *file-system* boundary, look at the files before looking at the emulator.

## Where the boot now stands

    I fs: open '.../Common/Media/MediaPS3.arc' -> 3
    I _sys_ppu_thread_create 'trophy_event_thread' -> 0x1000005
    I _sys_ppu_thread_create '(4J) Tile update' -> 0x1000006
    I _sys_ppu_thread_create 'MSSAsync' -> 0x1000007
    I fs: open '.../Common/res/TitleUpdate/res/colours.col' -> 3
    ps3base-wasm: t=30s pc=0x01401ee4 syscalls=8470 exited=0

Seven guest threads, real assets loaded, no crash, and a new wait inside `libfs` at
`0x01401ee4`. That is the furthest the wasm build has ever reached.

## Honest position

Milestone 3 is still **not** reached — the menu is not up. But the picture changed
qualitatively this round: the wasm build now performs the same asset-loading sequence as
the native run, and the remaining wait is in the file-system module rather than in the
emulator's core.

The next measurement is what `libfs` is waiting for at `0x01401ee4` — most likely the
FIFO thread's read completion, which returns to the RSX stub question.
