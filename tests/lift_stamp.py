#!/usr/bin/env python3
"""The lift staleness check, tested on a synthetic tree.

Why this test exists: on 2026-11-13 a lift was searched for a function the title
calls (0x00b58430) and the conclusion "not lifted" was written down before the
right chunk file was checked. The function WAS lifted, in rdr_115.cpp -- the
bucket is (addr - text_lo) / 0x19000, which the generated tree states nowhere.

Two things follow, and this test pins both:

  1. a lift whose INPUT is newer than table.cpp must report STALE, so a build
     cannot silently describe a different binary than the one on disk;
  2. the seed fingerprint must CHANGE when a seed changes and stay the SAME when
     nothing does, because two lifts with different seed sets can produce the
     same function count and the count alone cannot tell them apart.

The check is one-way on purpose: it can say "stale, relift", never "fresh,
skip". A lift is expensive; a stale lift is a wrong measurement.

Run: python3 tests/lift_stamp.py   (exit 0 = pass)
"""
import importlib.util
import os
import shutil
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def make_tree(tmp):
    """A tree shaped like out-rdr: ppu/table.cpp plus gen/ inputs."""
    ppu = os.path.join(tmp, "out", "ppu")
    gen = os.path.join(tmp, "out", "gen")
    seeds = os.path.join(gen, "seeds")
    os.makedirs(ppu)
    os.makedirs(seeds)
    with open(os.path.join(ppu, "table.cpp"), "w") as f:
        f.write("const FuncEntry functions[] = {\n"
                "    {0x00010000, f_00010000},\n"
                "    {0x00010010, f_00010010},\n};\n")
    with open(os.path.join(seeds, "rdr.txt"), "w") as f:
        f.write("0x00010000 rdr\n")
    with open(os.path.join(gen, "switch_tables.toml"), "w") as f:
        f.write("")
    with open(os.path.join(gen, "lle_layout.json"), "w") as f:
        f.write("{}")
    return ppu, gen, seeds


def case_fresh_tree_is_not_stale():
    setup = load("setup", os.path.join(ROOT, "tools", "setup.py"))
    with tempfile.TemporaryDirectory() as tmp:
        ppu, gen, _ = make_tree(tmp)
        # Make the generated code strictly newer than every input.
        future = time.time() + 60
        os.utime(os.path.join(ppu, "table.cpp"), (future, future))
        assert setup.lift_is_stale(ppu, gen) is False, \
            "a lift newer than its inputs was reported stale"
    return "a lift newer than its inputs is not stale"


def case_touched_seed_is_stale():
    setup = load("setup", os.path.join(ROOT, "tools", "setup.py"))
    with tempfile.TemporaryDirectory() as tmp:
        ppu, gen, seeds = make_tree(tmp)
        future = time.time() + 60
        os.utime(os.path.join(ppu, "table.cpp"), (future, future))
        assert setup.lift_is_stale(ppu, gen) is False
        # Now touch a seed to be newer than the lift.
        later = future + 60
        os.utime(os.path.join(seeds, "rdr.txt"), (later, later))
        assert setup.lift_is_stale(ppu, gen) is True, \
            "a seed newer than table.cpp was NOT reported stale"
    return "a seed newer than table.cpp reports stale"


def case_touched_switch_table_is_stale():
    setup = load("setup", os.path.join(ROOT, "tools", "setup.py"))
    with tempfile.TemporaryDirectory() as tmp:
        ppu, gen, _ = make_tree(tmp)
        future = time.time() + 60
        os.utime(os.path.join(ppu, "table.cpp"), (future, future))
        later = future + 60
        os.utime(os.path.join(gen, "switch_tables.toml"), (later, later))
        assert setup.lift_is_stale(ppu, gen) is True, \
            "a switch_tables.toml newer than table.cpp was NOT reported stale"
    return "a switch table newer than table.cpp reports stale"


def case_missing_lift_is_stale():
    setup = load("setup", os.path.join(ROOT, "tools", "setup.py"))
    with tempfile.TemporaryDirectory() as tmp:
        ppu, gen, _ = make_tree(tmp)
        os.remove(os.path.join(ppu, "table.cpp"))
        assert setup.lift_is_stale(ppu, gen) is True, \
            "a missing table.cpp was not reported stale"
    return "a missing table.cpp is stale (nothing to trust)"


def case_fingerprint_changes_with_seeds():
    setup = load("setup", os.path.join(ROOT, "tools", "setup.py"))
    with tempfile.TemporaryDirectory() as tmp:
        _, gen, seeds = make_tree(tmp)
        a = setup.seed_fingerprint(gen)
        assert len(a) == 64, "fingerprint is not a sha256 hex digest: %r" % a
        b = setup.seed_fingerprint(gen)
        assert a == b, "the fingerprint changed with no input change"
        with open(os.path.join(seeds, "rdr.txt"), "a") as f:
            f.write("0x00010010 rdr\n")
        c = setup.seed_fingerprint(gen)
        assert c != a, "the fingerprint did NOT change when a seed changed"
    return "the fingerprint is stable, and changes when a seed changes"


def case_stamp_counts_functions():
    stamp = load("lift_stamp", os.path.join(ROOT, "tools", "lift_stamp.py"))
    with tempfile.TemporaryDirectory() as tmp:
        ppu, gen, _ = make_tree(tmp)
        n = stamp.function_count(os.path.join(ppu, "table.cpp"))
        assert n == 2, "function_count returned %r, expected 2" % n
    return "the stamp counts the functions in table.cpp"


def main():
    checks = [
        case_fresh_tree_is_not_stale(),
        case_touched_seed_is_stale(),
        case_touched_switch_table_is_stale(),
        case_missing_lift_is_stale(),
        case_fingerprint_changes_with_seeds(),
        case_stamp_counts_functions(),
    ]
    for c in checks:
        print("ok   " + c)
    print("%d checks passed" % len(checks))
    return 0


if __name__ == "__main__":
    sys.exit(main())
