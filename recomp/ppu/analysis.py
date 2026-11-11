import collections
import json
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
from inventory import Elf, opd, prx_param, imports, exports
from isa import mnemonic


def sext(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


class Image:
    def __init__(self, path, nids=None):
        self.elf = Elf(path)
        prx = prx_param(self.elf)
        self.imports = []
        for m in imports(self.elf, prx, nids or {}):
            for f in m["functions"]:
                self.imports.append({"module": m["module"], "nid": f["nid"], "name": f["name"], "stub": int(f["stub_target"], 16)})
        self.stubs = {imp["stub"]: i for i, imp in enumerate(self.imports)}
        # Switch tables supplied by hand in switch_tables.toml, keyed by the bctr
        # address. The detector recovers the shapes it recognises; this covers the
        # rest. Red Dead Redemption's f_00d19278 builds its table base with
        # `lis r16, 0x13f` + `addic r16, r16, 0x6400`, indexes with `lbzx` (a BYTE
        # index) then `slwi` and `lwzx`, and the detector did not follow that
        # combination -- 256 targets including 0x00d1bbec were invisible.
        self.manual_tables = {}
        self.exports = []
        # The code range is the executable PT_LOAD segment, whole. It used to be
        # cut short at the lowest import stub, on the theory that stubs sit past
        # the code. That is true for some images and false for others: a stub can
        # be planted inside the segment, and truncating there drops every function
        # above it. Red Dead Redemption's libsysutil has its first stub at
        # 0x2b01cbc0 and real code up to 0x2b01ee60, so a call to 0x2b0187a8 --
        # valid PowerPC, never lifted -- became 'unknown indirect target' and
        # killed the run.
        #
        # Stubs are excluded as ZONES instead, not as a boundary: the lifter
        # already treats a branch into one as an import call (t in img.stubs),
        # so keeping the whole range and letting that check do its work is both
        # correct and simpler. stub_ranges is what other passes consult.
        self.stub_ranges = sorted(self.stubs) if self.stubs else []
        if self.elf.shdrs:
            execs = sorted((s[3], s[3] + s[5]) for s in self.elf.shdrs if s[2] & 4 and s[5])
            self.text_lo = execs[0][0]
            self.text_hi = max(b for _, b in execs)
            self.text_size = self.text_hi - self.text_lo
            self.text = self.elf.read(self.text_lo, self.text_size)
            o = opd(self.elf)
            base = int(o["addr"], 16)
            raw = self.elf.read(base, o["count"] * 8)
            self.opd = [struct.unpack_from(">II", raw, i * 8) for i in range(o["count"])]
            self.toc = self.opd[0][1]
        else:
            seg = next(p for p in self.elf.phdrs if p[0] == 1 and p[1] & 1)
            self.text_lo = seg[3]
            self.text_hi = seg[3] + seg[5]
            self.text_size = self.text_hi - self.text_lo
            self.text = self.elf.read(self.text_lo, self.text_size)
            self.toc = int(prx["toc"], 16)
            self.opd = []
            for p in self.elf.phdrs:
                if p[0] != 1 or p[1] & 1 or not p[5]:
                    continue
                blob = self.elf.d[p[2]:p[2] + p[5]]
                for k in range(0, len(blob) - 7, 4):
                    code, toc = struct.unpack_from(">II", blob, k)
                    if toc == self.toc and self.in_text(code):
                        self.opd.append((code, toc))
            for m in exports(self.elf, prx, nids or {}):
                for e in m["entries"]:
                    self.exports.append({"module": m["module"], "nid": e["nid"], "name": e["name"], "opd": int(e["addr"], 16)})

    def word(self, addr):
        return struct.unpack_from(">I", self.text, addr - self.text_lo)[0]

    def in_text(self, addr):
        return self.text_lo <= addr < self.text_hi and addr % 4 == 0

    def toc_word(self, disp):
        # A TOC offset can leave the file when the displacement is wrong, which is
        # normal while probing a candidate base. Return 0 rather than raising: the
        # caller treats 0 as "not a table".
        blob = self.elf.read((self.toc + disp) & 0xFFFFFFFF, 4)
        return struct.unpack(">I", blob)[0] if blob else 0


def branch_target(pc, w):
    op = w >> 26
    if op == 18:
        li = sext(w & 0x03FFFFFC, 26)
        return (li if w & 2 else pc + li) & 0xFFFFFFFF
    if op == 16:
        bd = sext(w & 0xFFFC, 16)
        return (bd if w & 2 else pc + bd) & 0xFFFFFFFF
    return None


def bo_always(w):
    return ((w >> 21) & 0x14) == 0x14


def jump_table(img, bctr):
    """Recover a PPC switch table from the code preceding a `bctr`.

    Modelled on XenonRecomp/XenonAnalyse, same instruction family. The shape a
    compiler emits for `switch` is stable:

        cmplwi cr6, rX, N          bound check
        bgt    cr6, default        out of range -> default case
        ...                        index scaling (rlwinm/slwi by 4)
        lis/addi or a TOC load     base of the table
        lwzx   rY, base, index     or lwz after an add, when entries are relative
        mtctr  rY
        bctr                       dispatch

    Three things the previous version got wrong, each measured on Red Dead
    Redemption's f_00d19278:

      - it required base == bctr + 4 (the table right after the branch). Here the
        table lives in a data segment far from the code, reached through the TOC.
      - it assumed RELATIVE entries. Here they are ABSOLUTE addresses
        (00d19ce4 00d19d10 00d19d30 ...), so `base + v` produced nonsense.
      - it only looked at a 16-instruction window ending at bctr and required the
        bound to be a `cmpwi`-style op 10, missing `cmplwi`.

    A table is accepted only if its entries land inside this image's code and the
    run is contiguous, so a wrong guess produces None rather than a plausible
    wrong switch.
    """
    window = [(a, img.word(a)) for a in range(max(img.text_lo, bctr - 4 * 40), bctr, 4)]
    if not window:
        return None

    # 1. The bound: cmplwi (op 10) / cmpwi (op 11) with an unsigned immediate.
    bound = None
    default_off = None
    for i in range(len(window) - 1, -1, -1):
        a, w = window[i]
        op = w >> 26
        if op in (10, 11) and (w >> 21) & 1:      # opcode 10/11 with L=1
            bound = (w & 0xFFFF) + 1              # N + 1 entries
            # the branch that exits on out-of-range
            for a2, w2 in window[i + 1:]:
                if (w2 >> 26) == 16:
                    default_off = branch_target(a2, w2)
                    break
            break

    # 2. The base: a lis/addi pair, or a TOC load (lwz rX, disp(rX) after a lis rX, hi).
    base = None
    lis_reg = None
    lis_val = None
    for a, w in window:
        op = w >> 26
        if op == 15:                               # lis / addis
            rt = (w >> 21) & 0x1F
            ra = (w >> 16) & 0x1F
            if ra == 0:
                lis_reg, lis_val = rt, (w & 0xFFFF) << 16
        elif op in (12, 14) and lis_reg is not None and ((w >> 21) & 0x1F) == lis_reg:
            # 14 is addi, 12 is addic. Both compute lis_val + imm, and a table base
            # built with addic is what f_00d19278 does:
            #     lis   r16, 0x13f
            #     addic r16, r16, 0x6400     -> r16 = 0x013f6400
            # Missing opcode 12 is why this table went undetected with the pattern
            # sitting in plain sight.
            base = (lis_val + (w & 0xFFFF)) & 0xFFFFFFFF
        elif op == 24 and lis_reg is not None and ((w >> 21) & 0x1F) == lis_reg:  # ori
            base = (lis_val | (w & 0xFFFF)) & 0xFFFFFFFF
        elif op == 32 and lis_reg is not None and ((w >> 16) & 0x1F) == lis_reg:  # lwz
            # A load through the TOC: the value at toc+disp is the table address.
            v = img.toc_word(sext(w & 0xFFFF, 16))
            if v and img.in_text(v):
                base = v

    if base is None:
        return None

    limit = bound if bound else 1024
    # 3. Read the entries. Absolute addresses here; a relative table is accepted
    #    too, because the test is "does the result land in code".
    # The table lives in a DATA segment, so it is read through the ELF, not
    # through img.word() (which only addresses the text buffer). An absolute read
    # that the file cannot serve means the base is wrong: stop rather than
    # inventing entries from whatever is adjacent.
    targets = []
    for i in range(limit):
        blob = img.elf.read(base + i * 4, 4)
        if blob is None:
            break
        raw = struct.unpack(">I", blob)[0]
        absolute = raw
        relative = (base + sext(raw, 32)) & 0xFFFFFFFF
        for candidate in (absolute, relative):
            if img.in_text(candidate):
                targets.append(candidate)
                break
        else:
            break
    if not targets:
        return None

    return {"base": base, "count": len(targets), "bound": bound,
            "targets": targets, "default": default_off}


def load_switch_tables(path):
    """Read switch_tables.toml: tables the detector could not recover.

    Format, one entry per table:

        [[table]]
        bctr = 0x00d1933c      # the bctr that dispatches
        base = 0x013f6400      # first entry, in guest memory
        count = 256            # number of entries
        note = "..."           # optional

    plus optional `targets = [0x...]` to override the computed ones. A hand-
    written table is a claim, so the loader validates it against the image: every
    target must be inside the code range, and the bctr must be too. A typo then
    fails at lift time with the address, instead of producing a switch whose
    cases point at nothing.
    """
    if not path or not os.path.exists(path):
        return {}
    import re
    out = {}
    cur = None
    with open(path) as f:
        for raw in f:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            if line == "[[table]]":
                if cur:
                    out[cur["bctr"]] = cur
                cur = {}
                continue
            if cur is None or "=" not in line:
                continue
            k, v = line.split("=", 1)
            k, v = k.strip(), v.strip().strip('"')
            if k in ("bctr", "base"):
                cur[k] = int(v, 0)
            elif k == "count":
                cur[k] = int(v, 0)
            elif k == "note":
                cur[k] = v
    if cur:
        out[cur["bctr"]] = cur
    return out

class Function:
    def __init__(self, entry):
        self.entry = entry
        self.insns = set()
        self.calls = set()
        self.tail_calls = set()
        self.jump_tables = {}
        self.indirect_jumps = []
        self.indirect_calls = 0
        self.syscalls = []
        self.errors = []
        self.data = set()
        self.import_calls = set()


def explore(img, entry, entries):
    f = Function(entry)
    work = [entry]
    while work:
        pc = work.pop()
        while True:
            if pc in f.insns:
                break
            if not img.in_text(pc):
                f.errors.append(("out_of_text", pc))
                break
            if pc != entry and pc in entries:
                f.tail_calls.add(pc)
                break
            w = img.word(pc)
            m = mnemonic(w)
            if m is None:
                f.errors.append(("invalid", pc, w))
                break
            f.insns.add(pc)
            op = w >> 26
            if op == 18:
                t = branch_target(pc, w)
                if t in img.stubs:
                    f.import_calls.add(t)
                    if w & 1:
                        pc += 4
                        continue
                    break
                if w & 1:
                    f.calls.add(t)
                    pc += 4
                    continue
                if t in entries and t != entry:
                    f.tail_calls.add(t)
                else:
                    work.append(t)
                break
            if op == 16:
                t = branch_target(pc, w)
                if w & 1:
                    f.calls.add(t)
                    pc += 4
                    continue
                if t in entries and t != entry:
                    f.tail_calls.add(t)
                else:
                    work.append(t)
                if bo_always(w):
                    break
                pc += 4
                continue
            if op == 19 and m in ("bclr", "bcctr"):
                if w & 1:
                    f.indirect_calls += 1
                    pc += 4
                    continue
                if m == "bcctr":
                    # PS3BASE_NO_AUTO_TABLES=1 disables the automatic detector while
                    # keeping hand-written tables. It exists for bisection: a table
                    # accepted wrongly splits a function and the title derails into
                    # an address that is the middle of an instruction, which looks
                    # like a completely unrelated fault.
                    jt = None if os.environ.get("PS3BASE_NO_AUTO_TABLES") else jump_table(img, pc)
                    if jt:
                        f.jump_tables[pc] = jt
                        f.data.update(range(jt["base"], jt["base"] + jt["count"] * 4, 4))
                        for t in jt["targets"]:
                            work.append(t)
                    else:
                        f.indirect_jumps.append(pc)
                if bo_always(w):
                    break
                pc += 4
                continue
            if w == 0x44000002:
                f.syscalls.append(pc)
            if w == 0x7FE00008:
                break
            pc += 4
    return f


def is_prologue(w):
    return (w & 0xFFFF0003) == 0xF8210001 or w == 0x7C0802A6


def looks_like_function(img, a, b):
    ws = [img.word(x) for x in range(a, b, 4)]
    if any(is_prologue(w) for w in ws[:8]):
        return True
    return all(mnemonic(w) for w in ws) and ws[-1] == 0x4E800020 and not any(((w >> 23) & 0xFF) in range(0x70, 0x90) and (w & 0xFFFF) == 0 for w in ws)


def seeds_outside_functions(img, seeds, log=None):
    """Drop seeds that fall inside a function the lift already found.

    A seed is a claim about where a function STARTS. explore() stops at any
    address in `entries`, so a seed in the middle of an existing function splits
    that function in two, and the first half keeps a jump table whose cases now
    point past the cut. The generated code then contains `goto L_x` for a label
    that was never emitted, and the image does not compile:

    libavcdec_000.cpp:152: error: use of undeclared label L_3b0002c8

    The right answer is to refuse the seed: the function it lies in is already
    translated, and the address is a landing pad, not an entry. Rejected seeds
    are logged, because a discovery that is silently dropped looks like the
    loop not working.
    """
    if not seeds:
        return set()
    # discover() memoises its work per call; calling it once per seed made this
    # quadratic and a 40,000-seed image never finished. One baseline lift, then
    # every seed is tested against the same spans.
    base_funcs = getattr(img, "_baseline_funcs", None)
    if base_funcs is None:
        base_funcs, _ = discover(img)
        img._baseline_funcs = base_funcs
    # An address belongs to at most one function here, so a sorted list of
    # (first, last) intervals is enough: no function nests in another.
    spans = sorted((min(f.insns), max(f.insns), a) for a, f in base_funcs.items() if f.insns)
    starts = [s for s, _, _ in spans]
    keep = set()
    import bisect
    for s in seeds:
        i = bisect.bisect_right(starts, s) - 1
        inside = None
        for j in (i, i - 1):
            if 0 <= j < len(spans):
                lo_i, hi_i, a = spans[j]
                if a != s and lo_i <= s <= hi_i:
                    inside = a
                    break
        if inside is None:
            keep.add(s)
        elif log is not None:
            log("seed 0x%08x is inside function 0x%08x -- rejected (a landing pad, not an entry)" % (s, inside))
    return keep


def discover(img, extra=(), log=None):
    # Seeds first go through the interior test: claiming an address that is
    # already inside a translated function splits that function and produces a
    # label the generator never emits.
    extra = seeds_outside_functions(img, extra, log)
    entries = {c for c, _ in img.opd if img.in_text(c)} | set(extra)
    funcs = {}
    pending = set(entries)
    while pending:
        for e in sorted(pending):
            funcs[e] = explore(img, e, entries)
        new = set()
        for f in funcs.values():
            for t in f.calls | f.tail_calls:
                if img.in_text(t) and t not in entries:
                    new.add(t)
        entries |= new
        stale = {e for e, f in funcs.items() if any(i in new for i in f.insns if i != e)}
        pending = new | stale
    return funcs, entries


def islands(img, funcs):
    covered = set()
    for f in funcs.values():
        covered |= f.insns
        covered |= f.data
    out = []
    run = None
    for a in range(img.text_lo, img.text_hi, 4):
        w = img.word(a)
        if a in covered or w == 0 or w == 0x60000000:
            if run and run[1] - run[0] >= 16:
                out.append(run)
            run = None
            continue
        if mnemonic(w):
            run = [run[0], a + 4] if run else [a, a + 4]
        else:
            if run and run[1] - run[0] >= 16:
                out.append(run)
            run = None
    return out, covered


def main(elf_path, out_path):
    img = Image(elf_path)
    funcs, entries = discover(img)
    isl, covered = islands(img, funcs)
    extra = set()
    while True:
        found = {a for a, b in isl if looks_like_function(img, a, b)} - extra
        if not found:
            break
        extra |= found
        funcs, entries = discover(img, extra)
        isl, covered = islands(img, funcs)
    print(f"prologue-started islands promoted to functions: {len(extra)}")
    ninsn = sum(len(f.insns) for f in funcs.values())
    owners = collections.Counter()
    for f in funcs.values():
        for i in f.insns:
            owners[i] += 1
    shared = sum(1 for v in owners.values() if v > 1)
    errs = [(hex(f.entry), e[0], hex(e[1])) for f in funcs.values() for e in f.errors]
    jts = sum(len(f.jump_tables) for f in funcs.values())
    ind = sum(len(f.indirect_jumps) for f in funcs.values())
    print(f"toc {img.toc:#x} opd {len(img.opd)} functions {len(funcs)} (from bl/b {len(entries) - len({c for c, _ in img.opd})})")
    print(f"instructions reached {ninsn} unique {len(owners)} shared {shared} of {img.text_size // 4} words")
    print(f"jump tables {jts} unresolved bctr {ind} indirect calls {sum(f.indirect_calls for f in funcs.values())} syscalls {sum(len(f.syscalls) for f in funcs.values())}")
    print(f"errors {len(errs)}: {errs[:10]}")
    print(f"uncovered code-like islands >=4 insns: {len(isl)} totalling {sum((b - a) // 4 for a, b in isl)} insns; first {[(hex(a), (b - a) // 4) for a, b in isl[:10]]}")
    out = {
        "toc": hex(img.toc),
        "functions": [
            {
                "entry": hex(f.entry),
                "start": hex(min(f.insns)) if f.insns else hex(f.entry),
                "end": hex(max(f.insns) + 4) if f.insns else hex(f.entry),
                "insns": len(f.insns),
                "calls": sorted(hex(c) for c in f.calls),
                "tail_calls": sorted(hex(c) for c in f.tail_calls),
                "jump_tables": {hex(k): {"base": hex(v["base"]), "count": v["count"]} for k, v in f.jump_tables.items()},
                "indirect_jumps": [hex(x) for x in f.indirect_jumps],
                "indirect_calls": f.indirect_calls,
                "syscalls": [hex(x) for x in f.syscalls],
                "errors": [[e[0]] + [hex(x) for x in e[1:]] for e in f.errors],
            }
            for f in sorted(funcs.values(), key=lambda f: f.entry)
        ],
        "islands": [[hex(a), hex(b)] for a, b in isl],
    }
    json.dump(out, open(out_path, "w"), indent=0)


if __name__ == "__main__":
    main(*sys.argv[1:3])
