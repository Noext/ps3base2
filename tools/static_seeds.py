#!/usr/bin/env python3
"""static_seeds.py -- seeds a lift can find without running the game.

Two kinds, per image:

  (a) RELOCATION TARGETS. A .prx carries R_PPC64_ADDR32 relocations against its
      own code: a function pointer stored in a data segment. Every addend that
      lands in the code segment is an address the module itself says is code.

  (b) FUNCTION DESCRIPTORS. A pair {u32 code, u32 toc} in a data segment where
      code is inside the code segment and toc equals the image TOC. That is the
      PowerPC64 ABI function descriptor -- an OPD entry -- and the same shape
      the loader uses to start a thread.

Both are facts the binary states about itself, so they need no run. The dynamic
loop (PS3BASE_DISCOVERY) remains the safety net for what neither reaches.

Usage:
    tools/static_seeds.py --elf <image.elf> [--out <seeds.txt>] [--json]
"""
import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "recomp", "ppu"))

# PowerPC64 relocation types a module uses to point at its own code.
# PowerPC64 relocation types, as tools/prx_relocate.py names them. The ABSOLUTE
# ones carry an address in the module's own space:
#
#   1  R_ADDR32      a full 32-bit address
#   4  R_ADDR16_LO   the low 16 bits of the address
#   5  R_ADDR16_HI   the high 16 bits
#   6  R_ADDR16_HA   the high 16 bits, adjusted for the low half's sign
#   38 R_ADDR64      a full 64-bit address
#   57 R_ADDR16_LO_DS a low 16 with the two format bits kept
#
# 4 and 6 usually appear as a lis/addi pair in the CODE, building an absolute
# address at run time -- which is how a function pointer that never sits in a
# data segment still names a function. An earlier version of this file treated
# them as relative branches and skipped them, so those targets were never seeds.
#
# The RELATIVE ones (10 R_REL24, 11 R_REL14, 44 R_REL64) are direct branches and
# are NOT collected: the control-flow analysis already follows those.
ABSOLUTE_RELOCS = {1, 4, 5, 6, 38, 57}
RELATIVE_RELOCS = {10, 11, 44}

def read_elf(path):
    d = open(path, "rb").read()
    if d[:4] != b"\x7fELF":
        raise SystemExit("%s: not an ELF" % path)
    e_phoff, = struct.unpack(">Q", d[0x20:0x28])
    phes, phn = struct.unpack(">HH", d[0x36:0x3A])
    phdrs = []
    for i in range(phn):
        o = e_phoff + i * phes
        t, fl = struct.unpack(">II", d[o:o + 8])
        off, va = struct.unpack(">QQ", d[o + 8:o + 24])
        fsz, msz = struct.unpack(">QQ", d[o + 0x20:o + 0x30])
        phdrs.append({"type": t, "flags": fl, "off": off, "vaddr": va, "filesz": fsz, "memsz": msz})
    return d, phdrs


def code_range(phdrs):
    """The executable PT_LOAD: p_vaddr .. p_vaddr + p_memsz, whole.

    This is the same rule analysis.py uses after the text_hi fix. Deriving it
    twice from the same header is the point: a seed and the lift must agree on
    what counts as code, or the seed is silently dropped.
    """
    lo = hi = None
    for p in phdrs:
        if p["type"] != 1 or not (p["flags"] & 1) or not p["memsz"]:
            continue
        lo = p["vaddr"] if lo is None else min(lo, p["vaddr"])
        hi = p["vaddr"] + p["memsz"] if hi is None else max(hi, p["vaddr"] + p["memsz"])
    return lo, hi


def image_toc(d, phdrs, path):
    """The table-of-contents register value, from the module header.

    A .prx header carries it (prx_param in tools/inventory.py reads it at
    offset 32 of the first segment). Guessing it instead -- "the second word of
    the first {code, toc}-shaped pair in a data segment" -- picks up whichever
    pair happens to come first, and libsysutil has a stray one at its very first
    data word. That guess produced 0x2b01cbf8 where the header says 0x2b0276b0,
    so the descriptor scan matched exactly one pair per image and looked broken.

    For a plain ELF with no .prx header, fall back to the OPD: the first entry
    is the module entry, and its toc is the image toc.
    """
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
    from inventory import Elf, prx_param
    elf = Elf(path)
    prx = prx_param(elf)
    if prx and prx.get("toc"):
        # A .prx states its own TOC in its header. Trust it: it is what the
        # loader uses, and prx_relocate.py rewrites the value the loader reads.
        return int(prx["toc"], 16)
    # A plain disc ELF (an EBOOT) has no .prx header. Its TOC is the second word
    # of its process-parameter OPD, which is where the loader takes it from --
    # not the first {code, toc} pair to be found in a data segment. Measured on
    # Red Dead Redemption: the OPD says 0x0139ebb0 and that yields 51,565
    # descriptors; the first-pair guess said 0x0114bc1c and yielded one.
    # The OPD table of a disc executable: entries are {code, toc} and the TOC is
    # overwhelmingly the same in all of them, so the most common value wins.
    from inventory import opd
    o = opd(elf)
    if o and o.get("tocs"):
        best = max(o["tocs"].items(), key=lambda kv: kv[1])[0]
        return int(best, 16)
    return None


def reloc_seeds(d, phdrs, lo, hi):
    """(a) relocation targets that land in the code segment."""
    out = set()
    # A .prx that has not been relocated has base 0 and stores addends relative
    # to the module; one that has been relocated stores absolute addresses. Try
    # the raw value first and the segment base as a fallback, so the tool gives
    # the same answer either side of prx_relocate.py.
    # Segment bases, so an addend can be resolved to a virtual address. An
    # unrelocated .prx has base 0 and stores addends relative to the module; a
    # relocated ELF keeps the same relative addends in its relocation table, so
    # the same arithmetic works on both -- measured, not assumed.
    loads = [p["vaddr"] for p in phdrs if p["type"] == 1 and not (p["flags"] & 1) and p["memsz"]]
    base_add = lo if lo else 0
    for p in phdrs:
        if p["type"] != 0x700000A4:
            continue
        rel = d[p["off"]:p["off"] + p["filesz"]]
        for i in range(0, len(rel), 24):
            _offset, _info, index_value, _index_addr, rtype, addend = struct.unpack(">QHBBIQ", rel[i:i + 24])
            if rtype in RELATIVE_RELOCS or rtype not in ABSOLUTE_RELOCS:
                continue
            # The addend is relative to the segment named by index_value, or
            # absolute when index_value is 0xFF (no reference segment).
            if index_value == 0xFF:
                seg_base = 0
            elif index_value < len(loads):
                seg_base = loads[index_value]
            else:
                seg_base = 0
            target = (seg_base + (addend & 0xFFFFFFFF)) & 0xFFFFFFFF
            # An ADDR16_HA addend carries a carry adjustment that can push the
            # sum one 32-bit word past the true target; the LO half of the same
            # pair names it exactly, so both are collected and the code-range
            # test below drops whatever lands outside.
            for candidate in (target, target - 0x10000):
                if lo <= candidate < hi and candidate % 4 == 0:
                    out.add(candidate)
    return out


def descriptor_seeds(d, phdrs, lo, hi, toc):
    """(b) {code, toc} pairs in a data segment: OPD-style function descriptors."""
    out = set()
    if toc is None:
        return out
    for p in phdrs:
        if p["type"] != 1 or (p["flags"] & 1) or not p["filesz"]:
            continue
        blob = d[p["off"]:p["off"] + p["filesz"]]
        for k in range(0, len(blob) - 7, 4):
            code, t = struct.unpack_from(">II", blob, k)
            if t == toc and lo <= code < hi and code % 4 == 0:
                out.add(code)
    return out


def code_tables(d, phdrs, lo, hi):
    """Runs of code addresses in a data segment: jump tables, not descriptors.

    A {code, toc} descriptor and a jump table look nothing alike in memory. The
    descriptor alternates code and TOC; a jump table is a run of code addresses
    with no TOC anywhere near it:

        013f64a8: 00d19ce4  00d19d10  00d19d30  00d19e04  00d19e9c  00d19f48

    Red Dead Redemption dispatches through these, and an indirect target that
    lives in one is invisible to every other scan: not an OPD (no TOC), not a
    lis/addi pair (the address is in a table, not in code), not a relocation (a
    disc ELF has none). That is why 0x00d19e04 and 0x00d1bbec stayed missing while
    38,522 descriptors were found.

    A run is three or more consecutive 4-byte words inside the code segment.
    Three rather than two because two code pointers next to each other are
    common in ordinary data; a run of three is a dispatch table.
    """
    out = set()
    for p in phdrs:
        if p["type"] != 1 or (p["flags"] & 1) or not p["filesz"]:
            continue
        blob = d[p["off"]:p["off"] + p["filesz"]]
        run = []
        for k in range(0, len(blob) - 3, 4):
            code, = struct.unpack_from(">I", blob, k)
            if lo <= code < hi and code % 4 == 0:
                run.append(code)
                continue
            if len(run) >= 3:
                out.update(run)
            run = []
        if len(run) >= 3:
            out.update(run)
    return out

def code_address_seeds(d, phdrs, lo, hi):
    """Addresses built at run time by a lis/addi (or lis/ori) pair in the code.

    A disc executable has no relocation table: an absolute address in its code
    is a literal, split across two instructions. `lis rX, hi` then `addi/ori rX,
    rX, lo` reconstructs it, and when the result lands in the code segment it is
    a function address the module builds for itself -- typically for an indirect
    call it will later make.

    Only adjacent pairs in the same basic block are considered, and the
    intervening instruction must not write the same register, which keeps the
    false-positive rate low. The lift then decides whether the target is a real
    function entry; this only proposes.
    """
    out = set()
    for p in phdrs:
        if p["type"] != 1 or not (p["flags"] & 1) or not p["filesz"]:
            continue
        blob = d[p["off"]:p["off"] + p["filesz"]]
        n = len(blob) - 15
        for k in range(0, n, 4):
            w0 = int.from_bytes(blob[k:k + 4], "big")
            if (w0 >> 26) != 15:            # addis
                continue
            rt0 = (w0 >> 21) & 31
            if rt0 == 0:
                continue
            hi_val = w0 & 0xFFFF
            for step in (4, 8):             # adjacent, or one instruction between
                k2 = k + step
                if k2 + 4 > len(blob):
                    break
                w1 = int.from_bytes(blob[k2:k2 + 4], "big")
                op1 = w1 >> 26
                if op1 not in (14, 24):     # addi, ori
                    continue
                rt1 = (w1 >> 21) & 31
                ra1 = (w1 >> 16) & 31
                if rt1 != rt0 or ra1 != rt0:
                    continue
                imm = w1 & 0xFFFF
                if op1 == 14 and imm & 0x8000:
                    continue                # a negative addi is not a high-half build
                addr = ((hi_val << 16) | imm) & 0xFFFFFFFF
                if lo <= addr < hi and addr % 4 == 0:
                    out.add(addr)
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", required=True)
    ap.add_argument("--out", default="")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--name", default="")
    ap.add_argument("--tables", action="store_true",
                    help="include jump-table targets. OFF by default: the first run of this "
                         "scan added 3,460 functions at once and the title segfaulted in its "
                         "renderer. A table word is often a switch LABEL inside a function "
                         "already found, not a new entry, so lifting it as one splits a "
                         "function. Enable only with that classification in place.")
    ap.add_argument("--code-addresses", action="store_true",
                    help="also scan lis/addi pairs (useful for an EBOOT, which has no relocations)")
    args = ap.parse_args()

    d, phdrs = read_elf(args.elf)
    lo, hi = code_range(phdrs)
    if lo is None:
        raise SystemExit("%s: no executable segment" % args.elf)
    toc = image_toc(d, phdrs, args.elf)
    rel = reloc_seeds(d, phdrs, lo, hi)
    desc = descriptor_seeds(d, phdrs, lo, hi, toc)
    # Off unless asked for: see --tables. A table word is usually a switch label.
    tables = code_tables(d, phdrs, lo, hi) if args.tables else set()
    code_addrs = code_address_seeds(d, phdrs, lo, hi) if args.code_addresses else set()
    allseeds = rel | desc | code_addrs | tables

    name = args.name or os.path.basename(args.elf)
    info = {"image": name, "text_lo": lo, "text_hi": hi, "toc": toc,
            "relocation_seeds": len(rel), "descriptor_seeds": len(desc),
            "code_address_seeds": len(code_addrs), "total": len(allseeds)}
    if args.json:
        print(json.dumps(info))
    else:
        print("%-24s code 0x%08x-0x%08x  reloc %5d  descriptors %5d  code-addr %5d  total %5d"
              % (name, lo, hi, len(rel), len(desc), len(code_addrs), len(allseeds)))

    if args.out:
        with open(args.out, "w", newline="\n") as f:
            for a in sorted(allseeds):
                f.write("0x%08x %s\n" % (a, name))
    return 0


if __name__ == "__main__":
    sys.exit(main())