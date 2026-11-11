# A test for table_dispatch_bctr's two shape rules, on synthetic words.
#
# Why synthetic and not the title image: the detector is what is under test, and
# the disc is derived material that is not in the tree. A test that needs
# bin/rdr/EBOOT.elf can only run where the disc is mounted, which is exactly
# where nobody runs it.
#
# The two rules this pins, both measured on Red Dead Redemption's
# f_00d19208 (the RAGE script bytecode interpreter):
#
#   1. mtctr is not always at bctr-4. Of that function's 163 bcctr, 130 put
#      mtctr at -1, 26 at -2, 3 at -3 and 4 at -4 -- an instruction that
#      finishes the argument setup sits between the two. Requiring bctr-4
#      rejected 0x00d19e98, whose shape is
#
#          0xd19e90  mtctr r5
#          0xd19e94  subfc r30, r3, r30
#          0xd19e98  bctr
#
#      and the lift then emitted a switch with NO cases, so the run stopped at
#      "unknown indirect target 0x00d1bbec" holding the *correct* target.
#
#   2. The lwzx that feeds the dispatch is the one whose RT is the mtctr
#      source. Ignoring RT picked the loop consumer's lwzx
#      (0x00d1a008 lwzx r9,r3,r6) and rebuilt "a table" at 0x013323f0 with one
#      entry instead of the real 256-entry table at 0x013f6400.
#
# Run: python3 tests/ppu_dispatch_table.py   (exit 0 = pass)
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "recomp", "ppu"))

from analysis import table_dispatch_bctr

TEXT_LO, TEXT_HI = 0x1000, 0x20000
TABLE_BASE = 0x5000
TABLE_TARGET = 0x3000          # every entry points here; inside text


def lis(rd, imm):
    return 0x3C000000 | (rd << 21) | (imm & 0xFFFF)


def addic(rd, ra, imm):
    return 0x30000000 | (rd << 21) | (ra << 16) | (imm & 0xFFFF)


def lwzx(rd, ra, rb):
    return 0x7C00002E | (rd << 21) | (ra << 16) | (rb << 11)


def subfc(rd, ra, rb):
    return 0x7C000010 | (rd << 21) | (ra << 16) | (rb << 11)


def mtctr(rs):
    return 0x7C0003A6 | (rs << 21)


BCCTR = 0x4E800420


class FakeElf:
    def __init__(self, image):
        self.image = image

    def read(self, addr, size):
        return self.image[addr:addr + size]


class FakeImg:
    """Just enough of analysis.Image for table_dispatch_bctr."""

    def __init__(self):
        self.image = bytearray(TEXT_HI)
        self.elf = FakeElf(self.image)

    def put(self, addr, word):
        struct.pack_into(">I", self.image, addr, word)

    def word(self, addr):
        return struct.unpack_from(">I", self.image, addr)[0]

    def in_text(self, v):
        return TEXT_LO <= v < TEXT_HI


def build():
    img = FakeImg()
    # The table, 256 entries, all in text.
    for k in range(256):
        img.put(TABLE_BASE + k * 4, TABLE_TARGET)

    # Function opens with the two instructions that build the base in r16.
    img.put(0x1000, lis(16, 0x0000))
    img.put(0x1004, addic(16, 16, TABLE_BASE))
    return img


def case_mtctr_adjacent(img):
    """mtctr immediately before bctr, lwzx four back, RT == mtctr source."""
    img.put(0x10F0, lwzx(5, 16, 5))
    img.put(0x10FC, mtctr(5))
    img.put(0x1100, BCCTR)
    jt = table_dispatch_bctr(img, 0x1100, 0x1000, 0x2000)
    assert jt is not None, "mtctr adjacent: detector returned None"
    assert jt["base"] == TABLE_BASE, "mtctr adjacent: base 0x%x" % jt["base"]
    return "mtctr at bctr-4: base 0x%08x count %d" % (jt["base"], jt["count"])


def case_mtctr_gap(img):
    """The shape that broke the run: mtctr at -8, one ordinary insn between."""
    img.put(0x11E8, lwzx(5, 16, 5))
    img.put(0x11F8, mtctr(5))
    img.put(0x11FC, subfc(30, 3, 30))
    img.put(0x1200, BCCTR)
    jt = table_dispatch_bctr(img, 0x1200, 0x1000, 0x2000)
    assert jt is not None, "mtctr at bctr-8: detector returned None (the 0xd19e98 shape)"
    assert jt["base"] == TABLE_BASE, "mtctr at bctr-8: base 0x%x" % jt["base"]
    return "mtctr at bctr-8 with a gap: base 0x%08x count %d" % (jt["base"], jt["count"])


def case_rt_selects_lwzx(img):
    """An earlier lwzx into a non-mtctr register must not be taken as the feed."""
    img.put(0x12C0, lwzx(5, 16, 5))       # the real feed, into r5 (the mtctr src)
    img.put(0x12E0, lwzx(9, 3, 6))        # the loop consumer, into r9 -- not the feed
    img.put(0x12FC, mtctr(5))
    img.put(0x1300, BCCTR)
    jt = table_dispatch_bctr(img, 0x1300, 0x1000, 0x2000)
    assert jt is not None, "RT rule: detector returned None"
    assert jt["base"] == TABLE_BASE, "RT rule: took the wrong lwzx, base 0x%x" % jt["base"]
    return "RT == mtctr source picks the feed: base 0x%08x" % jt["base"]


def case_no_mtctr_rejected(img):
    """A bctr with no mtctr anywhere near it is not a table dispatch."""
    img.put(0x1400, BCCTR)
    jt = table_dispatch_bctr(img, 0x1400, 0x1000, 0x2000)
    assert jt is None, "no mtctr: expected None, got base 0x%x" % jt["base"]
    return "no mtctr: rejected"


def main():
    img = build()
    checks = [
        case_mtctr_adjacent(img),
        case_mtctr_gap(img),
        case_rt_selects_lwzx(img),
        case_no_mtctr_rejected(img),
    ]
    for c in checks:
        print("ok   " + c)
    print("%d checks passed" % len(checks))
    return 0


if __name__ == "__main__":
    sys.exit(main())
