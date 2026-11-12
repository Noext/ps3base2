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

