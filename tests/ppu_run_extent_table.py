# A test for jump_table's run-extent rule, on synthetic words.
#
# Why synthetic and not the title image: the disc is derived material that is not
# in the tree, so a test that needs bin/rdr/EBOOT.elf only runs where the disc is
# mounted -- which is exactly where nobody runs it.
#
# The rule this pins was measured on Red Dead Redemption's f_00ae50f0, a shared
# opcode dispatcher:
#
#     00ae5120  3f00013e  addis r24, r0, 0x013e
#     00ae5138  33185c8c  addic r24, r24, 0x5c8c   ; base 0x013e5c8c
#     00ae5160  889e0000  lbz   r4, 0(r30)          ; the opcode byte
#     00ae5168  5484103a  rlwinm r4, r4, 2, 0, 29
#     00ae516c  7c98202e  lwzx  r4, r24, r4
#     00ae5174  7c8903a6  mtspr ctr, r4
#     00ae5178  4e800420  bcctr
#
# There is no cmpwi/bgt and no branch to a default: the CALLER validates the
# byte, so a bound check here would be dead code. Before the fix the detector
# required both for every table whose base is not bctr+4, returned None, and the
# lift emitted a switch with no cases -- the run stopped on 0x00ae5818, entry 13
# of the 17-entry table at 0x013e5c8c.
#
# The rule: for an ABSOLUTE base built by lis/addi in the same window, the extent
# is the contiguous run of in-code entries, and three guards keep it from
# swallowing an ordinary indirect branch or a table of function pointers:
#
#   1. a TOC-loaded base still needs its bound compare (the value there is a
#      run-time pointer, so a bound is the only evidence of length);
#   2. the run must be at least three entries;
#   3. the entries must span at most 0x4000 bytes -- a switch's cases are basic
#      blocks of the function that dispatches, a function-pointer table's entries
#      are scattered across the image. Seeding those as cases is what made the
#      static table scan add 3,460 functions and segfault the renderer.
#
# Run: python3 tests/ppu_run_extent_table.py   (exit 0 = pass)
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "recomp", "ppu"))

from analysis import jump_table, table_dispatch_bctr

TEXT_LO, TEXT_HI = 0x1000, 0x10000
TABLE_BASE = 0x20000          # a DATA segment, outside the code range
TOC_BASE = 0x21000


def lis(rd, imm):
    return 0x3C000000 | (rd << 21) | (imm & 0xFFFF)


def addic(rd, ra, imm):
    return 0x30000000 | (rd << 21) | (ra << 16) | (imm & 0xFFFF)


def lbz(rd, ra, d):
    return 0x88000000 | (rd << 21) | (ra << 16) | (d & 0xFFFF)


def rlwinm(ra, rs, sh, mb, me):
    return 0x54000000 | (rs << 21) | (ra << 16) | (sh << 11) | (mb << 6) | (me << 1)


def lwzx(rd, ra, rb):
    return 0x7C00002E | (rd << 21) | (ra << 16) | (rb << 11)


def lwz(rd, d, ra):
    return 0x80000000 | (rd << 21) | (ra << 16) | (d & 0xFFFF)


def mtctr(rs):
    return 0x7C0003A6 | (rs << 21)


BCCTR = 0x4E800420


class FakeElf:
    def __init__(self, image):
        self.image = image

    def read(self, addr, size):
        if addr < 0 or addr + size > len(self.image):
            return None
        return bytes(self.image[addr:addr + size])


class FakeImg:
    """Just enough of analysis.Image for the two detectors."""

    def __init__(self):
        self.image = bytearray(0x30000)
        self.elf = FakeElf(self.image)
        self.profile = _Profile()
        self.toc = 0
        self.text_lo = TEXT_LO
        self.text_hi = TEXT_HI
        self.current_function = None

    def put(self, addr, word):
        struct.pack_into(">I", self.image, addr, word)

    def word(self, addr):
        return struct.unpack_from(">I", self.image, addr)[0]

    def in_text(self, v):
        return TEXT_LO <= v < TEXT_HI and v % 4 == 0

    def toc_word(self, disp):
        return struct.unpack_from(">I", self.image, (self.toc + disp) & 0xFFFFFFFF)[0]


class _Profile:
    def has(self, rule):
        return True


def put_table(img, base, targets):
    for k, t in enumerate(targets):
        img.put(base + k * 4, t)


def rdr_shape(img, bctr=0x1018, base=TABLE_BASE, targets=(0x1100, 0x1200, 0x1300)):
    """The measured shape: absolute base, no bound, no default, run extent."""
    img.put(0x1000, lis(24, base >> 16))
    img.put(0x1004, addic(24, 24, base & 0xFFFF))
    img.put(0x1008, lbz(4, 30, 0))
    img.put(0x100C, rlwinm(4, 4, 2, 0, 29))
    img.put(0x1010, lwzx(4, 24, 4))
    img.put(0x1014, mtctr(4))
    img.put(bctr, BCCTR)
    put_table(img, base, targets)


def case_run_extent_accepted():
    img = FakeImg()
    rdr_shape(img)
    jt = jump_table(img, 0x1018)
    assert jt is not None, "run extent: the RDR shape was rejected"
    assert jt["base"] == TABLE_BASE, "run extent: base 0x%x" % jt["base"]
    assert jt["count"] == 3, "run extent: count %d" % jt["count"]
    assert 0x1300 in jt["targets"], "run extent: last entry missing"
    return "no bound, no default, absolute base: accepted, %d entries from 0x%08x" % (
        jt["count"], jt["base"])


def case_toc_still_needs_bound():
    img = FakeImg()
    img.toc = 0x22000
    img.put(0x22000 - 0x22000 + 0, 0)          # unused
    # lwz r24, 0(r2) reads the TOC slot at img.toc + 0; put the table pointer there.
    img.put(img.toc, TABLE_BASE)
    img.put(0x1000, lwz(24, 0, 2))
    img.put(0x1008, lbz(4, 30, 0))
    img.put(0x100C, rlwinm(4, 4, 2, 0, 29))
    img.put(0x1010, lwzx(4, 24, 4))
    img.put(0x1014, mtctr(4))
    img.put(0x1018, BCCTR)
    put_table(img, TABLE_BASE, (0x1100, 0x1200, 0x1300))
    jt = jump_table(img, 0x1018)
    assert jt is None, ("TOC base with no bound: expected None, got base 0x%x"
                        % jt["base"])
    return "TOC-loaded base without a bound: still rejected"


def case_two_entries_rejected():
    img = FakeImg()
    rdr_shape(img, targets=(0x1100, 0x1200))
    jt = jump_table(img, 0x1018)
    assert jt is None, "two entries: expected None, got %d" % jt["count"]
    return "a run of two code words is not a dispatch table: rejected"


def case_scattered_rejected():
    img = FakeImg()
    # Three code addresses, but spread over 0x8000 bytes: a function-pointer
    # table, not a switch. Seeding these as cases of the caller is the failure
    # that made the static table scan segfault the renderer.
    rdr_shape(img, targets=(0x1100, 0x5000, 0x9000))
    jt = jump_table(img, 0x1018)
    assert jt is None, "scattered entries: expected None, got span 0x%x" % (
        max(jt["targets"]) - min(jt["targets"]))
    return "entries spanning 0x8000 bytes are function pointers, not cases: rejected"


def case_setter_is_nearest_before():
    """table_dispatch_bctr must take the definition ABOVE the bctr, not the last
    writer in the body. On f_00ae50f0 the last writer is 0x2800 bytes past the
    dispatch and rebuilds a base that is not in text, so the detector returned
    None while the correct table sat 861 instructions above."""
    img = FakeImg()
    # Correct base in r16, set at function entry.
    img.put(0x1000, lis(16, TABLE_BASE >> 16))
    img.put(0x1004, addic(16, 16, TABLE_BASE & 0xFFFF))
    img.put(0x10F0, lwzx(5, 16, 5))
    img.put(0x10FC, mtctr(5))
    img.put(0x1100, BCCTR)
    # An unrelated later block rewrites r16 to a base that is not in text.
    img.put(0x1800, lis(16, 0x0001))
    img.put(0x1804, addic(16, 16, 0x0000))
    put_table(img, TABLE_BASE, (0x1200, 0x1300, 0x1400))
    jt = table_dispatch_bctr(img, 0x1100, 0x1000, 0x2000)
    assert jt is not None, "setter rule: detector returned None (took the later writer)"
    assert jt["base"] == TABLE_BASE, "setter rule: base 0x%x, expected 0x%x" % (
        jt["base"], TABLE_BASE)
    return "nearest definition before the bctr wins over a later writer: base 0x%08x" % jt["base"]


def main():
    checks = [
        case_run_extent_accepted(),
        case_toc_still_needs_bound(),
        case_two_entries_rejected(),
        case_scattered_rejected(),
        case_setter_is_nearest_before(),
    ]
    for c in checks:
        print("ok   " + c)
    print("%d checks passed" % len(checks))
    return 0


if __name__ == "__main__":
    sys.exit(main())
