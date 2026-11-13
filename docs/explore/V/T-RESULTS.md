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

---

## 5. Criteres a preserver — etat mesure sur la base d41c834

### 5.1 Critere 4 (test C) : **ABSENT DE LA BASE**. Un SKIP est un ECHEC, et ici il n'y a meme pas de SKIP.

```
$ cd /root/Dev/wasm-base && git ls-tree -r --name-only d41c834 | grep -i 'rsx_fifo_synth'
(rien)
$ git ls-tree -r --name-only d41c834 | grep -i synth
third_party/glslang/Test/baseResults/hlsl.synthesizeInput.frag.out
third_party/glslang/Test/hlsl.synthesizeInput.frag
$ git log --oneline d41c834 -- 'tests/rsx_fifo_synth*'
(rien)
$ git merge-base --is-ancestor 8df4d6b d41c834 && echo YES || echo NO
NO
$ git branch -a --contains 8df4d6b
  explore/zZ4
```

**Constat** : `tests/rsx_fifo_synth.cpp`, `tests/rsx_fifo_synth_support.cpp` et
`tests/rsx_fifo_synth.sh` n'existent **pas** sur d41c834. Ils vivent sur la seule branche
`explore/zZ4` (commit 8df4d6b « Z4: tests/rsx_fifo_synth 12 PASS / 0 FAIL on HEAD »), qui n'est
pas un ancetre de d41c834. Le commit `960a77a` (« wip: sauvegarde avant nettoyage ») les porte
aussi mais n'est pas non plus un ancetre.

Consequence pour la file de fusion : **toute annonce de ce tour qui ecrit « test C 12 PASS / 0
FAIL » sans dire sur quelle branche est un REJET**, parce que la base sur laquelle le tour est
construit ne contient pas le test. Le « 12 PASS » de I2-ETAT.md et de Z0-METHODES-VUES.md a ete
obtenu sur `explore/zZ4`, pas sur d41c834. Le critere 4 est donc **ROUGE par absence** sur la base
du tour, et il le reste tant que le test n'est pas fusionne dans main.

### 5.2 Critere 2 (`_gcm_intr_thread`) : ce n'est PAS un symbole du binaire, c'est une ligne de log

```
$ grep -c '_gcm_intr_thread' /tmp/out-T/game.js      -> 0
$ strings /tmp/out-T/game.wasm | grep -c '_gcm_intr_thread' -> 0
$ grep -c 'gcm_intr_thread' /tmp/out-T/game.wasm     -> 0
```

Le nom n'apparait dans aucun des deux artefacts. C'est normal : `_gcm_intr_thread` est le **nom de
thread** que le guest passe a `sys_ppu_thread_create`, donc il n'apparait que dans le LOG d'un run
(`stacks: thread ... '_gcm_intr_thread'`). La mesure valide est donc, sur un log de run :

    grep -oE '_gcm_intr_thread' run.log | wc -l      (5 attendu, cf. docs/explore/H4-QUATRE-CRITERES.md)

**Aucune annonce ne peut prouver le critere 2 par inspection du binaire.** Une annonce qui cite un
`grep` sur game.wasm pour ce critere est rejetee pour methode.

### 5.3 Critere 3 (flips par 0x102) : la sonde existe, elle est compilee-hors par defaut

```
$ grep -n 'PS3BASE_FLIP_PROBE' runtime/src/lv2/rsx.cpp
216:    case 0x102: {
217:#if defined(PS3BASE_FLIP_PROBE) && PS3BASE_FLIP_PROBE
```

La sonde est dans la base mais **desactivee par defaut** : il faut `-DPS3BASE_FLIP_PROBE=1` au
build. Un module construit sans cette option ne peut pas prouver le critere 3 par la sonde ; il ne
peut le prouver que par `rsx: flip <n>` dans le log (regle 2 : `grep -oE 'flip [0-9]+' log | tail -1`).

### 5.4 Critere 1 (40 en-tetes FIFO) : instrument `PS3BASE_RSXB_PROBE`, lui aussi compile-hors

```
$ grep -Rl 'PS3BASE_RSXB_PROBE' .
./tools/build_wasm.sh  ./docs/explore/H4-QUATRE-CRITERES.md  ./runtime/src/lv2/rsx.cpp
```

Meme remarque : sans `-DPS3BASE_RSXB_PROBE=1`, le log ne porte pas de ligne `rsxb:` et le critere 1
n'est pas mesurable sur ce build. La reference de format (H4) : premier en-tete
`raw 00040060 56616661`, appel vers offset 0 `ea 0x40100000`, `put 0x000013a0`.

### 5.5 Critere 5 (tests unitaires) : la liste des cibles existe, l'execution est hors de mon perimetre

```
$ grep -n 'add_executable\|add_test' CMakeLists.txt | head
244:enable_testing()
245:add_executable(lv2_enosys tests/lv2_enosys.cpp)
248:add_test(NAME lv2_enosys COMMAND lv2_enosys)
...
```

Les cibles sont declarees dans le `CMakeLists.txt` racine (pas de `tests/CMakeLists.txt`). Le build
natif est un travail de T1/T2/T3 ; je verifie leur sortie, je ne le lance pas.

### 5.6 Provenance de l'oracle natif du logo (critere 3 des captures)

```
$ md5sum /tmp/V-native/native-logo.png
21a792fe661397702f5773d67453c59b  /tmp/V-native/native-logo.png
$ python3 tools/verify-capture.py /tmp/V-native/native-logo.png
   IHDR 1280x720 depth 8 colourtype 2
   mesure: 1280x720  couleurs=49699  mean=0.5406  std=0.2979  alpha=1.000  dominante=0.230
   centre RGBA [207, 207, 207, 255]
```

**Caveat de provenance, dit franchement** : ce PNG est un **oracle natif du logo** utilisable pour le
critere 3, mais son log associe (`/tmp/V-native/run.log`) **ne contient aucune ligne de capture**
(pas de `import`/`xwd`/`screenshot`). Le fichier est donc un oracle d'IMAGE valide (49 699 couleurs,
1280x720, non plat) mais sa **commande de capture n'est pas tracee** dans le log. Je l'accepte comme
reference de comparaison, je ne l'accepte pas comme preuve de provenance independante.

