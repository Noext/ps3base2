#!/usr/bin/env python3
"""Collect the static seeds for every image of one bisect step.

Same call setup.py's step_ppu makes for each manifest entry.
"""
import json
import os
import subprocess
import sys

out, root = sys.argv[1], sys.argv[2]
manifest = json.load(open(os.path.join(out, "images-mc.json")))
os.makedirs(os.path.join(out, "mc/gen/seeds"), exist_ok=True)
for spec in manifest:
    dst = os.path.join(out, "mc/gen/seeds", spec["name"] + ".txt")
    r = subprocess.run([sys.executable, os.path.join(root, "tools/static_seeds.py"),
                        "--elf", spec["elf"], "--name", spec["name"], "--out", dst],
                       cwd=root)
    if r.returncode:
        sys.exit("static_seeds failed for " + spec["name"])
