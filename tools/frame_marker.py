#!/usr/bin/env python3
"""Find a text marker in a screen recording, mechanically and cheaply.

Why a recording and not a poll: the RDR title screen is up for about a quarter of
a second before the SPURS stop. Polling the display a few times a second steps
over it -- the first pass of this task grabbed 66 window screenshots over 23 s and
saw no "Press START", while the same binary recorded at 30 fps shows eight frames
of it. So the criterion is read from a recording.

Why the OCR runs after the run and not during it: tesseract competes for the same
cores the title is rendering on, and that moves the flip count the same run is
being judged by.

Why a cheap prefilter picks the frames: 774 frames at 30 fps take five minutes if
every one goes through tesseract. The marker is lit text in one band of the
screen, so the frames whose band has no lit text cannot contain it and are
skipped on a pixel count. OCR, when it runs, reads the whole band rather than the
prefilter window, so a marker that sits a little left or right of the window is
still read.

usage: frame_marker.py <video> <marker> [--crop WxH+X+Y] [--window WxH+X+Y]
                        [--fps N] [--min-lit N]
exit: 0 marker found, 1 not found, 2 usage or environment problem
prints: "hit <frame> <seconds> <text>" on success, "miss: ..." otherwise.
"""
import os
import re
import subprocess
import sys

# The band the bottom-of-screen prompt is drawn in at 720p, and the window inside
# it where the prompt's glyphs are lit. Both are in the capture's pixels.
DEFAULT_CROP = "1000x160+140+550"
DEFAULT_WINDOW = "300x48+490+612"
DEFAULT_FPS = 30
DEFAULT_MIN_LIT = 20        # lit pixels in the window before OCR is worth it
# 0..255. This was 60 and that was wrong: the same title screen is captured at
# two brightnesses depending on how the frame was presented -- one run peaks at
# 200+ in the prompt's glyphs, another at 41 -- and a threshold of 60 discarded
# every frame of the dim kind before OCR ever saw it. Measured on two recordings
# of the same binary: glyph peak 41 in one, black background peak 2 in the same
# band of a frame without the prompt. 24 sits between them with room either side,
# and auto-level before OCR means the absolute brightness does not matter once
# the frame is let through.
LIT_LEVEL = 24


def norm(s):
    return re.sub(r"\s+", "", s).lower()


def decode(video, outdir, fps):
    os.makedirs(outdir, exist_ok=True)
    for f in os.listdir(outdir):
        if f.endswith(".png"):
            os.remove(os.path.join(outdir, f))
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                    "-i", video, "-vf", "fps=%d" % fps,
                    os.path.join(outdir, "f%05d.png")],
                   check=True)
    return sorted(f for f in os.listdir(outdir) if f.endswith(".png"))


def lit_counts(pngs, outdir, window):
    """Lit pixels inside the window for every frame, in one PIL pass."""
    from PIL import Image
    w, h, x, y = [int(v) for v in re.split(r"[x+]", window)]
    out = []
    for f in pngs:
        im = Image.open(os.path.join(outdir, f)).convert("L").crop((x, y, x + w, y + h))
        hist = im.histogram()
        out.append(sum(c for i, c in enumerate(hist) if i >= LIT_LEVEL))
    return out


def ocr(png, crop, crop_tmp):
    subprocess.run(["convert", png, "-crop", crop, "+repage",
                    "-colorspace", "gray", "-auto-level",
                    "-resize", "250%", crop_tmp],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    out = subprocess.run(["tesseract", crop_tmp, "-", "--psm", "6"],
                         capture_output=True, text=True)
    return out.stdout


def main(argv):
    if len(argv) < 3:
        print("usage: frame_marker.py <video> <marker> [--crop WxH+X+Y] "
              "[--window WxH+X+Y] [--fps N] [--min-lit N]", file=sys.stderr)
        return 2
    video, marker = argv[1], argv[2]
    crop, window = DEFAULT_CROP, DEFAULT_WINDOW
    fps, min_lit = DEFAULT_FPS, DEFAULT_MIN_LIT
    i = 3
    while i < len(argv):
        if argv[i] == "--crop" and i + 1 < len(argv):
            crop = argv[i + 1]; i += 2
        elif argv[i] == "--window" and i + 1 < len(argv):
            window = argv[i + 1]; i += 2
        elif argv[i] == "--fps" and i + 1 < len(argv):
            fps = int(argv[i + 1]); i += 2
        elif argv[i] == "--min-lit" and i + 1 < len(argv):
            min_lit = int(argv[i + 1]); i += 2
        else:
            print("unknown argument: " + argv[i], file=sys.stderr)
            return 2
    if not os.path.exists(video):
        print("no recording: " + video, file=sys.stderr)
        return 2
    outdir = os.path.join(os.path.dirname(os.path.abspath(video)), "frames")
    try:
        pngs = decode(video, outdir, fps)
    except subprocess.CalledProcessError as e:
        print("cannot decode %s: %s" % (video, e), file=sys.stderr)
        return 2
    if not pngs:
        print("recording decoded to no frames: " + video, file=sys.stderr)
        return 2
    try:
        lit = lit_counts(pngs, outdir, window)
    except ImportError:
        print("PIL is needed to read the frames; not importable", file=sys.stderr)
        return 2
    want = norm(marker)
    ctmp = os.path.join(outdir, ".ocr.png")
    screened = ocr_frames = 0
    try:
        for n, f in enumerate(pngs):
            if lit[n] < min_lit:
                continue
            screened += 1
            text = ocr(os.path.join(outdir, f), crop, ctmp)
            ocr_frames += 1
            if want and want in norm(text):
                print("hit %s %.2f %s" % (f, n / float(fps), " ".join(text.split())))
                keep = os.path.join(outdir, f)
                for g in pngs:
                    p = os.path.join(outdir, g)
                    if p != keep:
                        os.remove(p)
                return 0
        print("miss: %d frames, %d with lit text in the window, %d OCR'd"
              % (len(pngs), screened, ocr_frames))
        return 1
    finally:
        if ocr_frames == 0:
            for g in pngs:
                os.remove(os.path.join(outdir, g))


if __name__ == "__main__":
    sys.exit(main(sys.argv))
