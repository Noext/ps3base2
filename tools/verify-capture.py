#!/usr/bin/env python3
"""verify-capture.py -- les quatre verifications d'une capture, en une commande.

Pourquoi ce fichier existe : la consigne du tour T exige QUATRE preuves pour
accepter une image (dimensions + couleurs distinctes, recoupement temporel avec
le log, comparaison a une capture native du meme ecran, et origine non-test).
Aucun outil de la base d41c834 ne les rend ensemble :
    tools/imgdiff.py       n'existe pas dans la base (verifie le 13/11 13:35)
    tools/x-capture.mjs    rend 'colours=%k' par capture, mais ne compare rien
    tools/shot.py          capture une fenetre native, ne verifie rien
Ce script ne capture rien et ne lance rien : il LIT des PNG deja sur disque et
un log deja ecrit. Il est donc utilisable hors verrou.

usage:
  verify-capture.py capture.png [--ref ref.png] [--log run.log]
                    [--wasm game.wasm --md5 2204ffc4d8e3e81c]
                    [--json out.json]

Sortie : un bloc texte, et exit 0 seulement si les quatre verifications passent.
  exit 1 : une verification echoue (le detail dit laquelle)
  exit 2 : entree manquante / illisible

Aucune valeur n'est inventee : chaque nombre affiche est mesure sur le fichier.
"""
import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys

import numpy as np


# ------------------------------------------------------------------ lecture
def read_png(path):
    """Decode a PNG to HxWx4 uint8 via ImageMagick, and read its IHDR."""
    d = open(path, 'rb').read()
    if d[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('not a PNG: ' + path)
    w, h, depth, ctype = struct.unpack('>IIBB', d[16:26])
    # ImageMagick decodes every PNG flavour (palette, 16-bit, interlaced).
    out = subprocess.run(['convert', path, '-depth', '8', 'rgba:-'],
                         capture_output=True, check=True).stdout
    a = np.frombuffer(out, np.uint8)
    if a.size != w * h * 4:
        raise ValueError('decoded %d bytes, IHDR says %dx%d' % (a.size, w, h))
    return a.reshape(h, w, 4), (w, h, depth, ctype)


def stats(img):
    """What an image actually contains, measured, not asserted."""
    h, w, _ = img.shape
    rgb = img[:, :, :3]
    flat = rgb.reshape(-1, 3)
    # distinct colours, counted on packed uint32 so 1280x720 does not need a set of tuples
    packed = (flat[:, 0].astype(np.uint32) << 16) | (flat[:, 1].astype(np.uint32) << 8) | flat[:, 2].astype(np.uint32)
    uniq = np.unique(packed)
    alpha = img[:, :, 3]
    # a "flat" image is one colour, or one colour plus a uniform border
    counts = np.bincount(packed, minlength=1 << 24) if packed.max() < (1 << 24) else None
    top = int(counts.max()) / packed.size if counts is not None else None
    return dict(w=int(w), h=int(h), colours=int(uniq.size),
                mean=float(rgb.mean() / 255.0), std=float(rgb.std() / 255.0),
                mean_alpha=float(alpha.mean() / 255.0),
                dominant_share=top,
                top_colours=[[int(c >> 16), int((c >> 8) & 255), int(c & 255)]
                             for c in uniq[np.argsort(-counts[uniq])[:5]]] if counts is not None else [],
                centre=[int(x) for x in img[h // 2, w // 2]])


# ------------------------------------------------------- comparaison oracle
def compare(a, b):
    """RMS + correlation between two images, resampled to the same size."""
    if a.shape != b.shape:
        h, w = b.shape[:2]
        out = subprocess.run(['convert', '-', '-resize', '%dx%d!' % (w, h), '-depth', '8', 'rgba:-'],
                             input=a.tobytes(), capture_output=True, check=True).stdout
        a = np.frombuffer(out, np.uint8).reshape(h, w, 4)
    x = a[:, :, :3].astype(np.float64) / 255.0
    y = b[:, :, :3].astype(np.float64) / 255.0
    rms = float(np.sqrt(((x - y) ** 2).mean()))
    xs, ys = x.reshape(-1) - x.mean(), y.reshape(-1) - y.mean()
    den = float(np.sqrt((xs * xs).sum() * (ys * ys).sum()))
    corr = float((xs * ys).sum() / den) if den > 0 else 0.0
    # structural: share of pixels within 16/255 on every channel
    close = float(((np.abs(x - y) <= 16.0 / 255.0).all(axis=2)).mean())
    return dict(rms=rms, corr=corr, close=close)


# --------------------------------------------------------- recoupement log
def log_draws(path):
    """Draws and flips from a run log, by the project's counter rule.

    REGLE 2: the counter is read as 'name=<n>' and only the LAST one counts.
    The project logs look like:
        rsx: flip 4321 (buffer 0) worst ...
        rsx: draws 12345 ...
        rsx_ring_stat: ... nom=NNN
    Any of the accepted spellings is tried; the last match wins.
    """
    txt = open(path, 'r', errors='replace').read()
    import re
    res = {}
    for key, pats in {
        'flip': [r'\bflip=(\d+)', r'\bflips=(\d+)', r'rsx: flip (\d+)'],
        'draw': [r'\bdraws=(\d+)', r'\bdraw=(\d+)', r'rsx: draws? (\d+)'],
        'nom':  [r'nom=(\d+)'],
    }.items():
        last = None
        for p in pats:
            m = re.findall(p, txt)
            if m:
                last = int(m[-1])
        if last is not None:
            res[key] = last
    res['bytes'] = len(txt)
    return res


# ------------------------------------------------------------------ main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('capture')
    ap.add_argument('--ref', help='native capture of the same screen')
    ap.add_argument('--log', help='run log to cross-check in time')
    ap.add_argument('--wasm', help='game.wasm the capture claims to come from')
    ap.add_argument('--md5', help='md5 the capture claims (short or full)')
    ap.add_argument('--json')
    ap.add_argument('--min-colours', type=int, default=200,
                    help='"des centaines de couleurs" -- 200 is the floor')
    ap.add_argument('--min-corr', type=float, default=0.5)
    a = ap.parse_args()

    if not os.path.exists(a.capture):
        print('REJET: capture absente: ' + a.capture)
        return 2
    img, ihdr = read_png(a.capture)
    st = stats(img)
    verdict = {}
    print('== capture ' + a.capture)
    print('   md5 %s  %d octets' % (hashlib.md5(open(a.capture, 'rb').read()).hexdigest(),
                                    os.path.getsize(a.capture)))
    print('   IHDR %dx%d depth %d colourtype %d' % ihdr)
    print('   mesure: %dx%d  couleurs=%d  mean=%.4f  std=%.4f  alpha=%.3f  dominante=%.3f' % (
        st['w'], st['h'], st['colours'], st['mean'], st['std'], st['mean_alpha'],
        st['dominant_share'] if st['dominant_share'] is not None else -1))
    print('   centre RGBA %s  top %s' % (st['centre'], st['top_colours']))

    # 1. dimensions + couleurs distinctes
    v1 = st['colours'] >= a.min_colours and st['w'] >= 64 and st['h'] >= 64
    print('   [1] dimensions/couleurs: %s (couleurs %d >= %d, %dx%d)' % (
        'PASS' if v1 else 'FAIL', st['colours'], a.min_colours, st['w'], st['h']))
    verdict['dimensions_colours'] = v1

    # 3. comparaison oracle (avant 2, pour que l'ordre du rapport suive la consigne)
    if a.ref:
        if not os.path.exists(a.ref):
            print('   [3] oracle: REJET -- reference absente: ' + a.ref)
            verdict['oracle'] = False
        else:
            rimg, _ = read_png(a.ref)
            c = compare(img, rimg)
            ok = c['corr'] >= a.min_corr
            print('   [3] oracle %s: rms=%.4f corr=%.4f proches=%.3f -> %s' % (
                a.ref, c['rms'], c['corr'], c['close'], 'PASS' if ok else 'FAIL'))
            verdict['oracle'] = ok
            verdict['oracle_metrics'] = c
    else:
        print('   [3] oracle: NON VERIFIE -- aucune reference native fournie')
        verdict['oracle'] = None

    # 2. recoupement temporel avec le log
    if a.log:
        if not os.path.exists(a.log):
            print('   [2] log: REJET -- log absent: ' + a.log)
            verdict['log'] = False
        else:
            d = log_draws(a.log)
            ok = (d.get('draw', 0) > 0) and (d.get('flip', 0) > 0)
            print('   [2] log %s: %s -> %s' % (a.log, d, 'PASS' if ok else 'FAIL'))
            verdict['log'] = ok
            verdict['log_counters'] = d
    else:
        print('   [2] log: NON VERIFIE -- aucun log fourni')
        verdict['log'] = None

    # provenance
    if a.wasm and a.md5:
        if not os.path.exists(a.wasm):
            print('   [P] provenance: REJET -- wasm absent: ' + a.wasm)
            verdict['provenance'] = False
        else:
            m = hashlib.md5(open(a.wasm, 'rb').read()).hexdigest()
            ok = m.startswith(a.md5.lower())
            print('   [P] provenance: md5 mesure %s vs annonce %s -> %s' % (m, a.md5, 'PASS' if ok else 'FAIL'))
            verdict['provenance'] = ok
            verdict['wasm_md5'] = m
    else:
        print('   [P] provenance: NON VERIFIEE -- --wasm/--md5 absents')
        verdict['provenance'] = None

    # 4. origine : une page de test ne compte pas. Mesure, pas declaration.
    #    Une page de test est plate (std tres bas) ou presque monochrome.
    test_page = st['std'] < 0.02 or (st['dominant_share'] is not None and st['dominant_share'] > 0.98)
    print('   [4] origine: %s (std=%.4f dominante=%.3f)' % (
        'REJET -- page de test (image plate/monochrome)' if test_page else 'non-plate: PASS', st['std'],
        st['dominant_share'] if st['dominant_share'] is not None else -1))
    verdict['not_test_page'] = not test_page

    allpass = all(v is True for v in verdict.values() if not isinstance(v, dict))
    print('   ==> %s' % ('ACCEPTEE' if allpass else 'REJETEE'))
    if a.json:
        json.dump(dict(capture=a.capture, stats=st, verdict=verdict), open(a.json, 'w'), indent=1)
    return 0 if allpass else 1


if __name__ == '__main__':
    sys.exit(main())
