#!/usr/bin/env python3
"""lle_inventory.py -- answer "is this library served, and how?" from measurements.

For every module named on the command line, reports:

  imported   how many functions the title's import table requests from it
  LLE        of those, how many a loaded lifted firmware image exports
  HLE        of those, how many a runtime handler implements
  NONE       of those, how many resolve to nothing (a call that goes nowhere)
  prx        whether a PRX exporting the module exists in the firmware directory

An import is served LLE iff some loaded image exports the same (module, NID)
pair -- that is what ppu_runtime.cpp:179 resolve() checks first. It is served
HLE iff hle::find(module, name) finds a handler. Otherwise the call falls through
to "unimplemented" and returns nothing, which is the case worth knowing about.

Usage:
  lle_inventory.py --table <ppu/table.cpp> --fw <fw/version> [--images a.json]
                   [--hle runtime/src/hle] module [module ...]

The table is the generated one the runtime is built against, so the answer is
about the binary that runs, not about the recipes.
"""
import glob
import json
import os
import re
import struct
import sys

SUFFIX = bytes.fromhex("6759659904250490566427499489741A")


def nid(name):
    import hashlib
    return int.from_bytes(hashlib.sha1(name.encode() + SUFFIX).digest()[:4], "little")


def read_table(path):
    t = open(path, encoding="utf-8", errors="replace").read()
    images = re.findall(r'\{"([^"]+)", "([^"]+)", (true|false), ', t)
    imp = t[t.index("const ImportEntry imports[]"):]
    imp = imp[:imp.index("};")]
    imports = [(m, int(n, 16), nm, int(img))
               for m, n, nm, img in re.findall(
                   r'\{"([^"]+)", 0x([0-9a-fA-F]+)u, "([^"]*)", (\d+)\}', imp)]
    exp = t[t.index("const ExportEntry exports[]"):]
    exp = exp[:exp.index("};")]
    exports = {(m, int(n, 16))
               for m, n, _nm, _img, _opd in re.findall(
                   r'\{"([^"]+)", 0x([0-9a-fA-F]+)u, "([^"]*)", (\d+), 0x([0-9a-fA-F]+)\}', exp)}
    return [i[0] for i in images], imports, exports


def hle_handlers(hle_dir):
    out = set()
    for f in glob.glob(os.path.join(hle_dir, "*.cpp")):
        s = open(f, encoding="utf-8", errors="replace").read()
        for m, n in re.findall(r'HLE_FUNC\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*\)', s):
            out.add((m, n))
    return out


def prx_modules(fw_dir):
    """module name -> PRX file, for every PRX in a decrypted firmware directory."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from inventory import Elf, prx_param, exports as inv_exports
    nids = {}
    here = os.path.dirname(os.path.abspath(__file__))
    db = os.path.join(here, "..", "ref", "nid_db.json")
    if os.path.exists(db):
        nids = json.load(open(db))
    found = {}
    for p in sorted(glob.glob(os.path.join(fw_dir, "*.prx"))):
        try:
            e = Elf(p)
            prx = prx_param(e)
            if not prx:
                continue
            for m in inv_exports(e, prx, nids):
                if m["module"]:
                    found.setdefault(m["module"], []).append(os.path.basename(p))
        except Exception:
            continue
    return found


def main(argv):
    ap = {"--table": None, "--fw": None, "--hle": "runtime/src/hle"}
    mods = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ap:
            ap[a] = argv[i + 1]
            i += 2
            continue
        mods.append(a)
        i += 1
    if not ap["--table"] or not mods:
        print(__doc__)
        return 2
    images, imports, exports = read_table(ap["--table"])
    hle = hle_handlers(ap["--hle"])
    prx = prx_modules(ap["--fw"]) if ap["--fw"] else {}

    print("%-16s %8s %6s %6s %6s  %s" % ("module", "imported", "LLE", "HLE", "NONE", "prx exporting it"))
    worst = 0
    for want in mods:
        rows = [r for r in imports if r[0] == want]
        lle = sum(1 for m, n, _nm, _i in rows if (m, n) in exports)
        h = sum(1 for m, n, nm, _i in rows if (m, n) not in exports and (m, nm) in hle)
        none = len(rows) - lle - h
        worst = max(worst, none)
        print("%-16s %8d %6d %6d %6d  %s"
              % (want, len(rows), lle, h, none, ", ".join(prx.get(want, [])) or "(no PRX in this firmware)"))
    print("\nimages in the table: %s" % ", ".join(images))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
