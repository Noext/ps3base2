# M4 -- regress.sh with measured references

Branch explore/m4, from main HEAD 4a5a132.

Task: new measured references for the three titles, machine at rest, one game at
a time. The rule goes *into the script*: every comparison is an A/B on the same
machine within the same minute. The RDR criterion becomes the TITLE SCREEN:
OCR reads "Press START" off the presented frame. `screen=socialclub` and
`abort_after=60` are stale -- the reference binary built from 22d2a0c aborts at
23 s too, so a 60 s deadline fails a tree that is identical to main.

## Status: started

Proof follows, measurement by measurement, appended raw.

## Measurement 0 -- the criterion is readable off the committed evidence frame

The title screen evidence from HEAD (docs/evidence/rdr-title-screen-raw.png) is
dark red on black: 829862 pixels #000000, 41204 pixels #370000. Plain tesseract
on the raw frame misses it; the OCR needs a level stretch, and the script must
carry that preprocessing or the criterion is a coin flip.

    $ tesseract docs/evidence/rdr-title-screen-raw.png -
    (empty / "RED DEAD" fragments depending on the frame)

    $ convert docs/evidence/rdr-title-screen-raw.png -colorspace gray -auto-level /tmp/t.png
    $ tesseract /tmp/t.png -
    RED DEAD
    REDEMPTION

    Press START >

So the marker is `Press START` and the read is `-colorspace gray -auto-level`
then tesseract. The title is up for ~1.2 s before the SPURS abort at 23 s, so a
2 s sampler (the current script) can and does step over it: the sampling
interval has to be below the dwell time, and OCR must run after the run, not
inside it, or the OCR load moves the flip count it is judging.

## Measurement 1 -- a fresh HEAD build in wt-M4, sampled at 0.25 s: 601 flips, abort 23.4 s

Built from 4a5a132 in this worktree (build-rdr, target ps3base). Window-only capture,
sampler every 0.25 s on :963:

    $ /tmp/m4_run.sh /root/Dev/wt-M4/build-rdr/ps3base /root/Dev/rdr-port/vfs/dev_bdvd 'Red Dead Redemption' 60 m4-rdr-1
    TAG=m4-rdr-1 exit=12 wall=23.716063555
    frames=66
    I  22.474317 [3779821] rsx: flip 601 (buffer 0) worst 86.3 ms, >50ms 1, >100ms 0
    1282:I  23.466372 ... PS3 SDK INTERNAL ASSERTION FAILURE: libspurs ... handler.c line 198 spurs_handler_entry
    1519:I  23.475405 ... tty: [0] The SPURS is aborted.
    1535:I  23.476811 ... ppu: process exit -2147418100 from thread 0x1000004 lr=0x3000c7e8 liblv2+0xc7e8

The OCR over all 66 frames (auto-level + psm 6) finds the legal notice frames 9-25
and the notice text frames 29-47, then the Rockstar-mark intro frames 54-66, and no
frame with "Press START". The abort lands between frames 67 and 68 (the last two are
the blank desktop).

## Measurement 2 -- the title screen is not captured by a 4 Hz sampler

Same binary, root-window grab in a tight loop (no sleep), 596 frames over 54 s:

    $ /tmp/m4_runroot.sh /root/Dev/wt-M4/build-rdr/ps3base /root/Dev/rdr-port/vfs/dev_bdvd 60 m4-rdr-2
    TAG=m4-rdr-2 exit=12 frames=596
    I  53.546519 rsx: flip 901 (buffer 0) worst 57.1 ms, >50ms 5, >100ms 0
    1066: PS3 SDK INTERNAL ASSERTION FAILURE: libspurs ... handler.c line 198
    1246: tty: [0] The SPURS is aborted.
    1249: ppu: process exit -2147418100 ... lr=0x3000c7e8

    $ grep -c . /tmp/m4-ocr2-hits.txt          # frames whose OCR mentions Red Dead / START
    70
    $ grep -ic press /tmp/m4-ocr2-hits.txt
    0

So in this run the abort moves from 23 s to 54 s and the flip count from 601 to 901:
the SPURS stop is tied to a guest-frame count, not to wall time, and the capture load
moves it. That is why the criterion cannot be "abort after 60 s of wall clock".


## Measurement 3 -- the title screen is real but a single run is a coin flip on this machine

The 30 fps recording catches the title when it is drawn. Eight runs of the same
binary, same display, capture at 30 fps, judgement by tools/frame_marker.py:

    $ /tmp/m4_rate.sh 8
    run1 flips=541 abort=25.763827 load=20.50 :: miss: 898 frames, 6 with lit text in the window, 6 OCR'd
    run2 flips=601 abort=22.853372 load=17.96 :: miss: 814 frames, 6 with lit text in the window, 6 OCR'd
    run3 flips=601 abort=23.084891 load=17.60 :: miss: 822 frames, 8 with lit text in the window, 8 OCR'd
    run4 flips=601 abort=23.106703 load=18.54 :: miss: 822 frames, 6 with lit text in the window, 6 OCR'd
    run5 flips=601 abort=23.420260 load=18.24 :: miss: 830 frames, 10 with lit text in the window, 10 OCR'd
    run6 flips=601 abort=23.005371 load=16.89 :: hit f00750.png 24.97 Press START
    run7 flips=601 abort=23.490483 load=23.40 :: hit f00761.png 25.33 Press START
    run8 flips=601 abort=23.017963 load=17.04 :: hit f00750.png 24.97 Press START
    hit 3 of 8

So: 3 of 8 runs show the title, always in the last ~0.25 s before the abort. The
runs that miss are not a capture problem -- 6-10 frames per run have lit text in
the marker's window and were OCR'd; the misses are the Rockstar mark's fade and
the black frame after it, and the title was drawn after the abort in those runs.
Two consecutive frames of a hit, from an earlier recording:

    $ python3 ... band <490,612>-<790,660>, lit pixels >= 60
    f01291.png 507, f01292.png 507          <- title, ~0.07 s of it at 60 fps
    ...
    $ tesseract <band> -
    Press START

What that means for the criterion, stated plainly so it is not papered over:
the port does reach its title screen, and the screen is identifiable from pixels,
but the SPURS abort at ~23 s lands within a fraction of a second of it. Under
this machine's load (17-23 while other agents build) the title wins the race in
3 runs of 8. A regression script that demands the title on ONE run would fail
two thirds of the time on code that is unchanged, which is the same class of
false alarm as the two runs minutes apart.

The criterion stays "the title screen is reached", and the script attempts it
more than once, bounded and recorded, so the pass means what it says.


## Measurement 4 -- the full run: all three green, then the negative controls

### The green run

`tools/regress.sh`, display :963, candidate = this tree's builds, reference =
the main checkout's builds (4a5a132), paired back to back:

    $ DISPLAY_NUM=:963 sh tools/regress.sh
    regress: 120s per run; reference and candidate paired back to back
    regress: display :963, reference root /root/Dev/wt-M4/../ps3base2
    minecraft    candidate /root/Dev/wt-M4/build/ps3base
                 reference /root/Dev/wt-M4/../ps3base2/build/ps3base  (paired, back to back)
                 run root /root/Dev/wt-M4/../ps3base2, up to 2 attempt(s)
      minecraft-ref1 alive         flips=6241   crashes=0   abort=none      120s load=8.05
      minecraft-cand1 alive         flips=6061   crashes=0   abort=none      120s load=11.29
      minecraft-ref2 alive         flips=6241   crashes=0   abort=none      120s load=15.10
      minecraft-cand2 alive         flips=6721   crashes=0   abort=none      120s load=10.61
      minecraft    PASS  flips=6721   vs reference 6241 over 3 pair(s) in 490s
    gta-sa       candidate /root/Dev/wt-M4/build-gta/ps3base
                 reference /root/Dev/wt-M4/../ps3base2/build-gta/ps3base  (paired, back to back)
                 run root /root/Dev/wt-M4/../ps3base2, up to 2 attempt(s)
      gta-sa-ref1    alive         flips=6061   crashes=0   abort=none      120s load=9.63
      gta-sa-cand1   alive         flips=6421   crashes=0   abort=none      120s load=10.58
      gta-sa-ref2    alive         flips=6421   crashes=0   abort=none      120s load=6.44
      gta-sa-cand2   alive         flips=6841   crashes=0   abort=none      120s load=9.31
      gta-sa       PASS  flips=6841   vs reference 6421 over 3 pair(s) in 490s
    rdr          candidate /root/Dev/wt-M4/build-rdr/ps3base
                 reference /root/Dev/wt-M4/../ps3base2/build-rdr/ps3base  (paired, back to back)
                 run root /root/Dev/wt-M4/../ps3base2, up to 6 attempt(s)
      rdr-ref1       SPURS abort   flips=601    crashes=0   abort=22.908719  25s load=7.30
        hit f00746.png 24.83 Press START
      rdr-cand1      SPURS abort   flips=601    crashes=0   abort=22.952633  25s load=8.03
        hit f00748.png 24.90 Press START
      rdr          PASS  flips=601    vs floor 500 (metric=screen); reference flips 601 (recorded, not a bar) over 1 pair(s) in 69s screen=PressSTART 1/1 candidate, 1/1 reference

    regress: all present games passed (0 skipped)
    EXIT=0

Note the earlier pair on the same code (load 15-23) gave minecraft 6421 vs 5341
and a first RDR attempt where the reference missed the title and the candidate
also missed it -- which is exactly why the rule is best-of-N inside one-minute
pairs and not a single sample.

### The tests

    $ ctest --test-dir build -j 4
    100% tests passed, 0 tests failed out of 39
    Total Test time (real) =   3.48 sec

### Negative controls

The screen criterion has to fail when the screen is not there, or it is not a
criterion. Three controls, all run for real:

1. marker replaced by one that is not on the screen:
       $ sed -i 's/screen=PressSTART/screen=ZZNOTONSCREEN/' docs/regress-baseline.md
       $ tools/regress.sh rdr            (see /tmp/m4-neg1.txt)
2. a candidate binary that does not exist: must be reported, not passed
       $ MC_BIN=/nonexistent/ps3base tools/regress.sh minecraft
3. a candidate that is not the title: `RDR_BIN=/bin/false`

Results are in the next measurement so the raw output is not split.

### The negative controls, and the bug the third one found

Three controls, all run for real.

1. A marker that is not on the screen:

       $ sed -i 's/screen=PressSTART/screen=ZZNOTONSCREEN/' docs/regress-baseline.md
       $ DISPLAY_NUM=:963 sh tools/regress.sh rdr
         rdr-ref1       SPURS abort   flips=601    crashes=0   abort=22.703669  25s load=10.28
           miss: 746 frames, 6 with lit text in the window, 6 OCR'd
         rdr          FAIL  flips=601    vs floor 500 (metric=screen); screen=ZZNOTONSCREEN 0/6 candidate, 0/6 reference  <- screen-missing
       regress: 1 game(s) REGRESSED
       neg1 EXIT=1

2. A candidate binary that does not exist:

       $ MC_BIN=/nonexistent/ps3base DISPLAY_NUM=:963 sh tools/regress.sh minecraft
       minecraft    SKIP  no binary at /nonexistent/ps3base
       regress: no game regressed, but 1 game(s) could not be run (environment)
       neg2 EXIT=2

   That was exit 0 ("all present games passed") in the first version. A missing
   binary reading as a pass is the same false green as a wrong number, so a
   skipped game now makes the run an environment problem (exit 2).

3. A candidate that is not the title at all:

       $ RDR_BIN=/bin/false DISPLAY_NUM=:963 sh tools/regress.sh rdr

   The first result of this control was PASS with flips=601 -- for /bin/false.
   That is a false green, and it was a bug in the script, not the criterion
   firing. run_game assigned its positional parameters to globals named bin,
   disc and log, and run_one held the candidate in bin too: after the reference
   run, run_one's bin had been rewritten to the REFERENCE binary, so the
   "candidate" slot re-ran and re-scored the reference. Every variable in
   run_game, screen_check and run_one now carries its own prefix (rg_, sc_, ro_)
   and the control behaves:

       $ RDR_BIN=/bin/false SECONDS_PER_GAME=20 DISPLAY_NUM=:963 sh tools/regress.sh rdr
       rdr-ref1       alive         flips=541    crashes=0   abort=none       22s load=4.55
       rdr-cand1      exit 1        flips=0      crashes=0   abort=none        2s load=8.42
       rdr          FAIL  flips=0      vs floor 500 (metric=screen); reference flips 541 (recorded, not a bar) over 7 pair(s) in 210s screen=PressSTART 0/7 candidate, 0/7 reference  <- below-floor screen-missing
       regress: 1 game(s) REGRESSED
       RC=1

### A second bug the controls exposed: two runs at once

Running a control while an earlier regress was still in flight produced a
candidate log that was not the candidate's: both runs write
/tmp/regress-rdr-cand1.log, and each one's clear_strays kills the other's game.
The numbers from such a run are void, and neither is a pass. The script now takes
an exclusive lock (a directory under /tmp/ps3base-regress.lock, released by a
trap, with a liveness check so a killed run does not wedge the next one) and
waits, bounded, rather than interleaving.
### Measurement 5 -- the detector was throwing away dim title frames, and that is what the 0/7 was

The run before this one reported the candidate 0/7 on the title screen while the
reference was 3/7, on identical code. That is exactly the shape of a false
regression, and it was not the title: it was the prefilter in frame_marker.py.

The marker's brightness is not constant between presentations. Measured on two
recordings of the same binary, in the same 300x48 window the prompt is drawn in:

    recording A (title found):     glyph peak 200+
    recording B (title reported missed): glyph peak 41
    either recording, black frame: peak 2

The prefilter skipped any frame whose window had no pixel >= 60, so every frame
of the type-B presentation was discarded before tesseract ran, and the run was
scored "screen missing". Re-measured on that very recording with the threshold
lowered:

    $ python3 tools/frame_marker.py /tmp/m4-vt8/regress-rdr-cand1.mkv 'Press START'
    hit f00762.png 25.37 Press START
    EXIT=0

and the same frames are visible as the prompt in the recording:

    $ python3 -c '... crop (0,480)-(1280,720) of f00762.png and save'
    <- "Press START" in grey on black, dim but present

The threshold is now 24, which is 12x above the black frame's peak and still
below the dim glyph's 41. The negative direction is checked in the same pass --
a recording from a run where the title never appeared still misses:

    $ python3 tools/frame_marker.py /tmp/m4-fp/m4-pair-ref2.mkv 'Press START'
    miss: 816 frames, 19 with lit text in the window, 19 OCR'd
    EXIT=1

19 frames reach tesseract there (the Rockstar mark and the notice), and none of
them reads as the prompt.

### Measurement 6 -- the order of the pair does not explain the misses

Because the reference always ran first and the candidate second, a plausible
alternative was "the second run loses the race". Tested by swapping the roles --
the main checkout's binary as candidate, the worktree's as reference:

    $ RDR_BIN=/root/Dev/ps3base2/build-rdr/ps3base \
      RDR_REF_BIN=/root/Dev/wt-M4/build-rdr/ps3base \
      DISPLAY_NUM=:963 sh tools/regress.sh rdr
      rdr-ref1       SPURS abort   flips=541    crashes=0   abort=23.508914  26s
      rdr-cand1      SPURS abort   flips=601    crashes=0   abort=23.495855  26s
      rdr-ref2       SPURS abort   flips=601    crashes=0   abort=22.699934  25s
        hit f00741.png 24.67 Press START
      ...
      rdr-ref5       SPURS abort   flips=601    crashes=0   abort=22.803637  25s
        hit f00744.png 24.77 Press START
      rdr          FAIL ... screen=PressSTART 0/7 candidate, 2/7 reference

The worktree binary, now in the candidate slot, still hit 0/7, and the main
checkout binary, now in the reference slot, hit 2/7. So the miss follows the
slot, not the code -- and the slot is only distinguished by where in the pair it
runs. That is what sent the investigation to the detector rather than to the
pair ordering, and the detector was the cause.
