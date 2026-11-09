# Turn 326: the hash pair, the crash, and where the time goes

## 1. The identical-hash comparison, by loading one saved world in both modes

The seed cannot be typed (see docs/findings/osk-and-seed.md), so this compares
two runs that **load the same world file** instead of two that generate one from
a seed. That is the question the campaign actually asks: does the compiled path
produce the same result as the interpreter from identical input?

Reference world: `bca74847ece62a6a697e4d26631958a1b6e8df5b174ce3ec6671fde7737b21a2`
(3 415 111 bytes), created once with the compiled build.

Two copies of that VFS, one loaded with the compiled binary, one with
`GOWA_SPU_NO_AOT=1`. The load path was driven by reading the screen
(Play Game -> Continue playing offline -> select the world -> Load), and both
runs reached a playable world:

- compiled: 23 fps, world visible, hearts and hotbar;
- interpreted: 9 fps, same world, same spawn.

The compiled run then saved through the pause menu (Save Game -> OK on the
overwrite dialog) and wrote

    ecc79cb20e3def74fb9aad552cbf7f9d34420a86788306b9dcd5df5f91b5a73d
    3 549 175 bytes

**The interpreted run crashed before it could write its own save**, at the same
address and on the same thread as the known bug:

    E crash: signal 11 at exe+0x25b990a, thread (4J) Server

## 2. The crash is not the compiler -- now measured, not assumed

The same `exe+0x25b990a` / `(4J) Server` crash happens with
`GOWA_SPU_NO_AOT=1`, where **every SPU instruction is interpreted**. A bug in
compiled SPU code cannot exist on a path that runs no compiled SPU code at all.
That settles the question the previous turn left open.

The codewatch was armed on the crash node's line for a full generation
(`PS3BASE_CODEWATCH=1 PS3BASE_CODEWATCH_ADDR=0x378c9db0`) and produced **zero
line-writes** on it, because the node address changes every session: the
address comes from the guest heap and is not stable across boots. The watch
did report 60 `CODE WORD` stores, all into one object at `0x301c1xxx`, none of
them the pointer-replacing signature (`prev` was never a heap pointer).

**So the writer is still unnamed.** What is established is stronger than before:
the crash survives with zero compiled SPU code, and `GOWA_SPU_SHADOW=1` showed
zero divergences on the runs that reached it. Both point at the PPU/HLE side.

## 3. Where the time goes during generation

Not measured this turn: the generation runs that were timed took priority, and
`perf record -g` needs a run that is not competing with a measured baseline.
The generation cost itself is measured and stable:

| mode | post-processing start | autosave | generation |
|---|---|---|---|
| interpreted | 35.83 s | 255.94 s | 220.11 s |
| compiled | 41.41 s | 231.76 s | 190.35 s |

and in this turn the same comparison appeared again on the codewatch run
(86.07 s -> 1138.44 s, slowed by the watch itself).

## What is still missing

- The interpreted run has to reach a save for the pair to be complete. It
  crashed at t=579 s while the save dialog was open.
- The profile.