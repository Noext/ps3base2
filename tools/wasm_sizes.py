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
    """Return (count, sizes, names).

    names maps FUNCTION INDEX to symbol name when a name section is present. The
    index spaces do NOT line up naively: the name section may list more entries
    than the code section has bodies, with the extras at the end. Round 154 found
    this the hard way -- pairing size and name by index reported an 8,964-byte
    function as 1,316,534 bytes and cost seven rounds of chasing a phantom.
    """
    d = open(path, 'rb').read()
    i = 8
    n = len(d)
    out, names = [], {}
    count = 0
    while i < n:
        sid = d[i]
        i += 1
        r, i = leb(d, i)
        end = i + r
        if sid == 10:
            count, j = leb(d, i)
            for _ in range(count):
                ln, j = leb(d, j)
                out.append(ln)
                j += ln
        elif sid == 0:
            ln, j = leb(d, i)
            nm = d[j:j + ln].decode('utf8', 'replace')
            if nm == 'name':
                k = j + ln
                while k < end:
                    sub = d[k]
                    k += 1
                    sz, k = leb(d, k)
                    if sub == 1:
                        cnt, k2 = leb(d, k)
                        for _ in range(cnt):
                            idx, k2 = leb(d, k2)
                            sl, k2 = leb(d, k2)
                            names[idx] = d[k2:k2 + sl].decode('utf8', 'replace')
                            k2 += sl
                    k += sz
        i = end
    return count, out, names


def main():
    for p in sys.argv[1:] or ['/tmp/game.wasm']:
        cnt, s, names = sizes(p)
        if not s:
            print('%s: no code section' % p)
            continue
        top = max(s)
        idx = s.index(top)
        named = names.get(idx)
        print('%s: %d functions, largest %d bytes%s, total code %d bytes%s'
              % (p, cnt, top, (' (%s)' % named) if named else ' (no name section)',
                 sum(s),
                 '  ** largest function exceeds 1 MB **' if top > 1048576 else ''))
        if names:
            # Sanity: the name index space may be longer than the code section.
            extra = len([k for k in names if k >= cnt])
            if extra:
                print('   note: %d name entries have no matching body' % extra)


if __name__ == '__main__':
    main()
