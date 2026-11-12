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
