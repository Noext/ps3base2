# Options load, and the frame count doubles

## The VFS fix

`vfs::init()` resolves its storage through environment variables
(`PS3BASE_VFS_DIR`, `GOWA_HDD0_DIR`), and **`getenv` is inert in the wasm build** — so the
runtime fell back to the relative path `vfs`, which did not exist in the preloaded image.

The guest said so plainly:

    savedata: UserAutoLoad ... 'BLES01976-OPTIONS' ... -> -2147306485   (0x8002B40B, no data)

while native, on the same line, has `(existing)`.

Placing the savedata tree at the path the runtime actually looks for
(`<mount>/vfs/dev_hdd0/home/00000001/savedata`) fixes it:

    savedata: load 'BLES01976-OPTIONS' (existing) stat result 0
    savedata: read BLES01976-OPTIONS/OPTVER  offset 0x0 size 0x2   -> 0x2
    savedata: read BLES01976-OPTIONS/OPTDATA offset 0x0 size 0x3cc -> 0x3cc
    savedata: UserAutoLoad user 0 'BLES01976-OPTIONS' userdata=0x0 errDialog 0 -> 0

**`-> 0`: success.** The title's option block loads, which is a precondition for the
menu reaching its populated state rather than an error path.

## The frame count doubled

| | before | after |
|---|---|---|
| frames flipped | 6,301 | **11,161** |

With the option data available, the title keeps rendering for roughly twice as long
inside the same wall-clock window instead of taking the no-data branch.

## Where the build stands

| | |
|---|---|
| **frames rendered** | **11,161** |
| draw / clear commands | 3,000/s / 1,000/s |
| worst frame | ~45 ms |
| PPU threads | 16 |
| options loaded | **yes** (`UserAutoLoad -> 0`) |
| PNG decodes | 101+ |
| crashes | 0 |
| main menu log line | **not present** |

## What is still missing for the gate

The criterion is a **log line** showing the title reaching the main menu. The build renders
eleven thousand frames, loads its options, mounts its DLC and runs sixteen threads — and
still does not emit that line.

Two candidate explanations, both testable:

1. The title is at the menu and simply does not log it (the criterion is a log line, and a
   guest that renders a menu silently satisfies the *behaviour* but not the *criterion*).
2. The title is still in a loading/polling state whose visuals happen to render.

Distinguishing them needs the frame content, not more syscall counts: the RSX path is
stubbed, so nothing is captured, but the draw-command stream can be inspected for
menu-shaped traffic (large textured quads, text draws) versus loading-shaped traffic.

## Honest position

Milestone 3 is **not** passed: the log line is absent. This round fixed a real VFS defect
(options were failing), doubled the rendered frames, and pinned the remaining gap to one
question that is about what the guest is *drawing*, not what it is *waiting for*.
