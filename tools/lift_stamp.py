#!/usr/bin/env python3
"""lift_stamp.py -- say which seeds the generated PPU code came from.

Run at CMake configure time. Prints one line and exits 0 always: this is a
report, not a gate. The gate is in tools/setup.py, which relifts when an input
is newer than the generated code.

Why a stamp and not just a timestamp check: a lift is a pure function of
(ELF, seeds, switch tables, images.json), and two lifts that read different seed
sets can produce the same function count. The fingerprint is what tells them
apart. On 2026-11-13 out-rdr/ppu was found with no f_00b58430 -- the cellFiber
entry the title calls -- and no artifact recorded which seeds produced it.

Usage: lift_stamp.py <repo-root> [--out-root out-rdr]
Prints: "lift: <N> functions, seeds <sha256[0:16]>, <stale|fresh>"
"""
import hashlib
import os
import re
import sys


def seed_fingerprint(gen_root):
    seeds_dir = os.path.join(gen_root, "seeds")
    h = hashlib.sha256()
    n = 0
    if os.path.isdir(seeds_dir):
        for name in sorted(os.listdir(seeds_dir)):
            p = os.path.join(seeds_dir, name)
            if not os.path.isfile(p):
                continue
            h.update(name.encode())
            h.update(b"\0")
            with open(p, "rb") as f:
                for chunk in iter(lambda: f.read(1 << 20), b""):
                    h.update(chunk)
            h.update(b"\0")
            n += 1
    return h.hexdigest(), n


def function_count(table_path):
    try:
        with open(table_path, encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError:
        return None
    m = re.search(r"const FuncEntry functions\[\] = \{", text)
    if not m:
        return None
    return text.count(", f_", m.end())


def newest(paths):
    best = 0.0
    for p in paths:
        try:
            best = max(best, os.path.getmtime(p))
        except OSError:
            pass
    return best


def main(argv):
    root = argv[0] if argv else os.getcwd()
    out_root = "out-rdr"
    if "--out-root" in argv:
        out_root = argv[argv.index("--out-root") + 1]
    ppu = os.path.join(root, out_root, "ppu")
    gen = os.path.join(root, out_root, "gen")
    table = os.path.join(ppu, "table.cpp")
    if not os.path.exists(table):
        print("lift: no generated code at %s" % table)
        return 0
    fp, nseeds = seed_fingerprint(gen)
    nfuncs = function_count(table)
    inputs = [os.path.join(gen, "switch_tables.toml"), os.path.join(gen, "lle_layout.json")]
    seeds_dir = os.path.join(gen, "seeds")
    if os.path.isdir(seeds_dir):
        inputs += [os.path.join(seeds_dir, n) for n in os.listdir(seeds_dir)]
    stale = newest(inputs) > os.path.getmtime(table)
    print("lift: %s functions, seeds %s (%d file(s)), %s"
          % (nfuncs if nfuncs is not None else "?", fp[:16], nseeds,
             "STALE -- an input is newer than table.cpp" if stale else "fresh"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
