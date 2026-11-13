# A test for jump_table's run-extent rule, on the REAL bytes of Red Dead
# Redemption's f_00ae50f0.
#
# Why this file exists next to ppu_run_extent_table.py
# ----------------------------------------------------
# tests/ppu_run_extent_table.py builds the shape it tests out of synthetic
# instructions: lis/addic/lbz/rlwinm/lwzx/mtctr/bctr assembled by hand. It passes,
# and it passed while the real dispatch it was written for was still failing --
# because a hand-built shape encodes the author's idea of the code, not the code.
# That is the trap: a test that agrees with the imagination instead of the binary.
#
# So this test carries the 36 instruction words of f_00ae50f0 from 0x00ae50f0 to
# 0x00ae5178, and the 18 words of the table at 0x013e5c8c, exactly as they are in
# bin/rdr/EBOOT.elf. When that ELF is present the fixture is checked against it
# first, so the two cannot drift apart silently.
#
# The shape, which is why the synthetic test missed it
# ----------------------------------------------------
#     00ae5120  3f00013e  addis r24, r0, 0x013e
#     00ae5138  33185c8c  addic r24, r24, 0x5c8c   ; base 0x013e5c8c
#     00ae5160  889e0000  lbz   r4, 0(r30)          ; the opcode byte
#     00ae5168  5484103a  rlwinm r4, r4, 2, 0, 29
#     00ae516c  7c98202e  lwzx  r4, r24, r4
#     00ae5174  7c8903a6  mtspr ctr, r4
#     00ae5178  4e800420  bcctr
#
# Two things the synthetic version could not express and the real window does:
#
#   - the lis and the addic are 24 bytes apart, with real stores between them
#     (stb, stw, ld) that the synthetic shape did not have;
#   - the table base register is r24, and r24 is also the destination of the
#     addic at 0xae5138 whose source is r24 -- the pair is addis+addic on the SAME
#     register, not addis rX + addic rX, rX with a clean gap.
#
# What the rule must produce, measured on the real image
# -----------------------------------------------------
#   jump_table(0xae5178)  -> base 0x013e5c8c, 17 targets, 0xae517c .. 0xae58b8
#   explore(0xae50f0)     -> all 17 are labels of the function, 0 indirect jumps
#   and the same 17 at all 17 bcctr sites in the function (0xae51ec .. 0xae58b4),
#   because the dispatcher re-reads the same table after every opcode.
#
# Run: python3 tests/ppu_run_extent_real_bytes.py   (exit 0 = pass)
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "recomp", "ppu"))

from analysis import Image, ALL_RULES, explore, jump_table

ELF = os.path.join(ROOT, "bin", "rdr", "EBOOT.elf")

# The window the detector actually reads: jump_table() looks back 4*48 bytes
# from the bctr, so the fixture has to start at 0x00ae50b8, not at the function
# entry. It opens with the tail of the PREVIOUS function (0x4e800020 = blr at
# 0x00ae50ec), which is part of what the detector sees and therefore part of the
# evidence: the lis/addic pair has to be found without being confused by the
# neighbouring code above it.
WINDOW_LO = 0x00AE50B8
DISPATCH_LO = 0x00AE50F0
DISPATCH_WORDS = [
    0x90BE0008, 0x4B52B17D, 0x7BE51764, 0x3C80CECE, 0x907E0000, 0x7CA507B4,
    0x608480AA, 0x7C83292E, 0xE8010090, 0x7C0803A6, 0xEBC10070, 0xEBE10078,
    0x38210080, 0x4E800020,
    0xF821FF21, 0x7C0802A6, 0xF80100F0, 0xFBA100C8, 0xFB6100B8, 0x607D0000,
    0x38600000, 0xFB8100C0, 0x60DB0000, 0xFB0100A0, 0x609C0000, 0xFAC10090,
    0x3F00013E, 0x987D0000, 0xFBC100D0, 0x3AC00070, 0x83C50000, 0xFBE100D8,
    0x33185C8C, 0xFB2100A8, 0x83FB0010, 0x61190000, 0xFB4100B0, 0x9061007C,
    0x60FA0000, 0x38600001, 0x7EC1B014, 0xFAE10098, 0x889E0000, 0x60770000,
    0x5484103A, 0x7C98202E, 0xFAA10088, 0x7C8903A6, 0x4E800420,
]

# The table at 0x013e5c8c: 17 code addresses, then the zero word that ends it.
TABLE_BASE = 0x013E5C8C
TABLE_WORDS = [
    0x00AE517C, 0x00AE51F0, 0x00AE5284, 0x00AE5328, 0x00AE5580, 0x00AE53D4,
    0x00AE55D0, 0x00AE5630, 0x00AE5484, 0x00AE5528, 0x00AE56A8, 0x00AE5720,
    0x00AE57D8, 0x00AE5818, 0x00AE5424, 0x00AE5858, 0x00AE58B8, 0x00000000,
]

BCTR = 0x00AE5178
TEXT_LO, TEXT_HI = 0x10000, 0x01326860                  # rdr's own text extent


class FakeElf:
    """A sparse image: only the two regions this test needs."""

    def __init__(self, words):
        self.words = words

    def read(self, addr, size):
        if addr in self.words and size == 4:
            return struct.pack(">I", self.words[addr])
        return None


class FakeImg:
    def __init__(self, words):
        self.words = words
        self.elf = FakeElf(words)
        self.profile = ALL_RULES
        self.toc = 0
        self.text_lo = TEXT_LO
        self.text_hi = TEXT_HI
        self.current_function = None
        self.manual_tables = {}

    def word(self, addr):
        return self.words[addr]

    def in_text(self, v):
        return TEXT_LO <= v < TEXT_HI and v % 4 == 0

    def toc_word(self, disp):
        return 0


def fixture_words():
    words = {}
    for k, w in enumerate(DISPATCH_WORDS):
        words[WINDOW_LO + k * 4] = w
    for k, w in enumerate(TABLE_WORDS):
        words[TABLE_BASE + k * 4] = w
    return words


def case_fixture_matches_the_elf():
    """The fixture is the real bytes, checked against the real ELF when it is
    here. Without this the test could pass on a stale copy of the code."""
    if not os.path.exists(ELF):
        return "SKIP: no %s on this machine; the fixture is unverified" % (
            os.path.relpath(ELF, ROOT))
    from inventory import Elf
    elf = Elf(ELF)
    bad = []
    for k, w in enumerate(DISPATCH_WORDS):
        a = WINDOW_LO + k * 4
        got = elf.u32(a)
        if got != w:
            bad.append((a, w, got))
    for k, w in enumerate(TABLE_WORDS):
        a = TABLE_BASE + k * 4
        got = elf.u32(a)
        if got != w:
            bad.append((a, w, got))
    assert not bad, "fixture differs from the ELF at %s" % (
        ", ".join("0x%08x fixture 0x%08x elf 0x%08x" % b for b in bad[:4]))
    return "the 36 dispatcher words and 18 table words match %s" % (
        os.path.relpath(ELF, ROOT))


def case_bctr_is_a_bcctr():
    """The bctr must be the bcctr, not the return address the crash report
    carried. The report said lr=0x00adc030, and 0x00adc030 is the instruction
    AFTER the bl at 0x00adc02c -- keying the table on it would never fire."""
    assert fixture_words()[BCTR] == 0x4E800420, "0x%08x is not bcctr" % BCTR
    return "the dispatch is 0x%08x = 4e800420 bcctr (not lr 0x00adc030)" % BCTR


def case_real_window_recovers_the_table():
    img = FakeImg(fixture_words())
    img.current_function = (DISPATCH_LO, None)
    jt = jump_table(img, BCTR)
    assert jt is not None, "the real dispatcher window was rejected"
    assert jt["base"] == TABLE_BASE, "base 0x%08x, expected 0x%08x" % (
        jt["base"], TABLE_BASE)
    assert jt["count"] == 17, "count %d, expected 17" % jt["count"]
    assert jt["targets"] == TABLE_WORDS[:17], "targets differ from the table"
    return "real window: base 0x%08x, %d targets, entry 13 = 0x%08x" % (
        jt["base"], jt["count"], jt["targets"][13])


def case_zero_word_ends_the_run():
    """The extent is the contiguous run: the 18th word is zero and must not be
    taken as an 18th case."""
    img = FakeImg(fixture_words())
    img.current_function = (DISPATCH_LO, None)
    jt = jump_table(img, BCTR)
    assert jt["count"] == 17, "the zero word was counted as a case"
    return "the zero word at 0x%08x ends the run at 17 entries" % (
        TABLE_BASE + 17 * 4)


def case_targets_are_labels_on_the_real_image():
    """The claim the hand-written table makes: all 17 are labels of the
    function that dispatches. This is the check that the switch is real and not
    a table of function pointers."""
    if not os.path.exists(ELF):
        return "SKIP: no %s; the label check needs the whole function" % (
            os.path.relpath(ELF, ROOT))
    img = Image(ELF, None, ALL_RULES)
    img.current_function = (DISPATCH_LO, None)
    jt = jump_table(img, BCTR)
    assert jt is not None and jt["count"] == 17, "the real image gave %s" % (
        None if jt is None else jt["count"])
    img.current_function = (DISPATCH_LO, None)
    f = explore(img, DISPATCH_LO, set())
    missing = [t for t in jt["targets"] if t not in f.insns]
    assert not missing, "not labels of the function: %s" % (
        ", ".join("0x%08x" % m for m in missing))
    assert not f.indirect_jumps, "indirect jumps left: %s" % (
        ", ".join("0x%08x" % x for x in f.indirect_jumps))
    return "all 17 are labels of f_00ae50f0 (%d insns), 0 indirect jumps left" % (
        len(f.insns))


def case_every_bcctr_site_uses_the_same_table():
    """The dispatcher re-reads the same table after every opcode: 17 bcctr in
    the function, all 17 resolving to base 0x013e5c8c. A detector that found
    only the first one would leave 16 indirect jumps behind."""
    if not os.path.exists(ELF):
        return "SKIP: no %s; the per-site check needs the whole function" % (
            os.path.relpath(ELF, ROOT))
    img = Image(ELF, None, ALL_RULES)
    img.current_function = (DISPATCH_LO, None)
    f = explore(img, DISPATCH_LO, set())
    sites = sorted(f.jump_tables)
    assert len(sites) == 17, "%d dispatch sites resolved, expected 17" % len(sites)
    bases = {f.jump_tables[s]["base"] for s in sites}
    assert bases == {TABLE_BASE}, "bases %s" % (
        ", ".join("0x%08x" % b for b in bases))
    return "17 bcctr sites (0x%08x..0x%08x) all resolve to 0x%08x" % (
        sites[0], sites[-1], TABLE_BASE)


def main():
    checks = [
        case_fixture_matches_the_elf(),
        case_bctr_is_a_bcctr(),
        case_real_window_recovers_the_table(),
        case_zero_word_ends_the_run(),
        case_targets_are_labels_on_the_real_image(),
        case_every_bcctr_site_uses_the_same_table(),
    ]
    skipped = 0
    for c in checks:
        if c.startswith("SKIP"):
            skipped += 1
            print("skip " + c)
        else:
            print("ok   " + c)
    ran = len(checks) - skipped
    print("%d checks passed, %d skipped" % (ran, skipped))
    # A skip is not a pass: say so in the exit code when nothing ran on the
    # real image, so a tree without the disc cannot report a green rule.
    if ran <= 3:
        print("nothing ran on the real image: the fixture-only checks are not "
              "evidence for the rule", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
