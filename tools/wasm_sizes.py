#!/usr/bin/env python3
"""Report the function count and the largest function of a .wasm module.

The prompt asks for "the number of functions produced, and the largest generated
function in bytes" because V8 refuses to optimise very large functions. Reading
it out of the code section is exact; no disassembler needed.

usage: wasm_sizes.py module.wasm
"""
import sys


def leb(d, i):
    r = 0
    sh = 0
    while True:
        b = d[i]
        i += 1
        r |= (b & 0x7F) << sh
        if not (b & 0x80):
            break
        sh += 7
    return r, i


def sizes(path):
    d = open(path, 'rb').read()
    i = 8
    n = len(d)
    while i < n:
        sid = d[i]
        i += 1
        r, i = leb(d, i)
        end = i + r
        if sid == 10:
            cnt, j = leb(d, i)
            out = []
            for _ in range(cnt):
                ln, j = leb(d, j)
                out.append(ln)
                j += ln
            return cnt, out
        i = end
    return 0, []


def main():
    for p in sys.argv[1:] or ['/tmp/game.wasm']:
        cnt, s = sizes(p)
        if not s:
            print('%s: no code section' % p)
            continue
        top = max(s)
        print('%s: %d functions, largest %d bytes, total code %d bytes%s'
              % (p, cnt, top, sum(s),
                 '  ** largest function exceeds 1 MB **' if top > 1048576 else ''))


if __name__ == '__main__':
    main()
