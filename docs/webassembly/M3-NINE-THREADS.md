# With decoding enabled: nine threads, real file I/O, and a stall at the same address

## What the boot does now

The decoder fix from last round changed the shape of the run. Sampled second by second:

    0x005ec0ac  t=4s
    0x004e2064  t=6s      <- new address, not seen before
    0x005ec0ac  t=8s
    0x0021cf80  t=9s
    0x01401ee4  t=10s

**Five distinct program counters over ten seconds.** Before the fix the pc was pinned at
`0x003a18b0` for the whole window.

Threads created: **9** (`trophy_init_thread`, `trophy_event_thread`, `(4J) Tile update`,
`MSSAsync`, `(4J) Chunk update`, `(4J) Basic Event Handler`, plus the three boot threads).

## The thread dump is qualitatively different

    status: ppu 0x1000000 'main_thread' running, last 0
            (r3 0x32325488 r31 0x32325470)
            top syscalls 802x5590 141x426
    status:    regs r3 323254b0 r4 f2 r5 323283c0 ...

`main_thread` is **`running`**, not `blocked`. Its top syscall is **802 = `sys_fs_read`,
5,590 calls** — the title is reading its asset files, in bulk, successfully. Before the
decoder fix the same thread was blocked in `usleep` at 4 Hz.

That is the difference between "the emulator is stuck" and "the game is loading": the
thread now spends its time in `sys_fs_read` and `sys_fs_fcntl` rather than in a sleep.

## Where it settles

After t≈10 s the pc holds at `0x01401ee4` — the `sys_fs_stat` call site in `libfs`
identified two rounds ago. The syscall counter still climbs slowly
(`lwmutex_create` 737 → 965 across samples) and new syscalls appear
(`817 sys_fs_fcntl`), so the process is not frozen: it is in a stat/retry loop whose
condition the current file set does not satisfy.

## The honest read

This is the furthest the build has been and the most *normal* its behaviour has looked:
a rendering thread reading assets, nine threads alive, no crash. It has not reached the
menu, and it is now blocked on a filesystem query whose answer the mount cannot give —
most likely the same class of problem as the missing `USRDIR` two rounds ago: a file the
title expects that the mounted tree does not contain.

## What the next measurement should be

The stat loop's *success/failure ratio*, not just its call count. `sys_fs_stat` returns
ENOENT for the derived `*MipMapLevel2.png` names by design (only `terrainMipMapLevel2.png`
exists), so the question is whether the loop is waiting on one of those derived names —
which the guest would then synthesise — or on a genuinely missing asset.

That is a counter on the HLE, not a hypothesis.
