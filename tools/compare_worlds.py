#!/usr/bin/env python3
"""Compare two saved worlds, and refuse to compare hashes if the seeds differ.

The whole point: a hash mismatch means a compiler bug ONLY when both runs were
asked for the same terrain. Three earlier attempts compared worlds built on
different RandomSeed values and would have libelled the compiler.

usage: compare_worlds.py <a>/GAMEDATA <b>/GAMEDATA
exit: 0 identical, 1 different (same seed), 2 seeds differ -- void, 3 unreadable
"""
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import drive_screens as ds  # noqa: E402


def main():
    if len(sys.argv) < 3:
        print("usage: compare_worlds.py <a>/GAMEDATA <b>/GAMEDATA")
        return 3
    a, b = sys.argv[1], sys.argv[2]
    for p in (a, b):
        if not os.path.isfile(p):
            print(f"missing: {p}")
            return 3
    sa, sb = ds.world_seed(a), ds.world_seed(b)
    ha = hashlib.sha256(open(a, "rb").read()).hexdigest()
    hb = hashlib.sha256(open(b, "rb").read()).hexdigest()
    print(f"a seed 0x{sa:016x}  sha {ha}" if sa is not None else "a seed unreadable")
    print(f"b seed 0x{sb:016x}  sha {hb}" if sb is not None else "b seed unreadable")
    if sa is None or sb is None:
        print("RESULT: seed unreadable -- comparison void")
        return 3
    if sa != sb:
        print("RESULT: SEEDS DIFFER -- comparison void, hashes must not be compared")
        return 2
    if ha == hb:
        print("RESULT: same seed AND identical hash -- the compiled code is correct")
        return 0
    print("RESULT: same seed but HASHES DIFFER -- compiler bug on this world")
    return 1


if __name__ == "__main__":
    sys.exit(main())
