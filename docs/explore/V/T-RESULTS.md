# PISTE V — VERIFICATEUR (tour T) — T-RESULTS.md

Base : /root/Dev/wasm-base @ **d41c834** (HEAD verifie : `git -C /root/Dev/wasm-base log --oneline -1`).
Worktree : /root/Dev/t-V (branche explore/tV, cree le 13/11 13:34).
Arbres partages lies : bin out out-mc fw ref -> /root/Dev/ps3base2/*.

**Role** : je ne lance RIEN. Je verifie les mesures des autres (T1 = agent-51, T2 = agent-52,
T3 = agent-53). Chaque annonce est REJOUEE ou REJETEE sur trois pieces : md5 du game.wasm,
chemin exact, commande.

---

## 0. Regles appliquees, sans exception

| Regle | Application |
|---|---|
| Interdiction du natif | aucun `ps3base`, `play.sh`, `run_world.sh`, `regress.sh` lance par moi |
| Une seule instance | WASM dans Chromium, `flock -w 120 /tmp/game.lock timeout 180` |
| Avant tout lancement | `pgrep -f 'ps3base --no-launcher' \| wc -l` doit etre 0 |
| Regle 1 (instrument d'abord) | `grep -rn <nom> .` avant d'ecrire un outil (section 1) |
| Regle 2 (compteurs) | `grep -oE 'nom=[0-9]+' log \| tail -1`, jamais `grep -c mot` |
| Worktrees | jamais supprimes ; `python3 tools/wt_gc.py` en dry-run uniquement |

## 1. Inventaire des instruments (REGLE 1) — mesure, pas supposition

```
$ cd /root/Dev/wasm-base && for f in tools/x-capture.mjs tools/imgdiff.py tools/g4_counters.py \
    tools/divergence.py tools/wasm_sizes.py tools/disc_verify.py tools/wt_gc.py \
    web/serve.py docs/explore/X6/x6-inpage-hook.js; do
    [ -e "$f" ] && echo "PRESENT $f" || echo "MISSING $f"; done
PRESENT tools/x-capture.mjs
MISSING tools/imgdiff.py
MISSING tools/g4_counters.py
MISSING tools/divergence.py
PRESENT tools/wasm_sizes.py
MISSING tools/disc_verify.py
PRESENT tools/wt_gc.py
PRESENT web/serve.py
PRESENT docs/explore/X6/x6-inpage-hook.js
```

```
$ grep -Rl 'PS3BASE_RSXB_PROBE' .   ->  ./tools/build_wasm.sh  ./docs/explore/H4-QUATRE-CRITERES.md  ./runtime/src/lv2/rsx.cpp
$ grep -Rl 'PS3BASE_FLIP_PROBE' .   ->  ./runtime/src/lv2/rsx.cpp
```

Consequence : `tools/imgdiff.py` **n'existe pas** dans la base d41c834. Une annonce qui cite
`tools/imgdiff.py` comme instrument de mesure cite un fichier absent de la base : elle est
rejetee pour provenance, sauf si l'agent fournit le script et son chemin dans SON worktree.

## 2. Provenance de reference (l'unique binaire de ce tour)

```
$ cd /tmp/out-T && md5sum game.wasm game.data game.js
2204ffc4d8e3e81cce2cfa6348b5f5ed  game.wasm
32b6950390e96442c984aad74db8c260  game.data
3f83e2128219f8072ee43cad7ee15627  game.js
$ xxd -l 32 game.wasm
00000000: 0061 736d 0100 0000 01a1 34b1 0360 027f  .asm......`.
$ python3 tools/wasm_sizes.py /tmp/out-T/game.wasm
/tmp/out-T/game.wasm: 49715 functions, largest 737751 bytes (no name section), total code 44851780 bytes
```

md5 court attendu par la consigne : `2204ffc4d8e3e81c`. Conforme. **Toute capture dont la
provenance ne porte pas ce md5 (ou un md5 de build T1/T2/T3 explicitement lie) est rejetee.**

## 3. Les quatre verifications d'une capture

Aucune capture n'est acceptee sans les quatre :

1. **dimensions** + **nombre de couleurs distinctes** (des centaines ; une couleur unie = rejet) ;
2. **recoupement temporel** avec le log du run (draws non nuls au meme instant) ;
3. **comparaison** avec une capture native existante du meme ecran ;
4. **origine** : si la capture vient d'une page de test (clear, quad), ce n'est PAS le jeu.

## 4. Etat au moment d'ouvrir ce document (13/11 13:35)

- Aucune capture WASM/Chromium du logo n'existe : `find /tmp /root/Dev -name '*.png' -newermt '2026-11-13 10:00' -not -path '*/third_party/*'`
  ne rend que des PNG **du depot** (mtime = heure du checkout du worktree t-T1, 13:34) et des
  PNG de pistes anterieures (Q1/Q3, 12:0x-12:4x).
- `/tmp/out-T/game.wasm` est le binaire de reference (md5 ci-dessus).
- Aucun processus de jeu natif : `pgrep -f 'ps3base --no-launcher' | wc -l` -> 0.

Les sections suivantes sont remplies AU FUR ET A MESURE des annonces, avec commande + sortie brute.
