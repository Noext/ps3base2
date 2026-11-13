# S1 — la page draine l'anneau : ce qui est mesure

Worktree `/root/Dev/s-S1` (branche `explore/sS1`), base `083bdbe` + fusion de
`explore/rR3` (producteur d'anneau, merge `b94ad19`). Arbres partages lies :
`bin out out-mc fw ref`. Ce fichier est ecrit AVANT les mesures de build.

## 0. Provenance des binaires utilises dans ce document

    $ cd /tmp/out-R3/web && md5sum game.wasm | cut -c1-16
    (voir section 6, collee avec la sortie brute)
    $ ls -la /tmp/out-R3/web/game.wasm /tmp/out-R3/web/game.js /tmp/out-R3/web/game.data

Le module utilise pour les mesures de la section 2 est celui construit par la piste R3
(`/tmp/out-R3/web`, 45 676 153 o de wasm). Il porte le producteur d'anneau H4, le renderer
lie via `vk_null.cpp`, et `emdawnwebgpu`.

## 1. Antecedents (REGLE 12 — grep fait AVANT d'ecrire)

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Aucun consommateur d'anneau cote page n'existe, seulement un compteur | `docs/explore/R3/RESULTS.md` sec. 5.4 | **oui** : c'est le chainon a ecrire |
| Le producteur classifie 7 types de records et les pousse dans `CommandRing` | `runtime/include/gpu/rsx_ring.h:40-49` | **oui** : c'est le contrat de lecture |
| Le producteur refuse (compte et jette) quand l'anneau est plein | `runtime/include/gpu/rsx_ring.h` (Back-pressure) | **oui** : `refused` est le compteur du symptome |
| H4 a deja un consommateur JS qui LIT mais ne dessine pas | `explore/iH4 web/h4-ring.js` | **oui** : la lecture existe, le replay manque |
| Le device est en page et le wasm ne peut pas le prendre (transfert) | `docs/explore/R3/RESULTS.md` sec. 2.2 | **NON — refute en section 3** |
| WebGPU est disponible dans un Worker dedie | probe S1 p4 (section 3) | **oui** : ouvre une architecture plus simple |
| `-sPROXY_TO_PTHREAD=1` met `main()` sur un pthread | `tools/build_wasm.sh:88` | **oui** : c'est ce qui separe les realms |

## 2. L'anneau est PLEIN et personne ne le vide — mesure

Le module R3 lance dans un Worker (page servie par `web/serve.py`, COOP/COEP). Le worker
principal JS cree un device WebGPU puis lit les compteurs de l'anneau par les exports
`mod._rsx_ring_*`. Commande exacte :

    $ cd /tmp/S1-probe/real && python3 serve.py 8971 &
    $ cd /root/Dev/wasm-base && node tools/x-capture.mjs \
        --url http://127.0.0.1:8971/index.html \
        --out /tmp/S1-probe/shots-real --seconds 80 --interval 20 --no-compositor

Sortie brute (extraits de `/tmp/S1-probe/shots-real/capture.log`) :

    [1.24s] page console.log w: WORKER-MAIN: device OK
    [1.99s] page console.log w: WORKER-MAIN: module booted; exports present=
            _rsx_ring_address:function,_rsx_ring_capacity:function,
            _rsx_ring_word:function,_rsx_ring_stat:function
    [3.03s] page console.log w: I   1.036925 [   10] rsxring: first method through the seam: 0x0060 arg=0x56616661 io=0x00001000
    [2.99s] page console.log RING {"addr":8535616,"cap":16777216,"recs":0,"draws":0,"clears":0,"flips":0,"refused":0,"used":0,"popped":0,"ticks":1}
    [3.99s] page console.log RING {"addr":8535616,"cap":16777216,"recs":20,"draws":0,"clears":1,"flips":0,"refused":0,"used":3436,"popped":0,"ticks":2}
    [17.99s] page console.log RING {"addr":8535616,"cap":16777216,"recs":923,"draws":26,"clears":53,"flips":13,"refused":1995,"used":16777200,"popped":0,"ticks":16}
    [80.79s] page console.log w: I  78.829850 [   10] rsx: flip 1381 (buffer 0) worst 115.6 ms, >50ms 15, >100ms 1

Lecture, et c'est le defaut nomme chiffre :

| grandeur | valeur | consequence |
|---|---|---|
| `recs` (enregistrements pousses) | **923** | l'anneau se remplit bien |
| `draws` / `clears` / `flips` | **26 / 53 / 13** | le chemin de rendu du guest s'execute |
| `used` / `cap` | **16 777 200 / 16 777 216** | l'anneau est plein a **16 octets pres** |
| `popped` | **0** | **aucun consommateur ne lit** |
| `refused` | **1995** | le producteur jette, donc des commandes sont perdues |
| `flip` du renderer | **1381** a t=78 s | le renderer lie et flippe, mais ne peint rien |

**`used = cap - 16` et `popped = 0` ensemble sont la mesure directe du chainon manquant** :
l'anneau est un tuyau dont le bout consommateur n'est pas raccorde. Le `refused=1995` de ce run
(et non 1 160 458 comme dans R-final) vient de ce que ce run mesure sur 80 s avec un device
present ; le compteur monte avec la duree, il ne change pas de nature.

## 3. Ce que R3 avait conclu faux : le device PEUT etre pris par le wasm

R3 (sec. 2.2) conclut « un `GPUDevice` n'est pas transferable dans un worker », donc « le device
vit dans la PAGE ». La mesure ci-dessous montre que le transfert entre realms n'est pas la seule
voie : **dans le meme realm, le wasm ADOPTE le device par `preinitializedWebGPUDevice`**, et le
chemin complet (texture, passe, submit) fonctionne. Sonde minimale, 3 fichiers, dans
`/tmp/S1-probe/` :

    $ cd /tmp/S1-probe && /root/Dev/ps2-web-recomp/tools/emsdk/upstream/emscripten/em++ \
        p.cpp -o p.js --use-port=emdawnwebgpu --js-library=bridge.js \
        -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createP \
        -sEXPORTED_FUNCTIONS=_probe_adopt,_probe_clear,_probe_status -O1
    $ node tools/x-capture.mjs --url http://127.0.0.1:8899/index.html \
        --out /tmp/S1-probe/shots --seconds 8 --interval 2 --no-compositor

    [1.30s] page console.log device OK, format rgba8unorm
    [1.32s] page console.log c: P: ADOPTED device=0x11438 queue=0x11418
    [1.33s] page console.log c: P: canvas texture ptr=0x11488
    [1.33s] page console.log c: P: CLEAR SUBMITTED 1280x720 rgb(30,200,120) errors=0
    [1.33s] page console.log clear=1 status=110
    [2.24s] PAGE-CANVAS /tmp/S1-probe/shots/page-canvas-000-0.png canvas=c |
            size=1280x720 mean=0.457516 meanA=1 centre=srgba(30,200,120,1) colours=1

Le canvas porte **exactement** la couleur demandee (`srgba(30,200,120,1)`), ce qui prouve que le
wasm a ecrit dans la texture de canvas de la page. Ce n'est pas une lecture de retour : c'est le
canvas lui-meme.

## 4. Le vrai mur, mesure : un pthread n'a pas l'etat WebGPU

Le renderer RSX du guest tourne sur le thread 10, qui est un **pthread Emscripten** (visible dans
toutes les lignes `[   10] rsx:` ci-dessus). La sonde p2 mesure si ce realm voit le device :

    [1.14s] page console.log c: P: adopted on main realm=0 dev=0x24f80
    [1.15s] page console.log c: P: clear ok realm=0 rgb(30,200,120)
    [1.15s] page console.log c: P: pthread start realm=1
    [1.15s] page console.log c: P: pthread clear rc=-2

`realm=1` n'arrive meme pas a configurer le canvas (`rc=-2`). La sonde p3 isole la cause :

    [1.27s] page console.log c! worker sent an error! p3.js:380: Uncaught TypeError:
            Cannot read properties of undefined (reading 'createCommandEncoder')

Le port `emdawnwebgpu` garde son etat dans `WebGPU.Internals`, un global **par realm**. Un pthread
a le sien, vide. C'est **le** mur, et il explique pourquoi le chemin « le vrai renderer dessine »
n'est pas un simple cablage.

La sonde p5 verifie la seule echappatoire : proxifier les entrees JS par `__proxy: 'sync'`.

    [1.19s] page console.log c: P5: pthread queue=0x24f50
    [1.19s] page console.log c: P5: canvas tex ptr=0x378b0 (from proxied main)
    [1.19s] page console.log c! worker sent an error! p5.js:380: Uncaught TypeError:
            Cannot read properties of undefined (reading 'createCommandEncoder')

Le proxy fonctionne pour MES fonctions (le canvas a bien ete configure, le pointeur de texture
vient du main), **mais pas pour les fonctions du port** : `wgpuDeviceCreateCommandEncoder` est
l'une des siennes et elle s'execute toujours dans le realm appelant. Il faudrait annoter ~46
fonctions de `library_webgpu.js` ; ce n'est pas fait ici et je ne l'annonce pas comme fait.

## 5. La piste qui marche sans anneau : un seul realm, dans un Worker

La sonde p4 met tout dans un seul realm : le Worker cree le device et un `OffscreenCanvas`, le
wasm (sans `PROXY_TO_PTHREAD`) les adopte et peint.

    [0.55s] page console.log w: W: worker realm gpu=true offscreen=true
    [1.48s] page console.log w: W: device OK in worker realm
    [1.49s] page console.log w: c: P4: main on worker main thread
    [1.49s] page console.log w: c: P4: adopted device (main thread, no proxy)
    [1.49s] page console.log w: c: P4: CLEAR SUBMITTED from main()
    [1.50s] page console.log w: c: P4: RESULT clear=1

WebGPU **est** disponible dans un Worker. Ceci ouvre une architecture sans anneau — mais elle ne
suffit pas non plus : le renderer du guest tourne sur un pthread (section 4), pas sur le thread
principal du worker.

## 6. Le backend natif pixel-exact compile pour le web

`runtime/src/gpu/wgpu_backend.cpp` (2 089 l., RMS 0.0000 contre Vulkan) inclut `<webgpu/wgpu.h>`,
le header de wgpu-native, absent du port. Un shim de 20 lignes
(`/tmp/S1-probe/shim/webgpu/wgpu.h`) redirige vers `<webgpu/webgpu.h>` du port.

    $ cd /root/Dev/r-R2 && em++ -std=c++20 -c runtime/src/gpu/wgpu_backend.cpp \
        -o /tmp/S1-probe/wb.o --use-port=emdawnwebgpu -I /tmp/S1-probe/shim \
        -I runtime/include -I runtime/src -w
    $ echo $?
    0

**0 erreur.** Sur les 46 symboles `wgpu*` que ce fichier appelle, **un seul** manque au port :

    $ for s in $(cat /tmp/wgpu-syms.txt); do grep -q "\b$s\b" .../webgpu.h || echo "MISSING $s"; done
    MISSING wgpuDevicePoll

`wgpuInstanceProcessEvents` existe (header ligne 2734) et le port pompe ses callbacks lui-meme.
La sonde p1 a d'ailleurs peint sans aucun poll. Donc le cout de portage du backend est **1 symbole
et 1 shim**, pas une reecriture.

## 7. Ce qui n'est PAS fait

1. **Le logo n'est pas peint.** Le critere de l'enonce n'est pas atteint. La cause est nommee
   section 4 : le renderer tourne sur un pthread dont le realm n'a pas l'etat WebGPU du port.
2. Aucun consommateur d'anneau n'est ecrit : `popped` reste a 0 (section 2).
3. Le producteur ne transporte pas le WGSL : `rsx_ring.h` porte un hash d'ucode, pas la source.
   Un consommateur JS devrait donc soit refaire le decodage RSX, soit recevoir le WGSL par un
   nouveau type de record (le renderer l'a deja dans `e.wgsl` sur le chemin web).
4. Le proxy des ~46 fonctions du port n'est pas fait (section 4).

## 8. Les trois chemins qui restent, par ordre de cout

| chemin | ce qu'il faut | cout mesure |
|---|---|---|
| A. renderer reel sur pthread | annoter ~46 fonctions de `library_webgpu.js` en `__proxy:'sync'` + module WGSL au lieu de SPIR-V | 1 copie locale du port + relink |
| B. consommateur JS de l'anneau | nouveau record WGSL + reconstruction des uniforms en JS | gros : le decode RSX est dans `rsx_renderer.cpp` |
| C. un realm sans pthread | deplacer les threads du guest hors des pthreads | hors budget |
