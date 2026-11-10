#!/usr/bin/env python3
"""layout_for.py -- place the firmware modules above a title, computed rather than fixed.

The fixed layouts (recomp/lle_layout.json and its per-title copies) are a trap.
They put liblv2 at 0x01000000, which is correct only for a title whose text ends
below that address. Minecraft ends at 0x8aa674 and GTA at 0x6fccdc, so nobody
noticed; Red Dead Redemption ends at 0x10d546c, which swallows all eight modules
and produced five "multiple definition" errors at link time plus, worse, a
lookup() that would have resolved every liblv2 call to the game's own code.

So: compute the bases from the title's own extent.

    layout_for.py --elf bin/rdr/EBOOT.elf [--out recomp/lle_layout.json]

Rules:
  - start above text_hi, rounded up to a 16 MiB boundary;
  - keep every module and every module's data segment inside its own 16 MiB slot,
    which is what the fixed layout already assumed;
  - refuse, loudly, if a slot would overlap the title or another module.
"""
import argparse
import json
import os
import struct
import sys

# 16 MiB per module slot, matching the spacing the fixed layout used.
SLOT = 0x01000000
# The runtime's own slot allocator (lv2/memory.cpp: reserve_slot_locked) walks
# UPWARD from 0x10000000 on a 16 MiB alignment, handing out sys_memory_allocate
# and libsysutil's shared area. Placing firmware modules there would collide with
# every guest allocation, so modules go ABOVE the allocator's floor, not at it.
ALLOC_FLOOR = 0x10000000
# Modules live at 0x30000000 and up, which is where RPCS3 maps the additional
# segment for fixed allocations (PPUModule.cpp:2242: vm::map(0x30000000,
# 0x10000000)). An earlier version used 0x20000000, "above the runtime's slot
# allocator floor" -- and that floor is exactly the problem: reserve_slot_locked
# walks UP from 0x10000000 in 0x10000000 steps, so sys_mmapper_allocate_address
# hands out 0x20000000 for a 0x10000000-byte request and the guest is given the
# same memory the firmware modules were placed in.
#
# Measured on Red Dead Redemption: liblv2 at 0x20000000, libsysmodule at
# 0x21000000, ... and "mmapper: allocate_address 0x10000000 -> 0x20000000". The
# module's own data was then written by the guest, module_start parked in
# sys_cond_wait forever, and the screen stayed black at 0.2% draws.
MODULE_BAND = 0x30000000
MODULE_BAND_END = 0x3F000000
# Where the runtime's own fresh memory begins in the default layout.
RESERVED_FROM = 0xC0000000
# The compact layout (PS3BASE_REGIONS=compact, and the only layout wasm has)
# puts video at 0x50000000, so modules must stay below that.
COMPACT_VIDEO = 0x50000000


def read_elf_extent(path):
    """Return (text_lo, text_hi) of the code in a guest ELF."""
    d = open(path, "rb").read()
    if d[:4] != b"\x7fELF":
        raise SystemExit("%s: not an ELF" % path)
    if d[5] != 2:
        raise SystemExit("%s: not big-endian" % path)
    e_phoff, = struct.unpack(">Q", d[0x20:0x28])
    e_phentsize, e_phnum = struct.unpack(">HH", d[0x36:0x3A])
    lo, hi = None, None
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_flags = struct.unpack(">II", d[off:off + 8])
        p_vaddr, = struct.unpack(">Q", d[off + 0x10:off + 0x18])
        p_memsz, = struct.unpack(">Q", d[off + 0x28:off + 0x30])
        if p_type != 1:            # PT_LOAD
            continue
        if not (p_flags & 1):      # PF_X == executable
            continue
        if p_memsz == 0:
            continue
        lo = p_vaddr if lo is None else min(lo, p_vaddr)
        hi = p_vaddr + p_memsz if hi is None else max(hi, p_vaddr + p_memsz)
    if lo is None:
        raise SystemExit("%s: no executable PT_LOAD, cannot place modules" % path)
    return lo, hi


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", required=True, help="the title's decrypted ELF")
    ap.add_argument("--modules", default="recomp/lle_modules.txt",
                    help="one .prx name per line, in placement order")
    ap.add_argument("--slots-per-band", type=int, default=16,
                    help="modules per 16 MiB band before moving to the next")
    ap.add_argument("--out", default="", help="write the JSON here; default is stdout")
    args = ap.parse_args()

    text_lo, text_hi = read_elf_extent(args.elf)

    # The band must start above both the title's text and the runtime's allocator.
    base = ((max(text_hi, MODULE_BAND) + SLOT - 1) // SLOT) * SLOT
    if base < MODULE_BAND:
        base = MODULE_BAND

    names = [l.strip() for l in open(args.modules) if l.strip() and not l.startswith("#")]
    if not names:
        raise SystemExit("%s lists no modules" % args.modules)

    layout = []
    for i, name in enumerate(names):
        addr = base + i * SLOT
        if addr <= text_hi:
            raise SystemExit("slot %d at 0x%08x overlaps the title, which ends at 0x%08x"
                             % (i, addr, text_hi))
        if addr + SLOT > COMPACT_VIDEO:
            raise SystemExit("slot %d at 0x%08x would reach the compact layout's video "
                             "region at 0x%08x: too many modules for this band"
                             % (i, addr, COMPACT_VIDEO))
        layout.append({"file": name, "base": "0x%08X" % addr})

    # No two slots may share a range; the spacing guarantees it, but say so.
    seen = {}
    for m in layout:
        b = int(m["base"], 16)
        if b in seen:
            raise SystemExit("two modules share base 0x%08x" % b)
        seen[b] = m["file"]

    out = json.dumps(layout, indent=1) + "\n"
    if args.out:
        open(args.out, "w").write(out)
        print("layout_for: title text 0x%08x-0x%08x, modules from 0x%08x, wrote %s"
              % (text_lo, text_hi, base, args.out), file=sys.stderr)
    else:
        sys.stdout.write(out)


if __name__ == "__main__":
    main()
