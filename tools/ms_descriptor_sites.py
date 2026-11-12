#!/usr/bin/env python3
"""Every caller of cellMSSystemGetNeededMemorySize / cellMSSystemInit, and the
descriptor each one passes -- read out of the guest ELF, not retyped.

The 0x920054 branch in cellMSSystemInit is reached only from descriptor word 3.
This tool answers, from the binary, which callers exist and what word 3 is at
each, so "the literal 0x10ca is the only producer of bit 0x40" is a scan and not
an inference.

    $ tools/ms_descriptor_sites.py bin/rdr/EBOOT.elf
"""
import struct
import sys


class Image:
    """The executable PT_LOAD segments of a big-endian ELF64, by guest address."""

    def __init__(self, data):
        self.data = data
        self.segs = []
        e_phoff = struct.unpack_from(">Q", data, 32)[0]
        ph = struct.unpack_from(">H", data, 54)[0]
        pn = struct.unpack_from(">H", data, 56)[0]
        for i in range(pn):
            off = e_phoff + i * ph
            t, fl = struct.unpack_from(">II", data, off)
            o, v, _pa, fsz, _msz = struct.unpack_from(">QQQQQ", data, off + 8)
            if t == 1 and (fl & 1) and fsz:
                self.segs.append((v, o, fsz))

    def read(self, a, n):
        for v, o, sz in self.segs:
            if v <= a < v + sz:
                return self.data[o + (a - v):o + (a - v) + n]
        return None

    def word(self, a):
        b = self.read(a, 4)
        return struct.unpack(">I", b)[0] if b else None

    def callers_of(self, target):
        """Every `bl` whose resolved target is `target`, by link address."""
        out = []
        for v, o, sz in self.segs:
            for i in range(0, sz - 3, 4):
                w = struct.unpack_from(">I", self.data, o + i)[0]
                if branch_target(v + i, w) == target and (w & 1):
                    out.append(v + i)
        return out

    def descriptor_stores(self, call_site, want=4):
        """The four `stw` immediately before a call that build its argument object."""
        stores = []
        for back in range(4, 44, 4):
            a = call_site - back
            w = self.word(a)
            if w is None:
                break
            if (w >> 26) == 36:
                stores.append((a, (w >> 21) & 31, w & 0xFFFF, (w >> 16) & 31))
        return list(reversed(stores[:want]))

    def immediates(self, value):
        """Every instruction that forms `value` as a 16-bit immediate."""
        out = []
        for v, o, sz in self.segs:
            for i in range(0, sz - 3, 4):
                w = struct.unpack_from(">I", self.data, o + i)[0]
                if (w & 0xFFFF) == (value & 0xFFFF) and (w >> 26) in (12, 13, 14, 15, 24, 25):
                    out.append((v + i, w >> 26, w))
        return out


def branch_target(pc, w):
    """Resolve a branch's target. AA (bit 1) selects absolute addressing."""
    if (w >> 26) != 18:
        return None
    li = w & 0x03FFFFFC
    if li & 0x02000000:
        li -= 0x04000000
    return (li if w & 2 else pc + li) & 0xFFFFFFFF


GETMEM, INIT = 0x0091F364, 0x0091FD18
DESCRIPTOR_LITERAL = 0x10CA


def main(path):
    img = Image(open(path, "rb").read())
    print("cellMSSystemGetNeededMemorySize f_%08x callers:" % GETMEM)
    for site in img.callers_of(GETMEM):
        print("  bl at 0x%08x -> return address 0x%08x" % (site, site + 4))
        for a, rt, disp, ra in img.descriptor_stores(site):
            print("    stw r%d, 0x%x(r%d) at 0x%08x" % (rt, disp, ra, a))
    print("cellMSSystemInit f_%08x callers:" % INIT)
    for site in img.callers_of(INIT):
        print("  bl at 0x%08x -> return address 0x%08x" % (site, site + 4))
    print()
    print("every instruction forming the immediate 0x%04x in the executable segment:"
          % DESCRIPTOR_LITERAL)
    for a, op, w in img.immediates(DESCRIPTOR_LITERAL):
        print("  0x%08x op%-2d 0x%08x" % (a, op, w))
    print()
    print("f_0091f364 masks word3 with rlwinm ..,0x1b / 0x1a / 0x19 (bits 4,5,6) and")
    print("adds one to a counter for each set bit. 0x%04x = 0b%s: bit4=%d bit5=%d bit6=%d."
          % (DESCRIPTOR_LITERAL, bin(DESCRIPTOR_LITERAL)[2:],
             (DESCRIPTOR_LITERAL >> 4) & 1, (DESCRIPTOR_LITERAL >> 5) & 1,
             (DESCRIPTOR_LITERAL >> 6) & 1))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "bin/rdr/EBOOT.elf")
