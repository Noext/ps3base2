#!/usr/bin/env python3
"""Filter out/ppu/table.cpp down to the functions a coverage profile reached.

Why this exists: 92% of the linked lifted functions are never executed in a boot
(measured: 1,775 of 22,685). Linking the whole table costs 92 MB; the keep-set
costs 50 MB. The cost is that the profile must be COMPLETE -- a single missing
indirect call aborts the guest with "unknown indirect target 0xADDR". The
closure loop for that is: link, run, add the reported address, repeat.

usage: filter_table.py <profile.txt> <out.cpp>
  profile.txt holds one 8-digit hex address per line, as COVERAGE-BEGIN dumps them.
"""
import re
import sys


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    prof_path, out_path = sys.argv[1], sys.argv[2]

    keep = set()
    for line in open(prof_path):
        line = line.strip()
        if line and re.fullmatch(r'[0-9a-fA-F]{1,8}', line):
            keep.add(int(line, 16))

    src = open('/root/Dev/ps3base2/out/ppu/table.cpp').read()
    start = src.index('const FuncEntry functions[] = {')
    end = src.index('};', start)
    block = src[start:end]

    entries = re.findall(r'\{(0x[0-9a-f]+),\s*(f_[0-9a-f]+)\}', block)
    kept = [e for e in entries if int(e[0], 16) in keep]

    body = 'const FuncEntry functions[] = {\n'
    for a, f in kept:
        body += '    {%s, %s},\n' % (a, f)
    body += '    {0, nullptr},\n'

    out = src[:start] + body + src[end:]
    # Dispatch reads function_count entries; leaving the old value reads past the
    # end of the filtered array and traps with "table index is out of bounds".
    out = re.sub(r'function_count\s*=\s*\d+', 'function_count = %d' % len(kept), out)
    open(out_path, 'w').write(out)

    missing = [a for a in keep if not any(int(e[0], 16) == a for e in entries)]
    print('table filter: %d of %d entries kept, %d profile addresses have no entry'
          % (len(kept), len(entries), len(missing)))
    for a in missing[:5]:
        print('  missing 0x%08x' % a)


if __name__ == '__main__':
    main()
