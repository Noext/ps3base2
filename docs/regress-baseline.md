# Regression baseline

Each game has a reference point. `tools/regress.sh` compares against this file
and against a reference binary, and a merge is allowed only when **no game goes
backwards**. A game that improves moves its own line; a game that drops is a
regression and takes priority over whatever produced it.

## The comparison rule

**Every comparison is an A/B on the same machine within the same minute.** The
script runs the reference binary (the pristine main checkout's build) and the
candidate binary back to back on the same display, and compares the candidate to
the reference, not to a number measured days ago.

This is a rule and not a preference because the failure it prevents already
happened: two runs of the *same* binary minutes apart were read as a regression
while other agents' builds moved the machine's load. Measured on the same
evening: the same minecraft binary gave 5881 and 5641 flips in two adjacent
minutes, and gta-sa gave 5401 and 5821. That spread is the machine, not the
code, and a bar that does not know about it produces false alarms.

The numbers below are read by `tools/regress.sh`, one line per game, as
`key=value` fields. Editing a bar is editing one token here.

    minecraft   flips=5000 attempts=2
                # Floor used only when no reference binary is available.
                # Measured 2026-11-12 at a 120 s budget, idle-ish machine:
                # reference binary 5881, this tree's binary 5641, back to back.
                # The budget, not the game, sets the number.

    gta-sa      flips=4500 attempts=2
                # Floor used only when no reference binary is available.
                # Measured 2026-11-12 at a 120 s budget: reference 5401,
                # candidate 5821, back to back, both alive, 0 crashes.

    rdr         flips=1000 screen=AFTERLOADING screen_attempts=6 metric=screen input=drive_rdr
                # The TITLE SCREEN: RED DEAD REDEMPTION with "Press START" on
                # black, drawn from the title's own assets. Measured 2026-11-12:
                # 601 flips, title at ~22 s, SPURS abort at ~23.4 s.
                # 2026-11-13: the bar is now the game's own ALERT screen,
                # not the title. Two fixes together (a real dev_flash tree and
                # the RoundGuard rounding mode) took RDR from 601 flips and a
                # SPURS abort at 23 s to 1321 flips, zero refusals, zero abort,
                # and the save prompt: "A Red Dead Redemption single player save
                # could not be found". The marker is the word ALERT.
                # The marker is written without its space because this file is
                # parsed by whitespace; the comparison drops whitespace from
                # both sides, so "PressSTART" matches the screen's "Press START".
                # The screen is read from a 30 fps recording of the run by
                # tools/frame_marker.py, not from a poll: the title is up for
                # about a quarter of a second, and a two-second sampler steps
                # over it (66 window grabs over 23 s, no hit; a 30 fps
                # recording catches eight frames of it).
                # The abort after the title is the next milestone's work and is
                # not part of the criterion; what is judged is that the title
                # screen was reached and that the flip count did not go
                # backwards against the reference measured in the same minute.
                #
                # metric=screen: the flip count is NOT the bar for this title, and
                # that is measured, not a judgement call. Three consecutive pairs
                # of the SAME reference binary gave 601, 601 and 721 flips with
                # aborts at 22.8 s, 23.3 s and 35.2 s -- the abort time sets the
                # flips, so a flips bar fails unchanged code. The screen is the
                # far point; flips stay as an absolute floor of 500 so a title
                # that stops drawing cannot pass by reaching a black screen.
                # screen_attempts=6 because the title wins the race against the
                # abort in 3 runs of 8 on this loaded machine (measured); the
                # script stops at the first hit, and each attempt is still a
                # same-minute reference/candidate pair.
                # 2026-11-13 (later): the criterion is the screen AFTER the
                # loading, not the loading screen itself. "EXODUS IN AMERICA"
                # is a LOADING card: three 900 s runs reached it and then sat on
                # a green screen at 1-2 fps forever, so a bar that stops at the
                # card reports a success that is not one. See
                # docs/findings/f-rdr-loading-screen.md.
                #
                # The marker is AFTERLOADING, which no screen in the current
                # build produces: the loading never completes. That is the
                # point -- the row now FAILS, honestly, instead of passing on
                # the card. When the loading completes, the marker becomes the
                # first frame of the opening cinematic.
                #
                # input=drive_rdr.py: regress.sh sends no input, so the title
                # never leaves "Press START" and NO screen past the title is
                # reachable. Measured: rdr-ref1 alive flips=2461 with
                # "miss: 0 OCR'd" on every attempt. The row therefore cannot be
                # judged by a bare regress.sh run until regress.sh drives the
                # pad; tools/rdr_ab.sh does, and that is the harness to use for
                # this title meanwhile.


## What changed and why

`screen=socialclub` and `abort_after=60` are gone. Both were stale:

- the Social Club legal notice is *earlier* than the title screen and is not the
  far point of the port any more; the title screen is;
- `abort_after=60` failed a tree identical to main. The reference binary built
  from 22d2a0c aborts at 23.0 s and this tree aborts at 23.2 s, so a 60 s
  deadline could only ever report a regression that did not exist. The abort
  time is also not a function of wall clock alone: with a heavy frame dump
  enabled the same title aborted at 45.6 s, and with a root-window grab loop at
  54 s, because the stop is driven by guest progress.

## History

| date | game | measured | what changed |
|---|---|---|---|
| 2026-11-10 | minecraft | 6661 flips | hook_flags per image |
| 2026-11-10 | minecraft | 6781 flips | allocator seeded with image ranges |
| 2026-11-10 | gta-sa | 6301 flips | cellSync2 |
| 2026-11-10 | gta-sa | 6481 flips | allocator seeded with image ranges |
| 2026-11-10 | rdr | 7081 flips | first frames, black screen (no geometry) |
| 2026-11-10 | rdr | 0 flips | after the proven-seven LLE list; engine threads run, renderer idle |
| 2026-11-11 | rdr | 0 flips | five syscalls now match RPCS3; install archive still forced |
| 2026-11-12 | rdr | 2161 flips | Social Club legal notice on screen at 13 s; SPURS abort at 78 s |
| 2026-11-12 | rdr | 601 flips | title screen reached at ~22 s; abort at ~23 s |
| 2026-11-12 | all | A/B rule | the criterion becomes the reference binary in the same minute; rdr's bar becomes the title screen, read from a recording |
| 2026-11-13 | rdr | EXODUS IN AMERICA | the 17-entry dispatch table at 0x013e5c8c resolves (17 labels, 0 indirect jumps left); 3 of 4 runs reach the chapter card, 4921/6061/5341 flips, 0 crashes, 0 unknown indirect. The 4th crashed after the card on a cellFiber JoinFiber defect, not on the dispatch. See docs/findings/f-rdr-dispatch-table.md |

### RDR's row cannot be judged by regress.sh yet

The RDR row above cannot be judged by `tools/regress.sh` as it stands: its
criterion is the ALERT screen and it sends no input, so the title never leaves
"Press START" there. Measured -- `rdr-ref1 alive flips=2461` with
`miss: 0 OCR'd` on every attempt. The run that does reach ALERT drives with
`tools/drive_rdr.py`, which presses START; `tools/rdr_ab.sh` pairs two binaries
that way. Fixing regress.sh for this title means giving it an input driver, and
that is a separate change.

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

## Why the RDR bar is a screen and not a flip count

7081 flips of a black screen were once recorded as "first frames". A flip count
alone cannot tell the title's own title screen from an empty buffer, so the bar
is the screen itself, read back from the pixels the title presented: the two-line
title block in dark red and "Press START" below it, both drawn from the title's
assets. The evidence frames are committed under docs/evidence/.
