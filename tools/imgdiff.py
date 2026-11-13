#!/usr/bin/env python3
"""Chiffrer la difference entre deux images PPM (P6), canal par canal.

usage: imgdiff.py <a.ppm> <b.ppm> [--top N]

Sortie (une ligne par metrique, valeur = sortie brute, jamais estimee):
  size        dimensions des deux images
  rms_r/g/b   ecart quadratique moyen par canal, 0..255
  rms         idem sur les trois canaux
  max         ecart absolu maximal observe
  over_1/2/4/8/16/32   pourcentage de pixels dont AU MOINS UN canal depasse N
  coverage    pourcentage de pixels non noirs dans chaque image
  mean_a/mean_b  luminance moyenne de chaque image

Un pixel noir a 0 a 0 n'est PAS ignore: il compte, parce que "le backend n'a
rien dessine la ou l'autre a dessine" est exactement le desaccord a chiffrer.
"""
import sys


def load(path):
    with open(path, 'rb') as f:
        data = f.read()
    if not data.startswith(b'P6'):
        raise SystemExit(f'{path}: not P6')
    # header: P6 <w> <h> <max>, whitespace-separated, comments possible
    parts = []
    i = 2
    while len(parts) < 3:
        while i < len(data) and data[i:i + 1].isspace():
            i += 1
        if i < len(data) and data[i:i + 1] == b'#':
            while i < len(data) and data[i:i + 1] != b'\n':
                i += 1
            continue
        j = i
        while j < len(data) and not data[j:j + 1].isspace():
            j += 1
        parts.append(data[i:j])
        i = j
    i += 1  # single whitespace after maxval
    w, h, maxv = int(parts[0]), int(parts[1]), int(parts[2])
    if maxv != 255:
        raise SystemExit(f'{path}: maxval {maxv} not 255')
    px = data[i:i + w * h * 3]
    if len(px) != w * h * 3:
        raise SystemExit(f'{path}: short pixel data {len(px)} != {w*h*3}')
    return w, h, px


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    top = 0
    if '--top' in sys.argv:
        top = int(sys.argv[sys.argv.index('--top') + 1])
    wa, ha, pa = load(sys.argv[1])
    wb, hb, pb = load(sys.argv[2])
    print(f'size        {wa}x{ha} vs {wb}x{hb}')
    if (wa, ha) != (wb, hb):
        print('MISMATCH: dimensions differ, nothing to compare')
        return 2
    n = wa * ha
    sums = [0, 0, 0]
    maxd = 0
    over = {k: 0 for k in (1, 2, 4, 8, 16, 32)}
    nonblack_a = nonblack_b = 0
    luma_a = luma_b = 0
    for p in range(n):
        base = p * 3
        pa0, pa1, pa2 = pa[base], pa[base + 1], pa[base + 2]
        pb0, pb1, pb2 = pb[base], pb[base + 1], pb[base + 2]
        d0, d1, d2 = pa0 - pb0, pa1 - pb1, pa2 - pb2
        a0, a1, a2 = abs(d0), abs(d1), abs(d2)
        sums[0] += d0 * d0
        sums[1] += d1 * d1
        sums[2] += d2 * d2
        m = max(a0, a1, a2)
        if m > maxd:
            maxd = m
        for k in over:
            if m > k:
                over[k] += 1
        if pa0 or pa1 or pa2:
            nonblack_a += 1
        if pb0 or pb1 or pb2:
            nonblack_b += 1
        luma_a += (pa0 * 299 + pa1 * 587 + pa2 * 114) // 1000
        luma_b += (pb0 * 299 + pb1 * 587 + pb2 * 114) // 1000
    import math
    rms = [math.sqrt(s / n) for s in sums]
    print(f'rms_r       {rms[0]:.4f}')
    print(f'rms_g       {rms[1]:.4f}')
    print(f'rms_b       {rms[2]:.4f}')
    print(f'rms         {math.sqrt((sums[0]+sums[1]+sums[2])/(3*n)):.4f}')
    print(f'max         {maxd}')
    for k in (1, 2, 4, 8, 16, 32):
        print(f'over_{k:<3}     {100.0*over[k]/n:.4f}%')
    print(f'coverage_a  {100.0*nonblack_a/n:.4f}%')
    print(f'coverage_b  {100.0*nonblack_b/n:.4f}%')
    print(f'mean_a      {luma_a/n:.4f}')
    print(f'mean_b      {luma_b/n:.4f}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
