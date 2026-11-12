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
