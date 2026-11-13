# Prompt de reprise — Red Dead Redemption (BLES00680)

## Nouvelle organisation, permanente, à écrire dans AGENTS.md

**Un seul sous-agent à la fois.** Tu en lances un, tu attends son statut `done` ou
`blocked`, tu lis son `RESULTS.md` **en entier**, tu fais vérifier et fusionner, puis
seulement tu lances le suivant. Jamais deux en parallèle.

**Une seule instance de jeu à la fois sur la machine**, sous verrou, durée bornée,
tuée par celui qui l'a lancée. **Au plus une compilation à la fois.**

**Un compte rendu par tour**, à la fin, après le dernier sous-agent. Tu ne poses pas
de question entre-temps : si un choix se présente, tu prends celui qui produit une
mesure et tu le notes.

**Tu ne conclus rien sur un sous-agent sans avoir lu son rapport.** Tu ne compares
jamais une adresse de descripteur à une borne de code : **un descripteur est une
donnée qui contient l'adresse du code**.

---

## Contexte machine — à ne pas redécouvrir

**Dépôt :** `/root/Dev/ps3base2`, branche `main`, HEAD `f0df369`.

**Entrées obligatoires :**

| quoi | chemin |
|---|---|
| disque du jeu | `/root/Dev/rdr-port/vfs/dev_bdvd` (BLES00680) |
| dev_flash | `/root/Dev/devflash-out/dev_flash` |
| lift PPU | `/root/Dev/ps3base2/out-rdr/ppu` |
| lift SPU | `/root/Dev/ps3base2/out-rdr/spu` |
| binaire | `/root/Dev/ps3base2/build-rdr/ps3base` — **ABSENT, à reconstruire** |

**`GOWA_FLASH_DIR` est obligatoire.** Sans lui RDR n'a pas son codec audio
(`flashMP3.pic`), le champ `+0x64` de `0x0177cc00` n'est jamais écrit, et le jeu
meurt à 23 s sur une DMA à l'adresse 0. Procédure d'extraction documentée dans
`docs/findings/f-dev-flash-and-rounding.md`.

**Reconstruction :**

```bash
cmake -S /root/Dev/ps3base2 -B build-rdr -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DPS3BASE_GENERATED_DIR=/root/Dev/ps3base2/out-rdr/ppu \
  -DGOWA_SPU_GENERATED_DIR=/root/Dev/ps3base2/out-rdr/spu \
  -DPS3BASE_SPU_NATIVE_GEN_DIR=/root/Dev/ps3base2/out-rdr/gen/spu
./tools/build_slot.sh cmake --build build-rdr -j 8 --target ps3base
```

`ulimit -s unlimited` avant de builder (`rdr_049.cpp` tue clang sinon).

**Lancement :**

```bash
GOWA_FLASH_DIR=/root/Dev/devflash-out/dev_flash \
PS3BASE_DISC_DIR=/root/Dev/rdr-port/vfs/dev_bdvd \
PS3BASE_LOG=info DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 \
./build-rdr/ps3base --no-launcher
```

**Relift :**

```bash
python3 tools/setup.py --steps ppu --images recomp/images-rdr.json \
  --elf bin/rdr/EBOOT.elf --out-root out-rdr --gen-root out-rdr/gen --allow-unknown
```

---

## État mesuré — ne pas re-dériver

**RDR atteint et dépasse les menus :**

```
écran titre "RED DEAD REDEMPTION" + "Press START"     capturé
ALERT "save could not be found"                        capturé
WARNING (2 branches : Carré et Croix)                  capturé
LOADING                                                 capturé
CHAPITRE "EXODUS IN AMERICA"                           capturé  <- dernier écran
```

Preuves dans `docs/evidence/` : `rdr-title-screen-raw.png`,
`rdr-alert-no-save.png`, `rdr-p4-warning.png`,
`q3-{square,cross}-{alert,warning,loading,chapter}.png`.

**Pilotage :** `tools/drive_rdr.py`. Séquence mesurée :

```
titre -> ENTRÉE (START) -> menu principal -> ALERT
  Carré = Continue Without Saving   Croix = Create A New Save Game   Cercle = Retry
-> WARNING -> Croix (Continue) -> LOADING -> CHAPITRE
```

Mapping clavier confirmé (`runtime/src/host_input.cpp:125,136`) : **ENTRÉE = START**
(`b[2]` bit `0x08`), **ESPACE = CROSS** (`b[3]` bit `0x40`).

**La branche Croix écrit 3 vraies sauvegardes** (`RAGE.SAV` 74 951 o,
`ICON0.PNG` PNG 320x176 valide, `PARAM.SFO` PSF v1.1 avec
`SUB_TITLE='Exodus in America'`). `savedata/` doit être vidé entre deux runs,
sinon le jeu charge une sauvegarde au lieu d'afficher l'ALERT.

**Correctifs déjà en place, ne pas défaire :**

- `cellFiber` en HLE (`runtime/src/hle/cellFiber.cpp`), défaut ON,
  `GOWA_FIBER_HLE=0` restaure le LLE. A supprimé un SIGSEGV à `guest 0x10`.
- `RoundGuard` arrondit vers zéro (`runtime/src/spu/ops_impl.h`), comme la console.
- dev_flash réel extrait du PUP 4.91.

**Le blocage actuel :**

```
F ppu: unknown indirect target 0x00ae5818 -- rdr+0xad5618 (no lifted code)
  (lr=0x00adc030, thread 0x1000000) r3=0x1 r4=0xae5818 ctr=0xae5818
```

C'est une **table de sauts à `0x013e5c8c`, 17 entrées**, `0xae5818` = entrée 13 :

```
[ 0] 0x00ae517c   [ 1] 0x00ae51f0   [ 2] 0x00ae5284   [ 3] 0x00ae5328
[ 4] 0x00ae5580   [ 5] 0x00ae53d4   [ 6] 0x00ae55d0   [ 7] 0x00ae5630
[ 8] 0x00ae5484   [ 9] 0x00ae5528   [10] 0x00ae56a8   [11] 0x00ae5720
[12] 0x00ae57d8   [13] 0x00ae5818   [14] 0x00ae5424   [15] 0x00ae5858
[16] 0x00ae58b8
```

**Aucune des 17 n'est liftée.** Le relift avec la règle `run_extent` de Q1 ne les
produit pas : le total reste à 76 793 fonctions.

**Le mécanisme de table manuelle existe déjà** : `recomp/ppu/analysis.py:603`
`load_switch_tables()`, activé par la règle `manual` du profil `ALL_RULES` (dont
RDR hérite). Format attendu :

```toml
[[table]]
bctr  = 0x00adc030     # le bctr qui dispatche
base  = 0x013e5c8c     # première entrée en mémoire invitée
count = 17
note  = "..."
```

Le fichier se lit à `out-rdr/gen/switch_tables.toml`. **Il n'existe pas encore.**

**Deux pièges connus :**

1. Le chemin `manual` a déjà été **silencieusement perdu** lors d'une réécriture
   ("announced then ignored", `analysis.py:745`). Vérifier qu'il atteint bien
   `explore()`, pas seulement le log.
2. `load_switch_tables` valide chaque cible : elle doit être dans la plage de code,
   et le `bctr` aussi. Un typo échoue au lift avec l'adresse.

---

## File de ce tour, dans l'ordre, un par un

### A — La table `0x013e5c8c`

Ajoute-la **à la main** dans `switch_tables.toml` (adresse du `bctr`, base,
17 entrées, cibles), comme pour la table de 256 entrées. Relift, puis vérifie
**sur le cas réel** : les 17 cibles sont des étiquettes de la fonction, et le jeu
dépasse `rdr+0xad5618`.

**Le correctif de la règle `run_extent` vient ensuite**, avec un test construit sur
les **vrais octets du jeu** autour de ce `bctr`, pas sur une forme synthétique.
Le test de Q1 passe sur une forme qu'il a imaginée et pas sur le binaire réel —
c'est exactement le piège à éviter.

### B — Pilotage

`drive_rdr.py` rejoue le chemin jusqu'à « EXODUS IN AMERICA », puis **continue** :
capture de chaque écran, cinématique d'introduction, premier moment où le joueur a
la main.

### C — Stabilité du chemin

**Trois runs de suite** jusqu'au même point, sans crash. Tout arrêt est traité par la
descente habituelle (site, feuille, cause).

### V — Vérificateur après chaque fusion

Tests, porte de couverture des exports (`tools/check_export_coverage.py`, tourne au
configure), `regress.sh` sur les trois jeux, **un jeu à la fois**.

---

## Règles de mesure

**Toute comparaison est un A/B sur la même machine, dans la même minute.** Deux runs
séparés de plusieurs minutes sur une machine partagée ne sont pas comparables —
cette erreur a déjà produit un faux rapport de régression.

**Machine au repos pour les mesures de flips.** Vérifier
`cut -d' ' -f1-3 /proc/loadavg` avant et noter la charge.

**Surveiller la RAM**, pas seulement le CPU et le disque. Un script d'analyse a déjà
pris 46 Go et saturé la machine. Plafonner tout script Python :

```bash
ulimit -v 2000000 && timeout 60 python3 tools/check_export_coverage.py <table>
```

**Ne jamais supprimer hors de son périmètre.** `/root/Dev/wasm-base`,
`/root/Dev/t-T1`, `/root/Dev/t-T2`, `/root/Dev/t-V` et leurs branches ne sont pas à
nous.

**Les 10 branches non fusionnées de la session précédente** (`explore/n2`, `y2`,
`u4`, `w1`, `n1`, `n3`, `n4`, `n5`, `u3`, `y3`) contiennent du travail réel qui
n'est pas dans `main` — notamment le fuzzer SPU (`tests/spu_op_fuzz.cpp`,
24,9 M vérifications) et `tools/ppu_trace.py`. **Ne pas les supprimer.**

---

## Compte rendu unique à la fin

Le résultat de **A sur le cas réel**, la **capture du dernier écran atteint après
« EXODUS IN AMERICA »**, et le **statut de chaque étape**.
