#!/usr/bin/env python3
"""check_export_coverage.py -- the export coverage gate.

For every LLE module in a generated image table, every exported FUNCTION
(export table -> descriptor -> code address) must have a lifted function.
A module in the image table whose export resolves to an address the lifter
never produced is silently broken: resolve() finds the export, the runtime
loads the descriptor, and the call lands on a null dispatch slot.

The source of truth for "what must exist" is the PRX own libent chain, walked
here rather than through inventory.exports(), because that helper drops the
unnamed export set (module_start / module_stop / PRX entry). The source of
truth for "what the runtime will do" is the generated table: ppu_runtime.cpp
does `code = ld32(base, e.opd)` with e from image::exports[], so the TABLE opd
is what must be checked, and it must agree with the PRX.

Per exported function, in this order:
  1. the (module, NID) row is present in exports[];
  2. its opd is non-zero and equal to the PRX own view of that opd;
  3. the descriptor first word (the code address) is inside the image text
     and is an entry in functions[];
  4. the descriptor second word (the TOC) equals the image TOC.
Then per module: every one of its own imports resolves to an export or an
HLE handler. A module that cannot call its dependencies cannot run even with
every export lifted (libsail: 141/141 exports lifted, 49 of 90 imports dead).

Usage:
  check_export_coverage.py <table.cpp> [--root DIR] [--allow FILE] [--imports]
Exit 0 when every module every function export has lifted code, 1 otherwise.
"""
import argparse
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from inventory import Elf, prx_param, imports as inv_imports


def parse_table(path):
    t = open(path, encoding="utf-8", errors="replace").read()
    imgs = []
    blk = t[t.index("const ImageEntry images[]"):]
    blk = blk[:blk.index("};")]
    for m in re.finditer(
            r'\{"([^"]+)", "([^"]+)", (true|false), 0x([0-9a-fA-F]+), '
            r'0x([0-9a-fA-F]+), 0x([0-9a-fA-F]+), 0x([0-9a-fA-F]+)\}', blk):
        imgs.append(dict(name=m.group(1), file=m.group(2), prx=m.group(3) == "true",
                         text_lo=int(m.group(4), 16), text_hi=int(m.group(5), 16),
                         toc=int(m.group(6), 16), entry=int(m.group(7), 16)))
    fblk = t[t.index("const FuncEntry functions[]"):]
    fblk = fblk[:fblk.index("};")]
    funcs = {int(a, 16) for a in re.findall(r'\{0x([0-9a-fA-F]+), f_', fblk)}
    eblk = t[t.index("const ExportEntry exports[]"):]
    eblk = eblk[:eblk.index("};")]
    # Keyed by (module, nid, image), NOT (module, nid): the unnamed export set
    # (module_start / module_stop / PRX entry) has an EMPTY module name, and the
    # same three NIDs appear once per image. A (module, nid) key silently keeps
    # only the last image's row -- which is what check_lle_exports.py did.
    #
    # [^"]* and not [^"]+: that EMPTY module name is the second half of the same
    # bug. check_lle_exports.py's pattern required at least one character, so it
    # never parsed an unnamed row at all -- the very rows it then skipped with
    # `if not m["module"]: continue`.
    exps = {}
    for m in re.finditer(r'\{"([^"]*)", 0x([0-9a-fA-F]+)u, "([^"]*)", (\d+), 0x([0-9a-fA-F]+)\}', eblk):
        exps[(m.group(1), int(m.group(2), 16), int(m.group(4)))] = dict(
            name=m.group(3), opd=int(m.group(5), 16))
    return imgs, funcs, exps


def hle_names(root):
    """(module, name) pairs that have an HLE handler in the source tree."""
    out = set()
    hdir = os.path.join(root, "runtime", "src", "hle")
    if not os.path.isdir(hdir):
        return out
    for fn in os.listdir(hdir):
        if not fn.endswith(".cpp"):
            continue
        s = open(os.path.join(hdir, fn), encoding="utf-8", errors="replace").read()
        for m in re.finditer(r'HLE_FUNC\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*\)', s):
            out.add((m.group(1), m.group(2)))
    return out


def module_function_exports(elf, prx):
    """Every exported FUNCTION of a PRX: dict(module, nid, opd).

    The libent chain carries num_func, num_var, num_tls in that order and the
    address table is one array for all three, so the function entries are the
    first num_func of them. Variables and TLS are data, not code.
    """
    out = []
    addr, end = prx["libent"]
    while addr < end:
        b = elf.read(addr, 0x1C)
        if not b or len(b) < 0x1C:
            break
        size = b[0]
        num_func, num_var, num_tls = struct.unpack(">HHH", b[6:12])
        name_p, nid_p, addr_p = struct.unpack(">3I", b[0x10:0x1C])
        name = elf.cstr(name_p) if name_p else ""
        for i in range(num_func):
            nid = elf.u32(nid_p + i * 4)
            opd = elf.u32(addr_p + i * 4)
            out.append(dict(module=name, nid=nid, opd=opd))
        addr += size
    return out


def resolve_image(table_path, root, rel):
    """The image file a table row names.

    The table stores each image path relative to the tree the lift ran in, which
    is NOT necessarily the tree being configured: a build in a worktree points
    PS3BASE_GENERATED_DIR at another checkout, and every row then says
    "out-rdr/gen/reloc/libfiber.elf" -- correct for the lift tree, absent from
    the build tree. So try the generated tree first (three levels up from
    <tree>/<gen>/ppu/table.cpp), then --root. A gate that fails because it
    looked in the wrong directory is a false failure, and a false failure is
    how a gate gets switched off.
    """
    gen_tree = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(table_path))))
    for base in (gen_tree, root):
        p = os.path.join(base, rel)
        if os.path.exists(p):
            return p
    return os.path.join(gen_tree, rel)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("table")
    ap.add_argument("--root", default=os.getcwd(),
                    help="source tree for ref/nid_db.json and runtime/src/hle")
    ap.add_argument("--allow", default=None,
                    help="file of 'module NID' lines to report but not fail on")
    ap.add_argument("--imports", action="store_true",
                    help="also fail a module whose own imports resolve nowhere")
    args = ap.parse_args(argv)

    imgs, funcs, exps = parse_table(args.table)
    hle = hle_names(args.root)
    allow = set()
    if args.allow and os.path.exists(args.allow):
        for line in open(args.allow):
            line = line.split("#")[0].split()
            if len(line) == 2:
                allow.add((line[0], int(line[1], 16)))
    failures = []
    nids = {}
    nid_path = os.path.join(args.root, "ref", "nid_db.json")
    if os.path.exists(nid_path):
        nids = json.load(open(nid_path))
    print("%-14s %5s %5s %5s %5s %5s %5s %6s" %
          ("module", "funcs", "lifted", "miss", "rowmiss", "badopd", "badtoc", "impmiss"))
    for idx, im in enumerate(imgs):
        if not im["prx"]:
            continue
        path = resolve_image(args.table, args.root, im["file"])
        if not os.path.exists(path):
            failures.append((im["name"], "IMAGE-MISSING", 0, "", 0, 0))
            print("%-14s  image file missing: %s" % (im["name"], path))
            continue
        elf = Elf(path)
        prx = prx_param(elf)
        if not prx or "libent" not in prx:
            failures.append((im["name"], "NO-LIBENT", 0, "", 0, 0))
            continue
        rows = module_function_exports(elf, prx)
        lifted = miss = rowmiss = badopd = badtoc = 0
        for r in rows:
            tag = (r["module"], r["nid"])
            if tag in allow:
                continue
            row = exps.get((r["module"], r["nid"], idx))
            if row is None:
                rowmiss += 1
                failures.append((im["name"], "ROW-MISSING", r["nid"], r["module"], r["opd"], 0))
                continue
            if not row["opd"]:
                badopd += 1
                failures.append((im["name"], "OPD-ZERO", r["nid"], r["module"], r["opd"], 0))
                continue
            if row["opd"] != r["opd"]:
                badopd += 1
                failures.append((im["name"], "OPD-DIFFERS", r["nid"], r["module"], r["opd"], row["opd"]))
                continue
            code = elf.u32(row["opd"])
            toc = elf.u32(row["opd"] + 4)
            if toc != im["toc"]:
                badtoc += 1
                failures.append((im["name"], "TOC", r["nid"], r["module"], row["opd"], toc))
                continue
            if not (im["text_lo"] <= code < im["text_hi"]):
                badopd += 1
                failures.append((im["name"], "CODE-NOT-IN-TEXT", r["nid"], r["module"], row["opd"], code))
                continue
            if code not in funcs:
                miss += 1
                failures.append((im["name"], "NO-LIFT", r["nid"], r["module"], row["opd"], code))
                continue
            lifted += 1

        impmiss = 0
        if args.imports:
            none = []
            for m in inv_imports(elf, prx, nids):
                for f in m["functions"]:
                    if any((m["module"], int(f["nid"], 16), k) in exps for k in range(len(imgs))):
                        continue
                    if f["name"] and (m["module"], f["name"]) in hle:
                        continue
                    none.append((m["module"], f["nid"], f["name"]))
            impmiss = len(none)
            if none:
                failures.append((im["name"], "IMPORTS-UNRESOLVED", len(none), "", 0, 0))
        print("%-14s %5d %5d %5d %5d %5d %5d %6d" %
              (im["name"], len(rows), lifted, miss, rowmiss, badopd, badtoc, impmiss))
        if impmiss:
            print("  %-12s %d of its own imports resolve nowhere:" % (im["name"], impmiss))
            for mm, nid, nm in none[:20]:
                print("      %-16s %s %s" % (mm, nid, nm))

    if failures:
        print("")
        print("FAIL: %d export coverage problem(s)" % len(failures))
        for f in failures:
            print("  %-14s %-18s nid=%-10s module=%-14s opd=0x%08x code=0x%08x" %
                  (f[0], f[1], ("%08X" % f[2]) if isinstance(f[2], int) else f[2],
                   f[3] or "", f[4] if isinstance(f[4], int) else 0,
                   f[5] if isinstance(f[5], int) else 0))
        return 1
    print("PASS: every exported function of every LLE module has lifted code")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
