# Regression baseline

Each game has a reference point. `regress.sh` compares against this file, and a
merge is allowed only when **no game goes backwards**. A game that improves
moves its own line; a game that drops is a regression and takes priority over
whatever produced it.

```
minecraft   flips >= 6780   alive, 0 crashes
gta-sa      flips >= 6300   alive, 0 crashes
rdr         flips >= 0      alive, 0 crashes    best known: engine threads run
```

## History

| date | game | flips | what changed |
|---|---|---|---|
| 2026-11-10 | minecraft | 6661 | hook_flags per image |
| 2026-11-10 | minecraft | 6781 | allocator seeded with image ranges |
| 2026-11-10 | gta-sa | 6301 | cellSync2 |
| 2026-11-10 | gta-sa | 6481 | allocator seeded with image ranges |
| 2026-11-10 | rdr | 7081 | first frames, black screen (no geometry) |
| 2026-11-10 | rdr | 0 | after the proven-seven LLE list; engine threads run, renderer idle |
| 2026-11-11 | rdr | 0 | five syscalls now match RPCS3; install archive still forced |

## Why Red Dead Redemption sits at 0

The title reaches its own engine: RAGE netThreadPool, DVD Reader and Streamer,
HDD threads, LoadThread, FreeSpaceCheckThread, the SPURS handlers, the cellGame
data checks and content permit, and the 181 MB startup heap. It then creates its
worker threads through the OPD table at 0x01356420 and each one exits
immediately with _sys_ppu_thread_exit(0xdeadc0de).

That exit is NOT an error and NOT a runtime sentinel. The value is built by two
instructions in the title's own code at 0x005331c0:

    lis  r3, 0xdead
    oris r3, r3, 0xc0de
    bl   _sys_ppu_thread_exit

so a search for the constant finds nothing in the repository, in the generated
code, or in the binary: the word never exists in the file, only in a register.
The thread has finished its work, freed its network context
(sys_net_free_thread_context) and is leaving. The defect is upstream: the worker
finds no work item, so the pool is recreated every second and dies again.

Forcing: rdr2_installed.sdat is pre-placed under dev_hdd0 because the title's
install step never runs. See docs/forcings.md.
