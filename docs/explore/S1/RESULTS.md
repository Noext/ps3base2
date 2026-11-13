# S1 — la page draine l'anneau : arret sur demande, etat mesure

**Arrete sur instruction de l'utilisateur (via le lead), a la demande "ne relance rien".**
Aucun processus de jeu n'est laisse en vie : mes trois serveurs de page (ports 8899, 8971,
8988) ont ete tues ; je n'ai jamais lance le binaire natif dans cette piste et il n'y en a
aucun a moi.

Worktree `/root/Dev/s-S1` (branche `explore/sS1`). Base `083bdbe`, plus la fusion de
`explore/rR3` (producteur d'anneau) et de `explore/rR2` (backend WebGPU pixel-exact).
Arbres partages lies : `bin out out-mc fw ref`.

---

## 1. PROVENANCE — les trois elements exiges

    $ md5sum /tmp/out-S1/game.wasm | cut -c1-16
    12850bede632b680
    $ ls -la /tmp/out-S1/game.wasm /tmp/out-S1/game.js /tmp/out-S1/game.data
    -rwxr-xr-x 45463954  /tmp/out-S1/game.wasm
    -rw-r--r--   170437  /tmp/out-S1/game.js
    -rw-r--r-- 158483841 /tmp/out-S1/game.data

Commande de build (complete) :

    $ cd /root/Dev/s-S1
    $ DISK=/tmp/disk-r4 bash tools/build_wasm_s1.sh /root/Dev/s-S1 /tmp/out-S1
    (compile 209 objets, puis link ; le link doit etre em++ car le port l'exige)

Commande de relink seule (celle qui a produit le binaire ci-dessus) :

    $ cd /root/Dev/s-S1 && DISK=/tmp/disk-r4 bash tools/relink_s1.sh /root/Dev/s-S1 /tmp/out-S1

Commande de capture exacte :

    $ cd /tmp/out-S1/web && python3 serve.py 8988 &
    $ cd /root/Dev/wasm-base && node tools/x-capture.mjs \
        --url http://127.0.0.1:8988/game.html \
        --out /tmp/out-S1/shots7 --seconds 80 --interval 40 \
        --auto-hook docs/explore/X6/x6-inpage-hook.js

PNG produits : `/tmp/out-S1/shots{,2,3,4,5,6,7}/page-canvas-*.png` et
`compositor-*.png`. Tous les `page-canvas` sont **vides** (`mean=0, meanA=0`,
1 couleur) : le critere de l'enonce **n'est pas atteint**.

---

## 2. CE QUI EST MESURE, ET QUI COMPTE

### 2.1 Le chainon manquant, mesure avec ses chiffres

Le module R3 lance dans un Worker ; le thread JS principal cree un device WebGPU et lit les
compteurs de l'anneau par les exports `mod._rsx_ring_*` (node x-capture, port 8971) :

    RING {"addr":8535616,"cap":16777216,"recs":923,"draws":26,"clears":53,"flips":13,
          "refused":1995,"used":16777200,"popped":0,"ticks":16}
    I  1.036925 [   10] rsxring: first method through the seam: 0x0060 arg=0x56616661 io=0x00001000
    I 78.829850 [   10] rsx: flip 1381 (buffer 0) worst 115.6 ms, >50ms 15, >100ms 1

| grandeur | valeur | lecture |
|---|---|---|
| `used` / `cap` | **16 777 200 / 16 777 216** | anneau plein a **16 octets pres** |
| `popped` | **0** | **aucun consommateur** |
| `recs` | **923** | le producteur pousse |
| `refused` | **1995** | le producteur jette : des commandes sont perdues |

`used = cap - 16` avec `popped = 0` est la mesure directe du chainon manquant.

### 2.2 R3 s'etait trompe sur le lieu du device — corrige par la mesure

R3 (sec. 2.2) : « un GPUDevice n'est pas transferable dans un worker », donc « le device vit
dans la PAGE ». Vrai du TRANSFERT. Faux de l'ADOPTION : dans le meme realm, le wasm prend le
device par `preinitializedWebGPUDevice`. Sonde minimale `/tmp/S1-probe/p.cpp` :

    c: P: ADOPTED device=0x11438 queue=0x11418
    c: P: canvas texture ptr=0x11488
    c: P: CLEAR SUBMITTED 1280x720 rgb(30,200,120) errors=0
    PAGE-CANVAS .../page-canvas-000-0.png | 1280x720 centre=srgba(30,200,120,1) colours=1

Le canvas porte **exactement** la couleur demandee : le wasm a ecrit dans la texture de canvas.

### 2.3 Le vrai mur : un pthread n'a pas l'etat WebGPU du port

Le renderer tourne sur le thread 10, un **pthread** (toute ligne `[   10] rsx:`). Sondes
`p2`/`p3`/`p5` :

    c: P: adopted on main realm=0 dev=0x24f80
    c: P: clear ok realm=0 rgb(30,200,120)
    c: P: pthread start realm=1
    c: P: pthread clear rc=-2
    c: P: pthread queue=0x24f50
    c: p3.js:380 Uncaught TypeError: Cannot read properties of undefined (reading 'createCommandEncoder')

`emdawnwebgpu` garde son etat dans `WebGPU.Internals`, un global **par realm**; celui d'un
pthread est vide. C'est le mur.

### 2.4 La breche, mesuree : proxy synchrone

En annotant les entrees du port en `__proxy: 'sync'`, un pthread peint le canvas :

    c: P5: adopted on main
    c: P5: pthread queue=0x24f50
    c: P5: canvas tex ptr=0x378b0 (from proxied main)
    c: P5: pthread encoder=0x378c8
    c: P5: PTHREAD CLEAR SUBMITTED
    c: P5: pthread rc=1
    PAGE-CANVAS .../page-canvas-000-0.png | 1280x720 centre=srgba(25,191,89,1) colours=1

`0.10/0.75/0.35` -> `srgba(25,191,89,1)` : exact. 79 entrees annotees dans le port
vendore (`third_party/emdawnwebgpu/`, 14 Ko).

### 2.5 Le backend pixel-exact COMPILE pour le wasm, avec 1 symbole manquant

`runtime/src/gpu/wgpu_backend.cpp` (2 089 l., RMS 0.0000 contre Vulkan) inclut
`<webgpu/wgpu.h>`, absent du port. Sur les 46 symboles `wgpu*` qu'il appelle :

    $ for s in $(cat /tmp/wgpu-syms.txt); do grep -q "\b$s\b" .../webgpu.h || echo "MISSING $s"; done
    MISSING wgpuDevicePoll
    $ em++ -std=c++20 -c runtime/src/gpu/wgpu_backend.cpp --use-port=... -I shim -w
    $ echo $?
    0

**1 symbole et 1 shim de 20 lignes**, pas une reecriture.

### 2.6 Le module S1 : le renderer reel TOURNE dans Chromium, et le backend adopte le device

Run `/tmp/out-S1/shots4` (md5 `2a3b7d12ba3e1dab`, un binaire anterieur a la correction) :

    I   1.545050 [    2] rsx: local memory 0xf900000 bytes at 0x58000000
    I  17.585055 [   10] rsx: new color surface at local 0x280000 1280x480 format 44 pitch 0x1400
    I  17.593610 [   10] rsx: vertex program fd4be450bbeda1e5 start 9: queued
    s1: device adopted on the owning realm (handle 2625294792)
    wgpu: web: adopted device 0x9c7ad1c8 queue 0x9bc2be80 colourFormat 22
    wgpu: clear 1..4 mask 0x3 ...
    I  80.700555 [   10] rsx: flip 1141 (buffer 0)

`fd4be450bbeda1e5` est **le hash du programme de vertex du logo** mesure par K2/R2. Le
renderer lie, decode, et le backend WebGPU tient un device dans Chromium.

### 2.7 Le defaut exact qui reste, nomme par les compteurs du renderer

Run `/tmp/out-S1/shots7` (binaire md5 `12850bede632b680`) :

    S1STATS draws=0 skipped=98064 n_method=1184451 skip=[3284:83966 3287:14098]
             recs=923 ring_draws=26

- `n_method=1 184 451` : le decodeur consomme bien le flux RSX.
- `draws=0` : aucun draw n'atteint une pipeline.
- `skip=[3284:83966 3287:14098]` : **tout** tombe sur deux lignes de
  `rsx_renderer.cpp`, `if (!vp || !vp->ready || !vp->ok)` (3284) et l'equivalent fragment
  (3287). Les programmes ne sont pas marques « ok ».

Cause trouvee, et corrigee dans l'arbre mais **pas encore mesuree** : `finish_vp`/`finish_fp`
sont appeles par le worker de compilation **asynchrone** et ils ASSIGNENT `e.ok`. L'emission
WGSL faite au site d'appel (dans `vertex_program()`) etait donc ecrasee par
`e.ok = compile_glsl(...)` = false. Correction : l'emission WGSL se fait desormais AVANT
`compile_entry` (pour que l'entree la porte deja), et `finish_*` honore `e.wgsl`.

Etat au moment de l'arret : la correction est ecrite dans
`runtime/src/gpu/rsx_renderer.cpp` mais la recompilation a echoue sur une variable hors
portee (`vp` referencee dans le bloc async du chemin vertex, ligne 938), et **le binaire
`12850bede632b680` ne contient donc PAS le correctif**. C'est exactement pourquoi le dernier
run mesure encore `draws=0`.

---

## 3. CE QUI N'EST PAS FAIT

1. **Le logo n'est pas peint.** Le critere de l'enonce (1280x720 ou 1280x480, des centaines de
   couleurs, structure comparable a la capture native) n'est **pas** atteint. Tous les
   `page-canvas-*.png` sont vides.
2. La correction de l'ordre d'emission WGSL n'a jamais ete compilee ni mesuree (erreur de
   compilation non corrigee, ligne 938).
3. Aucun consommateur d'anneau cote page n'est ecrit : `popped` reste a 0. La strategie
   retenue n'en a plus besoin (le renderer dessine directement), mais ce n'est pas mesure
   jusqu'au bout.
4. Le producteur ne transporte pas le WGSL (`rsx_ring.h` porte un hash d'ucode). Sans objet
   pour la strategie retenue.

## 4. Antecedents (REGLE 12)

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Aucun consommateur d'anneau cote page, seulement un compteur | `docs/explore/R3/RESULTS.md` sec. 5.4 | **oui** : c'etait le chainon a ecrire |
| 7 types de records, back-pressure par refus compte | `runtime/include/gpu/rsx_ring.h:40-49` | **oui** : contrat de lecture |
| Le device ne peut pas etre TRANSFERE | `docs/explore/R3/RESULTS.md` sec. 2.2 | **non, refute** : l'adoption marche (sec. 2.2 ici) |
| WebGPU dispo dans un Worker dedie | sonde p4 | **oui**, mais insuffisant (renderer sur pthread) |
| `-sPROXY_TO_PTHREAD=1` met main() sur un pthread | `tools/build_wasm.sh:88` | **oui** : separe les realms |
| Le backend natif est pixel-exact (RMS 0.0000) | `docs/explore/R-FINAL.md` | **oui** : c'est lui qu'on porte, pas un reimplemente |

## 5. Les fichiers produits, et ou ils sont

| fichier | contenu |
|---|---|
| `tools/build_wasm_s1.sh` | build complet S1 (renderer + port + bridge + STACK 8 Mo) |
| `tools/relink_s1.sh` | relink seul (~1 min au lieu de ~40) |
| `web/s1-bridge.js` | pont canvas/device, toutes entrees `__proxy:'sync'` |
| `web/game.html` | page : cree le device, l'offre au module, lit les compteurs |
| `web/worker.js` | worker (variante device-transfere, non retenue) |
| `third_party/emdawnwebgpu/` | le port vendore, 79 entrees annotees |
| `runtime/include/gpu/wgpu_web_shim.h` | le shim du seul symbole manquant |
| `runtime/src/gpu/wgpu_backend.cpp` | backend R2 + branche web (adoption, canvas, WGSL) |

## 6. La prochaine action, si la piste reprend

Une seule, et elle est nommee : corriger la ligne 938 de `rsx_renderer.cpp` (la reference a
`vp` dans le bloc async du chemin vertex), recompiler, relinker, recapturer. Le reste de la
chaine est mesure : device adopte, renderer lie, anneau alimente, compteurs de skip qui
designent exactement les deux portes a franchir.
