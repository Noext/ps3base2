# T5 -- the RDR reference point

Task: tools/regress.sh must treat RDR as PASS when the Social Club legal notice
is reached, the flip count is above 2000, and nothing aborts before 60 s.
Encode that, measure it, raise the bar at each milestone.

## Measurements

### M1 -- what RDR actually does at HEAD b1ba5ee, on a fresh build

Build: /root/Dev/wt-T5/build-T5 from main b1ba5ee, RelWithDebInfo, clang.
Run: timeout 110 build-T5/ps3base --no-launcher, fresh VFS, DISPLAY :920.

The previous RDR reference point was flips >= 0, which a black screen satisfies.
Two facts replace it.

**Fact 1: the Social Club legal notice really is on screen.** A frame captured
at t=13 s from the title's own window reads, unmodified:

    $ DISPLAY=:920 import -window 0x60002e /tmp/T5-shots3/t13.png
    $ convert /tmp/T5-shots3/t13.png -format '%k colours mean=%[fx:mean*255]' info:
    120 colours mean=1.53743
    $ tesseract /tmp/T5-shots3/t13.png - | head -8
    Your gameplay information may be displayed on web pages and leaderboards, including those
    located at http:/www.rockstargames.com/socialclub.

    Playing the game may make you eligible to claim some prizes. Check out
    http:/www.rockstargames.com/socialclub/events.

    For details, official rules, or to opt-out of certain Social Club features visit
    http:/www.rockstargames.com/socialclub/privacy.

The same text is still up at t=15 s (identical frame, 29981 bytes in both), and
by t=17 s the window is black (3 colours, 494 bytes). So the notice occupies
about t=12..15 s of a run and is the first window in which the title draws its
own content rather than a logo.

A second capture at 1 s intervals shows the sequence around it:

    t021.png  124490 4259 colours  mean=11.0462 max=230
    t022.png  309061 13602 colours mean=24.0969 max=255   <- Rockstar logo
    t023.png  232847 671 colours   mean=7.55931 max=230
    t024.png   34708 45 colours    mean=1.31848 max=230
    t025.png     494 3 colours     mean=0.0342569 max=230  <- black
    ...
    t042.png     494 3 colours     mean=0.0342569 max=230

and the decoder wakes once, at 16.9 s in that run:

    I  16.909544 [1687758] tty: [0] [BINK] Using 2 speakers.

**Fact 2: the abort is well after the notice, and it is the SPURS assertion.**
From the same run:

    E  85.431822 spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET cmd 0x40 unmapped ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428
    E  85.431868 spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET cmd 0x40 unmapped ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06510
    I  85.432059 tty: [0] PS3 SDK INTERNAL ASSERTION FAILURE: libspurs, version 492000, file "handler.c", line 198, function "spurs_handler_entry", unexp
    I  85.435602 tty: [0] The SPURS is aborted.
    I  85.435619 process: _sys_process_exit(0x8001000c)
    I  85.435654 ppu: process exit -2147418100 from thread 0x1000004 lr=0x3000c7e8 (not lifted)

Flip count and run length in that run: rsx: flip 2341 at t=83.5 s, exit 12 at
86 s. A run with no frame sampling at all reached rsx: flip 2881 at t=109 s and
was still alive when the 110 s timeout closed the window (gpu: window closed,
exiting), so the abort's wall-clock position moves but always follows the notice
by more than 60 s: 78 s, 85 s, 91 s measured across three runs.

So the three-part criterion is measurable today: notice yes, flips 2100-2900,
abort later than 60 s.

### M2 -- the criterion encoded in tools/regress.sh

docs/regress-baseline.md becomes the single place the bar lives, as key=value
fields read by the script:

    minecraft   flips=6500
    gta-sa      flips=6300
    rdr         flips=2000 screen=socialclub abort_after=60

tools/regress.sh gains a third expectation kind, screen:

- screen= names a marker word the captured frame must contain. frame_shows pipes
  the grab through tesseract and greps for it, so the screen is read from the
  pixels the title presented rather than inferred from a counter, and a renderer
  that presents an empty buffer fails.
- flips= is the flip floor, unchanged in meaning; RDR's goes from 0 to 2000.
- abort_after= is the deadline in seconds. The SPURS abort exits through
  _sys_process_exit, so the process exit code is the title's own and not a
  signal; the script reads the first of "SPURS is aborted",
  "_sys_process_exit(0x8001000c)" or "PS3 SDK INTERNAL ASSERTION FAILURE" and
  fails only if it happened before the deadline. An abort after it is reported
  and tolerated: it is the next milestone's work, not a regression.
- Sampling runs every 2 s while the title runs and stops as soon as the marker
  is read, so OCR does not load the machine whose flip count is being judged.
- If xdotool, import or tesseract is missing, RDR reports SKIP as an environment
  problem instead of passing on liveness it did not verify.

### M3 -- the run, with the new criterion

    $ cd /root/Dev/wt-T5 && DISPLAY_NUM=:920 SECONDS_PER_GAME=120 tools/regress.sh
    regress: 120s per game on display :920
    minecraft    FAIL  exit 1          flips=0      crashes=0   log=/tmp/regress-minecraft.log
    gta-sa       FAIL  exit 1          flips=0      crashes=0   log=/tmp/regress-gta-sa.log
    rdr          PASS  SPURS abort     flips=2341   crashes=0 screen=socialclub@13s abort=at 85s

    regress: 2 game(s) REGRESSED

RDR is judged PASS by the new criterion: notice read at 13 s, 2341 flips, abort
at 85 s, later than the 60 s deadline. The two FAILs are an artefact of this
worktree and not a regression: that run used a checkout with no out-mc/out-gta
symlink, so both titles died at load with

    E   0.321482 [1727470] loader: cannot read ./out-mc/gen/reloc/liblv2.elf
    E   0.292179 [1727507] loader: cannot read ./out-gta/gen/reloc/liblv2.elf

They are re-run with the generated directories linked, below.

The frame the script itself captured and matched at 13 s is kept as
/tmp/regress-rdr-shots/hit.png, and reads back:

    $ tesseract /tmp/regress-rdr-shots/hit.png -
    30 fPs 33.4 ms

    Your gameplay information may be displayed on web pages and leaderboards, including those
    located at http:/Avww.rockstargames.com/socialclub

    Playing the game may make you eligible to claim some prizes. Check out
    http:/www.rockstargames.com/socialclub/events.

    For details, official rules, or to opt-out of certain Social Club features visit
    http:/www.rockstargames.com/socialclub/privacy

### M4 -- negative controls: the criterion can fail

A test that cannot fail is not a criterion. Two runs with the bar deliberately
moved prove each half of it is load-bearing. Neither edit is kept.

**Marker removed** (screen=ZZNOTONSCREEN, everything else unchanged):

    $ sed -i 's/screen=socialclub/screen=ZZNOTONSCREEN/' docs/regress-baseline.md
    $ DISPLAY_NUM=:920 SECONDS_PER_GAME=120 tools/regress.sh rdr
    regress: 120s per game on display :920
    rdr          FAIL  SPURS abort     flips=2341   crashes=0   screen=MISSING abort=at 85s log=/tmp/regress-rdr.log

    regress: 1 game(s) REGRESSED
    exit=1

The flips and the timing are identical to the passing run; only the frame check
decides, and it fails. That is the point of reading the pixels: a high flip
count alone is what used to pass a black screen.

**Deadline moved past the abort** (abort_after=200, everything else unchanged):

    $ sed -i 's/abort_after=60/abort_after=200/' docs/regress-baseline.md
    $ DISPLAY_NUM=:920 SECONDS_PER_GAME=120 tools/regress.sh rdr
    regress: 120s per game on display :920
    rdr          FAIL  SPURS abort     flips=2641   crashes=0   screen=socialclub@14s abort=at 98s log=/tmp/regress-rdr.log

    regress: 1 game(s) REGRESSED
    exit=1

The notice is read at 14 s and the flip floor is met, but the abort at 98 s is
before the 200 s deadline, so the run fails. The deadline half is measured too,
and "no abort before 60 s" is enforced rather than assumed.

### M5 -- Minecraft and GTA under the same script

The change is additive: minecraft and gta-sa keep expectation kind 'alive' and
their floors of 6500 and 6300 come from the same file, now as flips=6500 and
flips=6300 instead of the old "flips >= N" prose. Measured with the new script,
on this machine:

    $ cd /root/Dev/wt-T5 && DISPLAY_NUM=:920 SECONDS_PER_GAME=120 tools/regress.sh
    regress: 120s per game on display :920
    minecraft    FAIL  alive           flips=5761   crashes=0   log=/tmp/regress-minecraft.log
    gta-sa       PASS  alive           flips=6301   crashes=0
    rdr          PASS  SPURS abort     flips=2641   crashes=0 screen=socialclub@14s abort=at 97s

    regress: 1 game(s) REGRESSED
    exit=1

That minecraft line is load, not the script: the shared machine was at load
average 18.01 at the time (other agents' builds and runs). Re-run alone once the
load fell, with no code change in between:

    $ uptime
     07:54:13 up 2 days, 12:12,  0 user,  load average: 2.37, 7.02, 9.38
    $ DISPLAY_NUM=:920 SECONDS_PER_GAME=120 tools/regress.sh minecraft
    regress: 120s per game on display :920
    minecraft    PASS  alive           flips=6721   crashes=0

    regress: all present games passed (0 skipped)
    exit=0

and GTA alone, at load 2.91:

    $ DISPLAY_NUM=:920 SECONDS_PER_GAME=120 tools/regress.sh gta-sa
    regress: 120s per game on display :920
    gta-sa       PASS  alive           flips=6481   crashes=0

    regress: all present games passed (0 skipped)
    exit=0

So minecraft 6500 and gta-sa 6300 are unchanged as floors and both still pass
when the machine is not oversubscribed; 6721 and 6481 flips are the measured
values against them.

### M6 -- how the bar is raised from here

The RDR bar is one line in docs/regress-baseline.md, and the script has no RDR
numbers in it:

    rdr         flips=2000 screen=socialclub abort_after=60

Each later milestone is one edit to that line plus a History row:

| milestone | edit |
|---|---|
| press Start accepted, main menu drawn | screen=<marker read from that frame>, abort_after=120 |
| a rendered scene rather than a menu | screen=<marker read from that frame>, flips raised to the measured value |
| no abort at all in the budget | abort_after=9999, so only the notice and the flips decide |

The measurement that will set each number is the same one used here: capture the
frame, read it, count the flips, and note the second at which the first SPURS
assertion appears.

### M7 -- reproduction on the committed tree

The criterion is re-measured on b2bcf74, with the same command a user runs:

    $ cd /root/Dev/wt-T5 && git rev-parse --short HEAD
    b2bcf74
    $ DISPLAY_NUM=:920 SECONDS_PER_GAME=120 tools/regress.sh rdr
    regress: 120s per game on display :920
    rdr          PASS  SPURS abort     flips=2101   crashes=0 screen=socialclub@11s abort=at 75s

    regress: all present games passed (0 skipped)
    exit=0

Four RDR runs across the session read the notice at 10 s, 11 s, 13 s and 14 s,
with 2101-2641 flips and the abort at 75-98 s. Every one passes; the numbers
move with the machine's load, which is why the floor is 2000 rather than the
lowest measured value.

### Files changed

    tools/regress.sh              third expectation kind, 'screen': captures the
                                  window, reads it with tesseract, greps the marker;
                                  reads flips=, screen= and abort_after= from the
                                  baseline; a SPURS stop is named rather than
                                  reported as a bare exit code; a missing OCR/grab
                                  tool is SKIP (environment), never a pass.
    docs/regress-baseline.md      bars become key=value fields; rdr moves from
                                  'flips >= 0' to
                                  'flips=2000 screen=socialclub abort_after=60';
                                  history row and a section on why 2000 and not 0.
    docs/progress.md              the milestone with its proof.
    RESULTS.md                    this file.

Minecraft's 6500 and GTA's 6300 are the same two numbers as before the change,
only spelled flips=6500 and flips=6300 so the script can read them.

### M8 -- the machine's load moves the flip counts, and that is not a regression

While other agents were building and running, the same committed tree was put
through the full suite. Measured load average, printed either side of the run:

    $ uptime
     08:36:04 up 2 days, 12:54,  0 user,  load average: 39.11, 27.12, 17.77
    $ DISPLAY_NUM=:920 SECONDS_PER_GAME=120 tools/regress.sh
    regress: 120s per game on display :920
    minecraft    FAIL  alive           flips=3601   crashes=0   log=/tmp/regress-minecraft.log
    gta-sa       FAIL  alive           flips=5941   crashes=0   log=/tmp/regress-gta-sa.log
    rdr          PASS  alive           flips=3361   crashes=0 screen=socialclub@14s abort=none

    regress: 2 game(s) REGRESSED
    exit=1
     08:42:06 up 2 days, 13:00,  0 user,  load average: 33.72, 28.91, 21.65

All three titles were alive and had zero crashes; what moved is the rate at
which they present frames, because the host has 32 cores and was running
several builds and three other titles at once. The same binaries on an idle
machine measured 6721 (minecraft, floor 6500) and 6481 (gta-sa, floor 6300) and
passed. Neither floor was changed to accommodate the loaded run: a floor that
moves with whoever else is building is not a regression test.

Two things about this run are worth keeping. RDR passed it without any of the
notice-timing slack being exercised -- notice at 14 s, abort=none at all, the
title still flipping at the budget -- which is the other side of the criterion
the earlier runs did not show. And the loaded run is the reason the RDR floor
is 2000 and not 3000: under this load it still measured 3361, so 2000 leaves
room for the worst machine this suite is ever run on.
