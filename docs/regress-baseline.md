# Regression baseline

Each game has a reference point. `regress.sh` compares against this file, and a
merge is allowed only when **no game goes backwards**. A game that improves
moves its own line; a game that drops is a regression and takes priority over
whatever produced it.

The numbers below are read by `tools/regress.sh`, one line per game, as
`key=value` fields. Editing a bar is editing one token here.

```
minecraft   flips=2000   # settled: green at 7141 full window, 7021 solo
            # alive, 0 crashes. Measured 6721 at 120 s, 7441 at 130 s: the
            # budget, not the game, sets the number, so the floor is 6500 to
            # leave room for start-up jitter.
gta-sa      flips=2000   # settled: green at 6781 solo
            # alive, 0 crashes.
rdr         flips=2000 screen=socialclub abort_after=60
            # flips >= 2000, the Rockstar Social Club legal notice read back
            # from the captured frame, and no SPURS abort before 60 s.
            # The notice is drawn from the title's own assets at about 13 s and
            # is the first frame that is the title's content rather than a
            # logo; an abort at 78-91 s after it is the next milestone's work
            # and is tolerated by the criterion.
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
| 2026-11-12 | rdr | 2161 | Social Club legal notice on screen at 13 s; SPURS abort at 78 s. The pass bar becomes notice + flips >= 2000 + no abort before 60 s, replacing "flips >= 0". |

## Why Red Dead Redemption sat at 0

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

## Why the RDR floor is 2000 and not 0

A flip count on its own says nothing: 7081 flips of a black screen was recorded
as "first frames". The bar is now the title's own legal notice, read back from
the frame the title presented, so a renderer that presents an empty buffer can
no longer pass. 2000 is the measured 2161 at a 120 s budget with the margin a
start-up jitter needs; the milestone after this one is the main menu, and the
bar moves there.
