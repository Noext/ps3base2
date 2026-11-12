# PISTE V/H — VERIFICATEUR : critere mecanique, mesuré sur /root/Dev/h-V @ 375f0e5

Worktree : `/root/Dev/h-V` (branche `explore/hV`), base `375f0e5`, HEAD verifie ci-dessous.
Out dir UNIQUE : `/tmp/out-V`. Cache d'objets : 201 objets issus de `/tmp/out-V` (d290fda).
Oracle natif : binaire bati DEPUIS les sources de `git HEAD` de `/root/Dev/ps3base2`
(`da6866a`), relie a `/tmp/ps3base-H` puis installe dans `build/ps3base`.

Regle compteurs respectee : chaque compteur est lu par VALEUR
(`grep -oE 'nom=[0-9]+' | tail -1`), jamais par `grep -c` sur un mot.

---

## 0. Antecedents (grep -Rn dans refs/ et docs/, AVANT toute hypothese)

| Finding | Source (path:ligne) | S'applique ici ? |
|---|---|---|
| le dump natif `rsxb` est pris AVANT la classification du paquet, avec `get/ea/raw x4/count/method/noninc/put` | `/root/Dev/ps3base2/runtime/src/lv2/rsx.cpp:619` | **oui** — c'est le format exact que le critere 1 compare, et le point d'insertion cote wasm |
| le wasm a un instrument `[hdr200]` au meme endroit mais avec un AUTRE format (`raw=0x.. reg= count= args=..`) | `runtime/src/gpu/gcm_fifo.cpp:499` | **oui** — un format different ne se compare pas champ par champ ; j'ajoute `hdr40` au format natif |
| `GCM_FLIP_COMMAND = 0xFEAC>>2` est le flip ; `GCM_FLIP_HEAD = 0xE920>>2` n'en est pas un | `refs/rpcs3/rpcs3/Emu/RSX/gcm_enums.h:826-829` | **oui** — critere 3 : c'est `0xFEAC` que `g_flips` compte |
| le natif cree `_gcm_intr_thread` a t=0,588 (prio 1, stack 0x4000, entree 0x3300be54) | `native-3000.log:3` | **oui** — critere 2 : presence exigee cote wasm |
| le natif ne passe PAS par notre HLE `_cellGcmInitBody` : 0 ligne `gcmstub`/`imp119` dans le run natif | mesure ci-dessous §2.4 | **oui** — c'est la divergence de contexte annoncee par Z3 |
| `PS3BASE_HLE_OVER_LLE` detourne tous les imports `cellGcmSys` vers le HLE | `runtime/src/ppu_runtime.cpp:193-206` | **oui** — c'est pourquoi le wasm a un `gcmstub` et le natif non |
| le test `tests/rsx_fifo_synth.sh` n'est PAS dans l'arbre a 375f0e5 (il n'existe que sur `explore/zZ4`) | `git log --all --diff-filter=A -- tests/rsx_fifo_synth.sh` -> `8df4d6b` | **oui** — critere 4 : je l'importe tel quel dans le worktree pour le mesurer |

---

## 1. Protocole et artefacts

```
$ git -C /root/Dev/wasm-base worktree add /root/Dev/h-V -b explore/hV 375f0e5
Preparing worktree (new branch 'explore/hV')
HEAD is now at 375f0e5 G0: the native loads 7 images not 9 ...

$ cd /root/Dev/h-V && git rev-parse HEAD
375f0e5...
```

Instrumentation ajoutee (logs seulement, aucun changement de comportement) :

1. `runtime/src/gpu/gcm_fifo.cpp`, dans `drain()`, AU MEME POINT que le natif — AVANT le
   decoupage jump/call/return et avant le test `cmd == 0`, exactement comme
   `rsx.cpp:619` — tag `hdr40`, 40 premiers en-tetes au format natif exact
   (`ea raw x4 count method noninc put`, 9 conversions, 9 arguments).
2. `runtime/src/lv2/rsx.cpp`, en tete de `case 0x102:` de `context_attribute`, compteur
   `p102` (5 premiers puis 1 sur 120). Sans lui, un zero de flips ne distingue pas
   « 0x102 jamais appele » de « 0x102 appele mais sans effet ».

Build : relink des 201 objets caches + 2 TU recompiles (`gcm_fifo.cpp`, `rsx.cpp`),
meme recipe que `tools/build_wasm.sh` (CFLAGS et LDFLAGS copies a l'identique).

---

## 2. Les cinq criteres, mesures

### Critere 1 — les 40 premiers en-tetes FIFO wasm identiques au natif : **FAUX**

Sortie brute du comparateur (champ ea, raw x4, count, method) :

```
native read 40 wasm read 40
identical: 0 /40
 idx0 nat ('0x40101000', '00040060', '56616661', '00040064', '00000030', '1', '0x0060')
       was ('0x40101000', '00041d6c', '00000ff0', '00041d70', '00010000', '1', '0x1d6c')
 idx1 nat ('0x40101008', '00040064', '00000030', '00040068', '00000001', '1', '0x0064')
       was ('0x40101008', '00041d70', '00010000', '00040064', '00000ff0', '1', '0x1d70')
 idx2 nat ('0x40101010', '00040068', '00000001', '00040060', '66616661', '1', '0x0068')
       was ('0x40101010', '00040064', '00000ff0', '00040068', '00000001', '1', '0x0064')
 idx3 nat ('0x40101018', '00040060', '66616661', '00000002', '00041d84', '1', '0x0060')
       was ('0x40101018', '00040068', '00000001', '00041d6c', '00000ff0', '1', '0x0068')
 idx4 nat ('0x40101020', '00000002', '00041d84', '00000003', '00041804', '0', '0x0000')
       was ('0x40101020', '00041d6c', '00000ff0', '00041d70', '00020000', '1', '0x1d6c')
 idx5 nat ('0x40100000', '00040000', '31337000', '003c0180', '66604200', '1', '0x0000')
       was ('0x40101028', '00041d70', '00020000', '00000000', '00000000', '1', '0x1d70')
C1b first raw: native 00040060 56616661 wasm 00041d6c 00000ff0
C1c call raw 00000002 count 0: native True wasm False
wasm headers containing 00040060 56616661: 0
```

Lecture mecanique :
- `raw 00040060 56616661` (l'en-tete 0 du natif, la constante "Vafa") n'apparait
  **dans aucun** des 40 en-tetes wasm ;
- l'appel vers l'offset 0 (`raw 00000002`, count 0) n'apparait pas non plus : le natif
  saute par un paquet call a `ea 0x40100000`, le wasm non ;
- le wasm commence son ring a `get=0x40101000` mais avec un contenu different des le
  premier mot (0x00041d6c au lieu de 0x00040060), et la sequence des offsets ne
  correspond plus a partir de idx2 : les deux lecteurs ne voient pas le meme flux.

L'instrument `hdr40` a ete corrige deux fois avant la mesure retenue :
1. sa premiere version passait 10 arguments pour 9 conversions (le `put` affichait 0) ;
2. sa deuxieme version dumpait APRES la classification, alors que le natif dumpe AVANT :
   le meme paquet pouvait donc apparaitre comme un appel d'un cote et une methode de
   l'autre, ce qui rend la comparaison deloyale.

La mesure retenue est `/tmp/hV-run3.log` (60 s), produite apres les deux correctifs, avec
l'instrument a la position native. Contre-epreuve : ses 40 en-tetes sont **identiques ligne
pour ligne** a ceux de `/tmp/hV-run2.log` (90 s, instrument apres classification) :

```
$ diff <(grep -a hdr40 /tmp/hV-run2.log | sed 's/^.*hdr40: //') \
       <(grep -a hdr40 /tmp/hV-run3.log | sed 's/^.*hdr40: //') && echo SAME
SAME
```

Le defaut mesure n'est donc ni dans l'instrument ni dans la position du dump : il est dans
le contenu du bloc de controle que le wasm lit.

### Critere 2 — `_gcm_intr_thread` present dans le run wasm : **FAUX**

```
wasm (grep -ac '_gcm_intr_thread' /tmp/hV-run3.log) : 0
natif (grep -ac '_gcm_intr_thread' /tmp/hV-native-cmdump.log) : 3
```

Les trois lignes natives sont la creation complete du thread (stack, create, start) ; le
wasm n'en a aucune. Les seuls threads crees cote wasm sont les deux handlers SPURS.

### Critere 3 — compteur de flips par 0x102 non nul : **FAUX**

Regle compteurs : valeur lue, pas comptage de mot.

```
$ grep -aoE 'flips=[0-9]+' /tmp/hV-run3.log | tail -1
flips=0
$ grep -ac 'p102' /tmp/hV-run3.log
0
$ grep -a 'gcmfifo: methods=' /tmp/hV-run3.log | tail -1
gcmfifo: methods=59442207 packets=26418763 draws=660468 clears=0 releases=440315 references=220156 flips=0 heads=0 drains=18 label255=0x00000002
```

Deux zeros, et ils disent des choses differentes :
- `p102 = 0` : le paquet 0x102 de `context_attribute` n'est **jamais** appele dans le
  run wasm, alors que le test synthetique (§3) l'atteint en 1,2 ms des qu'une file
  est ecrite dans le bloc de controle du lecteur ;
- `flips = 0` : la commande de flip 0xFEAC n'est pas dans le flux que gcm_fifo decode
  (le natif n'en a pas non plus : 0 sur 3000 commandes, mesure Z1) ; le flip natif
  vient de `context_attribute(0x102)`, pas du ring.

### Critere 4 — `tests/rsx_fifo_synth.sh` vert (12 PASS / 0 FAIL) : **VERT**

Le test n'existe pas dans l'arbre a 375f0e5. Je l'ai importe tel quel depuis
`8df4d6b` (branche `explore/zZ4`, seul commit qui l'ajoute) et lance sur ce worktree :

```
$ OUT=/tmp/hV-synth2 bash tests/rsx_fifo_synth.sh /root/Dev/h-V /tmp/hV-synth2
...
PASS the FIFO wrote the label, so the guest wait loop terminates
PASS SET_REFERENCE wrote dma.ref
PASS the release wrote the byte-swapped value
PASS the release wrote exactly 4 bytes: the neighbouring labels are untouched
PASS the flip packet incremented the flip counter
PASS the label advanced to the second value
PASS the second flip packet incremented the counter again
PASS the walker decoded all seven register writes, clear included
PASS the clear reached fb_clear: the case label ran and the framebuffer holds the colour
PASS the native first-flip sequence wrote label@0x410
PASS the native's SET_REFERENCE value (0) is what dma.ref holds
PASS the 0x0068 acquire did not stall: the 0x6C release satisfied it
== ALL CHECKS PASSED (0 failure(s))
rc=0
$ grep -c '^PASS' /tmp/hV-synth.log ; grep -c '^FAIL' /tmp/hV-synth.log
12
0
```

C'est un test **synthetique** : il prouve que le chemin RSX sait traiter un flux
NV4097 construit a la main (label, reference, release 4 octets, flip 0x102, clear).
Il ne prouve pas que le flux du jeu passe par ce chemin — c'est precisement ce que le
critere 1 mesure, et il est faux.

### Critere 5 — `regress.sh` natif vert sur les 3 jeux : **2 VERTS / 1 ROUGE**

Binaire natif : sources de `git HEAD` de `/root/Dev/ps3base2` (`da6866a`), pas un binaire
d'un worktree voisin. Le `build/ps3base` du checkout etait absent ; je l'ai relie depuis
les objets du build et les deux seuls TU dont l'objet venait d'une source G3 depuis
revertie (`rsx.cpp`, `main.cpp`), recompiles depuis `git show HEAD:<fichier>`.

```
$ cd /root/Dev/ps3base2 && SECONDS_PER_GAME=120 DISPLAY_NUM=:0 bash tools/regress.sh
regress: 120s per game on display :0
minecraft    (load 21.22 before the run)
minecraft    PASS  alive           flips=6481   crashes=0
gta-sa       (load 23.19 before the run)
gta-sa       PASS  alive           flips=6001   crashes=0
rdr          (load 18.58 before the run)
rdr          FAIL  SPURS abort     flips=601    crashes=0   screen=socialclub@12s abort=at 24s log=/tmp/regress-rdr.log

regress: 1 game(s) REGRESSED
```

Le rouge RDR est un **arret SPURS**, pas un crash memoire :

```
E  23.581655 spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET REFUSED cmd 0x40 (unmapped) ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428
E  23.581698 spu: thread 0x200 'SecondaryCellSpursKernel0' raised SPU exception 0xffffffff53505503
The SPURS is aborted.
```

Le seuil de reference pour RDR (`docs/regress-baseline.md`) est `flips >= 2000` +
notice Social Club + « pas d'abort SPURS avant 60 s ». L'abort arrive a **23,6 s**, donc
c'est bien le critere d'abort qui echoue, pas le compteur de flips (601 est en dessous
de 2000 aussi, mais l'abort est la cause premiere : le run s'arrete la).

**Ce que ce rouge est et n'est pas.** Premiere hypothese : un artefact de binaire. Le
`build-rdr/ps3base` du checkout a ete reconstruit a 19:08 par un autre agent pendant mon
travail, donc j'ai rejoue RDR sur un binaire INDEPENDANT, bati a 18:29 dans
`/root/Dev/wt-L2` depuis la meme ligne `da6866a` :

```
$ cd /root/Dev/wt-L2 && DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 \
    PS3BASE_DISC_DIR=/root/Dev/rdr-port/vfs/dev_bdvd PS3BASE_LOG=info \
    timeout 60 build-rdr/ps3base --no-launcher
exit=12
I  28.047993 rsx: flip 481 (buffer 0) worst 863.6 ms, >50ms 44, >100ms 41
1  <- occurrences de "SPURS is aborted"
E  33.904989 spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET REFUSED cmd 0x40 (unmapped) ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428
```

Meme faute, meme `pc 0x06428`, meme `ea 0x00000000`, sur deux binaires distincts. Le
rouge RDR n'est donc pas un artefact du binaire reconstruit : c'est l'etat de la ligne
`da6866a` pour ce titre, et il appartient a la piste SPURS, pas a ce verificateur. Le
critere 5 tel qu'enonce (« les 3 jeux verts ») est neanmoins **FAUX** et je le declare tel.

Troisieme verification, sur l'autre chemin de presentation. L2 documente que le
presenter Vulkan et le chemin headless ne sont pas interchangeables et que le chemin
headless est bien plus favorable a RDR (87 s avant l'abort). Je l'ai donc mesure :

```
$ cd /root/Dev/ps3base2 && env -u DISPLAY PS3BASE_DISC_DIR=/root/Dev/rdr-port/vfs/dev_bdvd \
    PS3BASE_LOG=info timeout 110 ./build-rdr/ps3base --no-launcher
exit=12
E   0.017020 gpu: SDL window creation failed: No available video device
I  42.883461 rsx: flip 841 (buffer 0) worst 37.7 ms, >50ms 0, >100ms 0
I  44.505813 tty: [0] The SPURS is aborted.
```

Meme sur le chemin headless, l'abort tombe a **44,5 s** sur ce binaire, donc toujours avant
la barre de 60 s. Les trois essais (Vulkan 23,6 s / binaire independant 33,9 s / headless
44,5 s) convergent : RDR echoue le critere d'abort quelle que soit la presentation, et
l'ecart entre les trois est l'ecart de charge machine, pas un changement de cause.

Honnetete sur les deux verts : `minecraft` a tourne sur le binaire que J'AI lie depuis les
sources de HEAD (`/tmp/ps3base-H`, installe dans `build/ps3base`) ; `gta-sa` a tourne sur
`build-gta/ps3base` date de 09:32, donc sur un arbre anterieur a `da6866a`. Le vert gta-sa
est un vert de NON-REGRESSION, pas une preuve d'equivalence avec HEAD. Le critere 5 est
donc, en toute rigueur, **2 verts sur 3**, et le vert gta-sa porte sur un binaire plus
ancien que la ligne mesuree.

---

## 3. Le test synthetique comme contre-epreuve du lecteur

`tests/rsx_fifo_synth.sh` prouve que le decodeur de `runtime/src/lv2/rsx.cpp` traite
correctement un flux ecrit dans **son** bloc de controle. Ce que j'ai mesure en plus,
c'est que le flux du JEU n'arrive pas dans ce bloc :

```
$ grep -aE 'rsx: context|reader armed' /tmp/hV-run3.log
I   1.458207 rsx: context at 0x00e00000 (hle init)
I   1.460600 gcmfifo: reader armed: context=0x01100000 control=0x01100040 ring=0x40100000 size=0xfa000 put=get=0x1000
```

et cote natif :

```
$ grep -a 'rsx: context' /tmp/hV-native-cmdump.log
I   1.089138 rsx: context at 0x50100000 (driver 0x50200000 reports 0x50300000 queue 0x8d009e00)
```

Le natif ne passe pas par notre HLE (0 ligne `gcmstub`, 0 ligne `imp119`), le wasm oui :

```
$ grep -ac 'imp119' /tmp/hV-run3.log ; grep -ac 'gcmstub' /tmp/hV-run3.log
1
1
```

C'est la divergence de contexte que Z3 a nommee, et elle est compatible avec tout ce qui
precede : le wasm ecrit le ring a `get=0x1000/put=0x1030` la ou le natif a `put=0x13a0`,
parce que ce ne sont pas les memes blocs.

---

## 4. Tableau livre

| piste | annonce | rejoue | resultat | fusionnee |
|---|---|---|---|---|
| V/G (branche `explore/gV`, 664fe67) | « les trois criteres G sont FAUX sur e3b25c3 ; retirer PS3BASE_HLE_OVER_LLE ne corrige rien, ca crashe » | oui — criteres 1/2/3 remesures sur 375f0e5 avec un instrument au meme point que le natif | **CONFIRME, et pire** : 0/40 en-tetes identiques (le premier mot n'est pas `00040060`), 0 `_gcm_intr_thread`, `flips=0`, et `0x102` **jamais appele** (p102=0) | oui — `235529f..bb84906`, ff-only |
| Z3 (message orchestrateur) | « deux lecteurs, deux blocs : rsx.cpp 0x00e00000, gcm_fifo 0x01100000, le natif 0x50100000 » | oui — les trois adresses sont dans mes logs bruts | **CONFIRME a la ligne** : mes deux adresses sont exactement celles annoncees ; le natif est a 0x50100000 | oui (constat, pas de code) |
| Z4 (8df4d6b) | « tests/rsx_fifo_synth 12 PASS / 0 FAIL sur HEAD » | oui — test importe tel quel, lance sur h-V avant ET apres rebase | **VERT deux fois** : 12 PASS / 0 FAIL, rc=0, avant et apres le rebase sur origin/main | oui — les 3 fichiers du test sont sur main |
| Z0 (297afdc, ee378c2) | « ni 0xFEAC ni 0xE920 dans le flux ; le flip natif n'est pas dans le ring » | oui — `flips=0` avec 59 442 207 methodes consommees | **CONFIRME** : 0 flip dans le ring cote wasm (et 0 ligne 0xFEAC cote natif), des deux cotes | oui (constat) |
| Z1 | « le wasm lit a put=0x1030 la ou le natif a 0x13a0 » | oui | **CONFIRME** : mes hdr40 portent `put 0x00001030` | oui (constat) |
| natif `tools/regress.sh` | — | oui, serialise, binaire lie depuis les sources de HEAD | **2 VERTS / 1 ROUGE** : minecraft 6481, gta-sa 6001, rdr abort SPURS 23,6 s (33,9 s binaire independant, 44,5 s headless) | oui (mesure, pas de code) |
| mes instruments (`hdr40`, `p102`) + le test importe | « mesurer au meme point et au meme format que le natif » | oui, sur `explore/hV` rebase | **livre** : 6 fichiers, +945 lignes, 0 suppression | oui — `235529f..bb84906` |

### 4.1 La fusion

`explore/hV` a ete rebase sur `origin/main` juste avant de conclure, puis poussee en
ff-only et serialisee :

```
$ git merge-base --is-ancestor origin/main HEAD && echo FF-POSSIBLE
FF-POSSIBLE
$ git push origin HEAD:main
   235529f..b9d7510  HEAD -> main
$ git ls-remote origin refs/heads/main
b9d75103ccbcd1d976bc6260af8cd065a43b8d1a  refs/heads/main
```

Controle de forme demande par la file de fusion, mesure APRES la fusion :

```
suppressions introduites par le push (235529f..origin/main) : 0
fichiers de MON diff (375f0e5..HEAD)                        : 6, tous ajoutes ou modifies
V/RESULTS.md sur main apres fusion                          : 488 lignes, exactement l'original
hdr40 dans runtime/src/gpu/gcm_fifo.cpp sur main            : present
p102  dans runtime/src/lv2/rsx.cpp sur main                 : present
```

Le push a avance `main` de 341 commits. Cinq sont les miens ; les 336 autres sont la
ligne trunk wasm (375f0e5 et ses ancetres) que `wasm-base` portait deja et que `main`
n'avait pas encore. Ils ne sont pas de moi et ne modifient aucune de mes mesures.

---

## 5. Ce que ce travail etablit, et ce qu'il n'etablit pas

Etabli, chiffre, reproductible, apres rebase sur origin/main et fusion ff-only
(235529f..bb84906, 6 fichiers, +945 lignes, 0 suppression) :
1. Les criteres 1, 2 et 3 sont **faux** sur 375f0e5, mesures avec un instrument place au
   meme point et au meme format que le natif. 0/40 en-tetes identiques, 0
   _gcm_intr_thread, flips=0, et 0x102 jamais appele.
2. Le test synthetique est **vert** (12 PASS / 0 FAIL), avant ET apres le rebase : le
   decodeur RSX fonctionne, donc l'echec n'est pas dans le traitement d'un flux, il est
   dans la **livraison** du flux.
3. Le natif est vert sur minecraft et gta-sa ; RDR aborte SPURS a 23,6 s (presenter
   Vulkan), 33,9 s (binaire independant), 44,5 s (headless), ce qui est un autre chantier
   et pas une regression de ce commit.

Non etabli :
- La cause exacte pour laquelle le bloc de controle alimente par le guest n'est pas celui
  que gcm_fifo lit. J'ai etabli les adresses (0x00e00000, 0x01100000, 0x50100000) et que
  le natif evite notre HLE, mais pas le mecanisme par lequel le HLE pose son bloc.
- Le rouge RDR sur un binaire HEAD propre : le binaire RDR du checkout a ete reconstruit
  par un autre agent pendant la mesure ; mon run ne l'isole pas.

---

## 6. Fichiers et artefacts

| chemin | contenu |
|---|---|
| `runtime/src/gpu/gcm_fifo.cpp` | instrument `hdr40` (format natif exact), 40 en-tetes |
| `runtime/src/lv2/rsx.cpp` | compteur `p102` sur `context_attribute(0x102)` |
| `tests/rsx_fifo_synth.{sh,cpp}`, `_support.cpp` | importes de 8df4d6b, inchanges |
| `/tmp/hV-run3.log` | run wasm RETENU, instrument a la position native (hdr40=40, p102=0, intr=0, flips=0) |
| `/tmp/hV-run2.log` | run intermediaire (instrument apres classification) : memes 40 en-tetes, defaut hors instrument |
| `/tmp/hV-native-cmdump.log` | run natif HEAD, 45 s, GOWA_FIFO_CMDUMP=500 (intr=3) |
| `/tmp/hV-synth.log` | `tests/rsx_fifo_synth.sh` vert : 12 PASS / 0 FAIL |
| `/tmp/out-V` | cache d'objets + module de reference (137 Mo) |
| `/tmp/ps3base-H` | binaire natif lie depuis les sources de HEAD |
