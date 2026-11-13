#!/usr/bin/env python3
"""is_green.py <png> -- is this frame the solid green clear colour?

The test RESULTS-S3.md validated: fraction of pixels with G>60, R<40, B<40 on a
downscale. The clear colour measured on RDR's green screen is srgb(0,145,0) and
covers ~89% of the frame (the rest is black bars), so a threshold of 0.80 is well
clear of both the black frames (0.0) and the red chapter card (0.0).

Why not ImageMagick's %[fx:standard_deviation]: it returns 0..1, not 0..255, so a
"< 3.0" test matches every frame -- that bug made a profiling run sample the red
chapter card and report it as the green screen.

usage: is_green.py <png> [--threshold 0.80] [--verbose]
exit: 0 green, 1 not green, 2 usage/read error
"""
import sys

try:
    from PIL import Image
except ImportError:
    sys.stderr.write("is_green: PIL is required\n")
    sys.exit(2)


def green_fraction(path, step=8):
    im = Image.open(path).convert("RGB")
    w, h = im.size
    px = im.load()
    n = g = 0
    for y in range(0, h, step):
        for x in range(0, w, step):
            r, gg, b = px[x, y]
            n += 1
            if gg > 60 and r < 40 and b < 40:
                g += 1
    return g / n if n else 0.0


def main(argv):
    if not argv:
        sys.stderr.write(__doc__)
        return 2
    path = argv[0]
    threshold = 0.80
    if "--threshold" in argv:
        threshold = float(argv[argv.index("--threshold") + 1])
    try:
        frac = green_fraction(path)
    except OSError as e:
        sys.stderr.write("is_green: %s\n" % e)
        return 2
    if "--verbose" in argv:
        print("%.4f" % frac)
    return 0 if frac >= threshold else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
