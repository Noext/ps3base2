# The lifter's seed forms, on synthetic input.
#
# Two forms, and the difference is the whole point of the parse:
#
#   0x3000c7e8       an ABSOLUTE guest address. Valid only for the layout it was
#                    recorded in: layout_for.py recomputes every module base from
#                    the title's text extent, so the same function moves.
#   liblv2+0xc7e8    (image, byte offset). Resolved at lift time against the base
#                    THIS lift gave that image, so it survives a relayout.
#
# The measured failure the named form fixes: liblv2 sat at 0x21000000, then the
# modules moved to 0x30000000, and every absolute seed in the file named the
# wrong image or nothing at all. seed_is_stale() could only drop them.
#
# Run: python3 tests/ppu_seed_forms.py   (exit 0 = pass)
import os
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "recomp", "ppu"))

from codegen import parse_seed, resolve_seeds, load_landmarks

FAILED = []


def check(name, got, want):
    if got != want:
        FAILED.append("%s: got %r, want %r" % (name, got, want))


def main():
    # 1. The two forms, and nothing else.
    check("absolute", parse_seed("0x3000c7e8"), (None, 0x3000c7e8))
    check("named", parse_seed("liblv2+0xc7e8"), ("liblv2", 0xc7e8))
    check("named-hex-0x", parse_seed("liblv2+0xc7e8"), ("liblv2", 0xc7e8))
    check("named-decimal", parse_seed("liblv2+51176"), ("liblv2", 51176))
    check("comment", parse_seed("# not a seed"), None)
    check("empty", parse_seed(""), None)
    check("garbage", parse_seed("liblv2+bogus"), None)

    # 2. resolve_seeds is per-image and uses THIS lift's base. The SAME file
    #    resolves to different addresses for the two layouts, which is what an
    #    absolute seed cannot do.
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "seeds.txt")
        with open(path, "w") as f:
            f.write("# a comment\n")
            f.write("liblv2+0xc7e8\n")
            f.write("0x3000c7e8\n")
            f.write("rdr+0x100\n")          # a seed for another image
        at_3000 = resolve_seeds(path, "liblv2", 0x30000000)
        at_2100 = resolve_seeds(path, "liblv2", 0x21000000)
        check("resolve/named/@0x30000000", at_3000[0], (0x3000c7e8, "liblv2+0xc7e8"))
        check("resolve/named/@0x21000000", at_2100[0], (0x2100c7e8, "liblv2+0xc7e8"))
        # The absolute seed does NOT move: it is the same number in both layouts,
        # which is exactly why it goes stale.
        check("resolve/absolute/@0x30000000", at_3000[1], (0x3000c7e8, None))
        check("resolve/absolute/@0x21000000", at_2100[1], (0x3000c7e8, None))
        # Another image's named seed is not liblv2's business.
        check("resolve/foreign-dropped", len(at_3000), 2)

    # 3. Landmarks must be the named form. An absolute one is refused LOUDLY
    #    rather than resolved, because a landmark exists to be stable.
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "landmarks.txt")
        with open(path, "w") as f:
            f.write("liblv2+0xc7e8 spurs-abort-tail\n")
            f.write("0x3000c7e8 stale-form\n")
            f.write("# commented out\n")
        lm = load_landmarks(path)
        check("landmarks/count", len(lm), 1)
        check("landmarks/entry", lm[0], ("liblv2", 0xc7e8, "spurs-abort-tail"))

    if FAILED:
        for f in FAILED:
            print("FAIL " + f)
        return 1
    print("ppu_seed_forms: 16 checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
