#!/usr/bin/env python3
"""Write the Minecraft image manifest for one bisect step.

"elf" is absolute: the lifter (codegen.py, static_seeds.py) opens it directly.
"file" is relative to the worktree root: the runtime opens it from its working
directory. Mixing the two is what produced
    FileNotFoundError: out-S1/bisect/../bin/mc/mc.elf
"""
import json
import os
import sys

out, root = sys.argv[1], sys.argv[2]
mods = [l.strip() for l in open(os.path.join(root, "out-S1/lle_modules-mc.txt")) if l.strip()]
m = [{"name": "mc", "elf": os.path.join(root, "bin/mc/mc.elf"), "file": "bin/mc/mc.elf"}]
for name in mods:
    n = name[:-4]
    rel = "out-S1/bisect/mc/gen/reloc/%s.elf" % n
    m.append({"name": n, "elf": os.path.join(root, rel), "file": rel})
json.dump(m, open(os.path.join(out, "images-mc.json"), "w"), indent=1)
