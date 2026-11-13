# A test for the export coverage gate, on a synthetic PRX.
#
# Why synthetic: the gate's job is to join three things -- a PRX's libent chain,
# the generated exports[]/functions[], and the descriptor words in the image --
# and every one of those can be built in a few hundred bytes. A test that needs
# bin/rdr/EBOOT.elf can only run where the disc is mounted, which is exactly
# where nobody runs it.
#
# The cases are the four holes tools/check_lle_exports.py had, each one measured
# on the real tree first (RESULTS-Q2.md, Measurement 4):
#
#   D  an unnamed export (module_start / PRX entry) missing from exports[]
#   G  a descriptor whose code word is outside the image text -- the old tool
#      called that "data" and passed it
#   A  an export whose code address is inside the text but not lifted
#   B  a descriptor with a zero opd
#
# Run: python3 tests/export_coverage.py   (exit 0 = pass)
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GATE = os.path.join(ROOT, "tools", "check_export_coverage.py")

TEXT_LO = 0x30000000
TEXT_HI = 0x30001000
OPD_LO = 0x30002000
TOC = 0x30003000
# Three descriptors: two named exports and one unnamed PRX entry.
FUNC_A = 0x30000000
FUNC_B = 0x30000040
FUNC_ENTRY = 0x30000080
OPD_A, OPD_B, OPD_ENTRY = OPD_LO, OPD_LO + 8, OPD_LO + 16
LIBENT = OPD_LO + 0x40


def build_prx(path, code_words=None, opds=None):
    """A minimal relocated PRX ELF: one R+X LOAD, one R+W LOAD, one PT_PRX_PARAM.

    code_words maps a text address to a word; opds maps an opd address to
    (code, toc). Anything not given is left as the default.
    """
    code_words = dict(code_words or {})
    opds = dict(opds or {})
    for a in (FUNC_A, FUNC_B, FUNC_ENTRY):
        code_words.setdefault(a, 0x4E800020)          # blr
    # Each descriptor points at the function entry of the same name. The
    # defaults must MATCH the entries the table declares, or the baseline case
    # fails and the negative cases prove nothing.
    opds.setdefault(OPD_A, (FUNC_A, TOC))
    opds.setdefault(OPD_B, (FUNC_B, TOC))
    opds.setdefault(OPD_ENTRY, (FUNC_ENTRY, TOC))

    text = bytearray(TEXT_HI - TEXT_LO)
    for a, w in code_words.items():
        struct.pack_into(">I", text, a - TEXT_LO, w)
    data = bytearray(0x2000)
    for a, (code, toc) in opds.items():
        struct.pack_into(">II", data, a - OPD_LO, code, toc)

    # libent chain: entry 0 = unnamed (1 func), entry 1 = "testLib" (2 funcs).
    # Layout of one entry (0x1C bytes): size, attr, num_func/var/tls, name,
    # nid table, address table.
    nids0 = LIBENT + 0x60
    addrs0 = nids0 + 4
    nids1 = addrs0 + 4
    addrs1 = nids1 + 8
    name1 = addrs1 + 8
    # The size is a SINGLE BYTE at offset 0, not a 4-byte word. Writing it with
    # pack_into(">I", ..., 0x1C) leaves b[0] == 0, and a walker that does
    # `addr += size` then never advances. That is exactly what happened here:
    # the fixture made the gate loop forever, three runs grew to 24/15/7.5 GB and
    # took the machine down. The gate now refuses a zero size (below), and this
    # fixture writes the byte the real ELFs write -- libfiber's entry at
    # 0x36005db4 begins 1c 00 00 00, i.e. size 0x1C.
    data[LIBENT - OPD_LO] = 0x1C
    struct.pack_into(">H", data, LIBENT - OPD_LO + 4, 0x8000)
    struct.pack_into(">HHH", data, LIBENT - OPD_LO + 6, 1, 0, 0)
    struct.pack_into(">III", data, LIBENT - OPD_LO + 0x10, 0, nids0, addrs0)
    struct.pack_into(">I", data, nids0 - OPD_LO, 0xBC9A0086)
    struct.pack_into(">I", data, addrs0 - OPD_LO, OPD_ENTRY)

    e1 = LIBENT + 0x1C
    data[e1 - OPD_LO] = 0x1C
    struct.pack_into(">H", data, e1 - OPD_LO + 4, 0x9)
    struct.pack_into(">HHH", data, e1 - OPD_LO + 6, 2, 0, 0)
    struct.pack_into(">III", data, e1 - OPD_LO + 0x10, name1, nids1, addrs1)
    struct.pack_into(">I", data, nids1 - OPD_LO, 0x11111111)
    struct.pack_into(">I", data, nids1 - OPD_LO + 4, 0x22222222)
    struct.pack_into(">I", data, addrs1 - OPD_LO, OPD_A)
    struct.pack_into(">I", data, addrs1 - OPD_LO + 4, OPD_B)
    data[name1 - OPD_LO:name1 - OPD_LO + 8] = b"testLib\0"

    # ELF64 big-endian, e_type 0xFFA4 (PRX), 3 program headers.
    hdr = bytearray(0x40 + 3 * 56)
    hdr[0:4] = b"\x7fELF"
    hdr[4] = 2          # ELFCLASS64
    hdr[5] = 2          # ELFDATA2MSB
    hdr[6] = 1          # EV_CURRENT
    struct.pack_into(">H", hdr, 0x10, 0xFFA4)
    struct.pack_into(">H", hdr, 0x12, 21)
    struct.pack_into(">Q", hdr, 0x18, OPD_ENTRY)      # e_entry
    struct.pack_into(">Q", hdr, 0x20, 0x40)           # e_phoff
    struct.pack_into(">H", hdr, 0x36, 56)             # e_phentsize
    struct.pack_into(">H", hdr, 0x38, 3)              # e_phnum

    # The PRX parameter block lives INSIDE the data LOAD: inventory.Elf.read
    # resolves an address through the PT_LOAD table, so a phdr whose vaddr is
    # outside every LOAD reads as None and prx_param() cannot parse it.
    prx_at = OPD_LO + 0x1800
    prx_param = struct.pack(">8I", 0, 0x00010001, 0x00010001, 0, LIBENT,
                            LIBENT + 0x38, 0, 0)
    data[prx_at - OPD_LO:prx_at - OPD_LO + len(prx_param)] = prx_param
    text_off = len(hdr)
    data_off = text_off + len(text)

    def ph(i, ptype, flags, off, vaddr, filesz, memsz):
        struct.pack_into(">IIQQQQQQ", hdr, 0x40 + i * 56, ptype, flags, off,
                         vaddr, vaddr, filesz, memsz, 8)

    ph(0, 1, 5, text_off, TEXT_LO, len(text), len(text))
    ph(1, 1, 6, data_off, OPD_LO, len(data), len(data))
    ph(2, 0x60000002, 4, data_off + (prx_at - OPD_LO), prx_at,
       len(prx_param), len(prx_param))
    open(path, "wb").write(bytes(hdr) + bytes(text) + bytes(data))


def build_table(path, elf_rel, exports, funcs):
    """A table.cpp with just enough for the gate: images, functions, exports."""
    lines = ["const ImageEntry images[] = {",
             '    {"test", "%s", true, 0x%08x, 0x%08x, 0x%08x, 0x%08x},'
             % (elf_rel, TEXT_LO, TEXT_HI, TOC, OPD_ENTRY),
             "};", "const uint32_t image_count = 1;", "",
             "const FuncEntry functions[] = {"]
    lines += ["    {0x%08x, f_%08x}," % (a, a) for a in funcs]
    lines += ["};", "const uint32_t function_count = %d;" % len(funcs), "",
              "const ImportEntry imports[] = {", "};",
              "const uint32_t import_count = 0;", "",
              "const ExportEntry exports[] = {"]
    lines += ['    {"%s", 0x%08xu, "", 0, 0x%08x},' % (m, n, o) for m, n, o in exports]
    lines += ["};", "const uint32_t export_count = %d;" % len(exports), ""]
    open(path, "w").write("\n".join(lines) + "\n")


def run_case(tmp, name, exports, funcs, opds=None, want_pass=False):
    elf = os.path.join(tmp, "test.elf")
    build_prx(elf, opds=opds)
    table = os.path.join(tmp, "table.cpp")
    build_table(table, "test.elf", exports, funcs)
    p = subprocess.run([sys.executable, GATE, table, "--root", tmp],
                       capture_output=True, text=True)
    ok = (p.returncode == 0) == want_pass
    print("%-4s %-46s exit %d %s" % ("PASS" if ok else "FAIL", name,
                                     p.returncode, "expected"))
    if not ok:
        print((p.stdout + p.stderr).strip())
    return ok


def main():
    all_exports = [("testLib", 0x11111111, OPD_A),
                   ("testLib", 0x22222222, OPD_B),
                   ("", 0xBC9A0086, OPD_ENTRY)]
    all_funcs = [FUNC_A, FUNC_B, FUNC_ENTRY]
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        # The baseline: everything lifted, everything present. Must PASS, or the
        # other cases prove nothing.
        ok &= run_case(tmp, "baseline: all exports lifted", all_exports,
                       all_funcs, want_pass=True)
        # A: a code address inside the text that the lifter never produced.
        ok &= run_case(tmp, "A: export code address not lifted",
                       all_exports, [FUNC_A, FUNC_ENTRY])
        # D: the unnamed PRX-entry row missing from the table.
        ok &= run_case(tmp, "D: unnamed PRX-entry export row missing",
                       [e for e in all_exports if e[0]], all_funcs)
        # G: the descriptor's code word outside the image text. The old tool
        # classified this as data and passed it.
        ok &= run_case(tmp, "G: descriptor code word outside text",
                       all_exports, all_funcs,
                       opds={OPD_A: (0x00010001, TOC)})
        # B: a descriptor with a zero opd.
        ok &= run_case(tmp, "B: descriptor opd zero",
                       all_exports, all_funcs,
                       opds={OPD_B: (0, TOC)})
        # TOC: the right code, the wrong TOC. Nothing else checks this.
        ok &= run_case(tmp, "T: descriptor TOC wrong",
                       all_exports, all_funcs,
                       opds={OPD_B: (FUNC_B, 0xDEADBEEF)})
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
