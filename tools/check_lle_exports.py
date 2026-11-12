#!/usr/bin/env python3
"""check_lle_exports.py -- prove a lifted module is internally consistent.

A module can be in the image table and still be useless: the lifter may have
dropped an export, or lifted an export's function descriptor without lifting the
code it points at. Both faults are silent -- resolve() finds no export, or finds
one whose opd reads a code address with no function, and the call goes nowhere.

For each (module.elf, module-name) pair this checks every export the PRX
declares against a generated PPU table:

  1. the (module, NID) pair is present in exports[] and its opd is non-zero and
     equal to the PRX's own view of that opd;
  2. the descriptor's FIRST word -- the code address, the same word the runtime
     loads at ppu_runtime.cpp:790 (`code = ld32(base, e.opd)`) -- is an entry in
     functions[]. A descriptor whose code was never lifted resolves and then
     calls a null dispatch slot.

The unnamed export set (module_start / module_stop / PRX entry points) is skipped:
the generated table stores those with an empty module name and the runtime
reaches them through find_special_export(), not through resolve().

Usage: check_lle_exports.py <table.cpp> <module.elf> <module-name> [...]
Exit code 0 when every export checks out, 1 otherwise.
"""
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
from inventory import Elf, prx_param, exports as inv_exports


def read_table(path):
    t = open(path, encoding="utf-8", errors="replace").read()
    exp = t[t.index("const ExportEntry exports[]"):]
    exp = exp[:exp.index("};")]
    exports = {}
    for m, nid, _name, _img, opd in re.findall(
            r'\{"([^"]+)", 0x([0-9a-fA-F]+)u, "([^"]*)", (\d+), 0x([0-9a-fA-F]+)\}', exp):
        exports[(m, int(nid, 16))] = int(opd, 16)
    funcs = {int(a, 16) for a in re.findall(r'\{0x([0-9a-fA-F]+), f_', t)}
    # The module text ranges, so a DATA export (sys_prx_version is the integer
    # 0x00010001, not a descriptor) is not mistaken for a descriptor pointing at
    # unlifted code. A code word is only suspicious when it lands inside an
    # image's own text.
    ranges = [(int(lo, 16), int(hi, 16))
              for lo, hi in re.findall(r'\{"[^"]+", "[^"]+", (?:true|false), 0x([0-9a-fA-F]+), 0x([0-9a-fA-F]+),', t)]
    return exports, funcs, ranges


def prx_exports(elf_path, nids):
    """(module, nid, opd) for every NAMED export of a relocated PRX ELF."""
    e = Elf(elf_path)
    prx = prx_param(e)
    out = []
    for m in inv_exports(e, prx, nids):
        if not m["module"]:
            continue
        for ent in m["entries"]:
            out.append((m["module"], int(ent["nid"], 16), int(ent["addr"], 16)))
    return out


def main(table, *pairs):
    exports, funcs, ranges = read_table(table)
    nids = json.load(open("ref/nid_db.json"))
    bad = 0
    for i in range(0, len(pairs), 2):
        elf_path, want_module = pairs[i], pairs[i + 1]
        rows = [r for r in prx_exports(elf_path, nids) if r[0] == want_module]
        if not rows:
            print("%s: no exports named %s" % (elf_path, want_module))
            bad += 1
            continue
        e = Elf(elf_path)
        missing = zero = mismatch = nocode = 0
        for _m, nid, opd in rows:
            got = exports.get((want_module, nid))
            if got is None:
                print("  %-22s %08X opd 0x%08x  MISSING from exports[]" % (want_module, nid, opd))
                missing += 1
                continue
            if got == 0:
                print("  %-22s %08X opd 0x%08x  OPD-ZERO" % (want_module, nid, opd))
                zero += 1
                continue
            if got != opd:
                print("  %-22s %08X opd 0x%08x != table 0x%08x" % (want_module, nid, opd, got))
                mismatch += 1
                continue
            word = e.read(opd, 8)
            if not word or len(word) < 8:
                print("  %-22s %08X opd 0x%08x  UNREADABLE" % (want_module, nid, opd))
                nocode += 1
                continue
            code, = struct.unpack(">I", word[0:4])
            in_text = any(lo <= code < hi for lo, hi in ranges)
            if in_text and code not in funcs:
                print("  %-22s %08X opd 0x%08x -> code 0x%08x NOT in functions[]"
                      % (want_module, nid, opd, code))
                nocode += 1
        ok = len(rows) - missing - zero - mismatch - nocode
        print("%-14s %3d exports: %d ok, %d missing, %d zero-opd, %d mismatched, %d no-code"
              % (want_module, len(rows), ok, missing, zero, mismatch, nocode))
        bad += missing + zero + mismatch + nocode
    print("FAIL" if bad else "PASS")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
