#!/usr/bin/env python3
"""List runtime sources whose #include names a generated SPU header.

The generated headers come from this title's own SPU blocks, so a different disc
cannot build those files. Printing them lets CMake drop exactly those and keep
the job-plumbing files in the same directories, whose symbols the runtime links.
"""
import os
import re
import sys

ROOT = sys.argv[1]
INC = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]', re.M)

for base, _dirs, files in os.walk(os.path.join(ROOT, "runtime", "src")):
    for name in sorted(files):
        if not name.endswith(".cpp"):
            continue
        path = os.path.join(base, name)
        try:
            text = open(path, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        for inc in INC.findall(text):
            if re.search(r"_gen\.(h|inc)$", inc):
                print(os.path.relpath(path, ROOT))
                break
