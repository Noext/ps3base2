# T1 — Rejeu sur le thread principal

Worktree `/root/Dev/t-T1` (branche `explore/tT1`), base `d41c834`.
Arbres partages lies : `bin out out-mc fw ref`. Out dir unique : `/tmp/out-T1`.

**Ce fichier est ecrit AVANT toute mesure.** Les sections 5 a 7 sont remplies au fur et a mesure,
avec des commandes et des sorties brutes, jamais une valeur decrite de memoire.

---

## 0. Antecedents (REGLE 12 — grep fait AVANT d'ecrire)

```
$ cd /root/Dev/t-T1 && grep -rln 'rsx_ring_stat' --include=*.md --include=*.h --include=*.cpp .
(0 ligne sur la base d41c834 : ni le producteur d'anneau ni le backend WebGPU ne sont dans la base)
```

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le renderer tourne sur un pthread, et un pthread a un `WebGPU.Internals` vide | `docs/explore/S1/RESULTS.md` sec. 2.3 | **oui** |
| Le wasm ADOPTE un device cree par JS (`preinitializedWebGPUDevice`) | `docs/explore/S1/RESULTS.md` sec. 2.2 | **oui** |
| Un anneau sans consommateur : `used=cap-16`, `popped=0` | `docs/explore/S1/RESULTS.md` sec. 2.1 | **oui** |
| Producteur d'anneau + dedup, `refused=0` mesure | `docs/explore/S2/RESULTS.md` | **oui** |
| `-sPROXY_TO_PTHREAD=1` met `main()` sur un pthread | `tools/build_wasm.sh:88` | **oui** |

---

## 1. PROVENANCE — les elements exiges

| element | valeur |
|---|---|
| md5 game.wasm (build courant) | `d4757a555c08696d` |
| chemin | `/tmp/out-T1/game.wasm` |
| commande de build | `DISK=/tmp/disk-r4 bash tools/build_wasm_s1.sh /root/Dev/t-T1 /tmp/out-T1` |
| commande de relink | `DISK=/tmp/disk-r4 bash tools/relink_s1.sh /root/Dev/t-T1 /tmp/out-T1` |
| commande de run | `flock -w 120 /tmp/game.lock timeout 150 node tools/x-capture.mjs --url http://127.0.0.1:8991/game.html --out DIR --seconds 90 --interval 20` |
| serveur | `cd /tmp/out-T1/web && python3 serve.py 8991` (COOP/COEP) |

---

## 2. Ce qui a ete franchi, avec la mesure

### 2.1 Le build S1 ne compilait pas — corrige

`rsx_renderer.cpp:938` nommait `vp.glsl` dans la branche async, ou `vp` n'existe pas encore
(declare ligne 946, dans la lambda du worker). C'est exactement l'erreur que S1 RESULTS sec. 2.7
nommait comme dernier blocage. Corrige : le diagnostic porte une chaine vide et le hash.
**Mesure** : `emcc -c rsx_renderer.cpp` -> exit 0.

### 2.2 Le link exige em++ — corrige par relink_s1.sh

`emcc: error: emdawnwebgpu requires C++. Link using em++`. `tools/relink_s1.sh` utilise deja
`em++`. Build complet : 209 objets, module 45 723 973 o.

### 2.3 L'anneau ne pouvait pas s'allouer — corrige (cause racine mesuree)

`vkMapMemory` (stub wasm, `vk_null.cpp`) fait un `calloc` de la taille demandee. `ring.cpp`
demandait `RING_SIZE * RING_SLOTS` = **256 Mo x 2 = 512 Mo**, alors que le buffer plat invite
(`mem::reserve`, bitset) prend **2432 Mo** et que le module est lie avec
`-sMAXIMUM_MEMORY=3072 Mo`. Le calloc echouait, le stub retournait `VK_SUCCESS` avec
`*ppData = null`, `ring_alloc` calculait `out.ptr = null + offset` — une adresse wasm BASSE et
donc VALIDE — et `decode_texture` ecrivait les textures decodees par-dessus les donnees du module.

**Mesure avant** : `memory access out of bounds` a t=13.47 s, juste apres le premier `texloc`
(1280x720, fmt 0x000ba529, dec_size 0x4aff90).

Corrections :
- `vk_null.cpp` : `vkMapMemory` retourne `VK_ERROR_OUT_OF_DEVICE_MEMORY` et loggue au lieu de
  mentir (regle « never a fake success »).
- `ring.cpp` : `RING_SIZE` = 64 Mo sous `__EMSCRIPTEN__` (128 Mo au total, tient dans les
  640 Mo restants), 256 Mo sinon. Le resultat de `vkMapMemory` est verifie et `g_ptr` nul est un
  refus.

**Mesure apres** : `texstep: D ring_alloc ok ptr 0xaa26e068`, `E decode ok`, `F`, `G` — la chaine
texture va au bout, **0 crash**.

### 2.4 `frame_serial()` constant bloquait l'anneau — corrige

`gpu_wasm_support.cpp:132` retournait `0` en dur. `ring.cpp` fait tourner ses deux slots sur un
changement de ce serial : la branche ne se declenchait jamais, `g_head` ne faisait que croitre.

**Mesure** : 46 528 `ring buffer exhausted in one frame (7488 bytes requested)` — un anneau de
64 Mo refusant 7 Ko — et tous les draws sautes a `rsx_renderer.cpp:3509`.

Correction : l'anneau a son **propre** compteur (`ring_frame_advance()` / `ring_frame()`), avance
par `present()`. `frame_serial()` reste constant : le faire avancer a ete essaye et **annule**,
parce que le renderer s'en sert aussi pour la retraite des surfaces (1428, 1455), le `last_used`
des textures (2179, 2201) et l'index du writeback (4645) ; l'avancer a fige la FIFO
(`busy 99.8%`, `methods 0.1%`, n_method bloque a 249).

**Mesure apres** : 0 `ring buffer exhausted`.

### 2.5 La porte du miroir exigeait du SPIR-V que le web n'a pas — corrige

`rsx_renderer.cpp:4165` sautait tout draw dont `spirv_wgpu` etait vide. Sous wasm il n'y a pas de
glslang, donc `compile_glsl` echoue toujours et `spirv_wgpu` est toujours vide ; le programme que
le renderer tient vraiment est le **WGSL** (`VpEntry::wgsl`), deja copie dans `win.vs_wgsl`, et
`wgpu_backend.cpp::shader_module()` ignore le SPIR-V sous `__EMSCRIPTEN__` et appelle
`module_from_wgsl(in.vs_wgsl)`. La porte refusait donc exactement les draws que le backend savait
dessiner.

**Mesure avant** : `4170:30080` sauts, `draws=0`, resume backend `0 draws replayed, 0 pipelines`.
**Mesure apres** : `draws=153942`, sauts `69`.

### 2.6 Le device etait adopte mais la cible canvas jamais fabriquee — corrige

`init()` (branche wasm) mettait `g.ready = true` sans appeler `make_targets()` ; la branche native
l'appelle a la fin du sien. `g.color_view` restait nul et `draw()` sortait a sa premiere ligne.

**Mesure avant** : `s1: canvas configured` jamais imprime, readback page = 1 couleur.
**Mesure apres** : `canvas target: tex 0x9bc97248 -> handle 0x9bc97248 view 0x9bc97260`.

### 2.7 La page doit prendre le contexte webgpu elle-meme — corrige

`canvas.getContext('webgpu')` appele depuis le pont (proxy sync) retournait **null** a chaque run,
alors que le meme appel depuis le thread de la page reussit immediatement (sonde : contexte OK).
La page prend donc le contexte **avant** d'instancier le module ; le `getContext` du pont retourne
alors le meme objet.

**Mesure** : `page: canvas webgpu context acquired`, puis `s1: canvas configured 1280x720 format
rgba8unorm`.

### 2.8 Le pont JS est cuit dans game.js au link — piege mesure

Editer `web/s1-bridge.js` ne change **rien** tant qu'on ne relinke pas : le contenu de
`--js-library` est copie dans `game.js`. Mesure : `grep -c s1cfg game.js` = 0 avant relink, 1
apres. Un run « avec instrumentation » sans relink mesure l'ancien pont.

---

## 3. LE MUR QUI RESTE, mesure

Le miroir WebGPU emet ses appels depuis le **pthread FIFO**. Chaque entree du port est annotee
`__proxy: 'sync'` : l'appel est execute sur le realm principal et le pthread **bloque** en
attendant. A raison de dizaines de milliers de draws par seconde, le pthread ne consomme plus le
flux RSX.

**Mesure decisive, deux binaires, meme arbre :**

| binaire | miroir emet des draws | resultat |
|---|---|---|
| `b50a130f89d1c952` | non (re-import desactive) | **42 379 draws, 503 327 methodes**, FIFO `busy 95.8%`, `methods 93.4% (119k/s)` |
| `1db6a8ebbe8da500` | oui (import en fin de frame) | **n_method fige a 378**, FIFO `busy 99.8%`, `methods 0.1% (0k/s)`, `draws=0` |
| `d4757a555c08696d` | oui (vue conservee) | identique : `n_method=378`, FIFO `busy 99.8%`, `methods 0.1%` |

Le stall est **deterministe** (rejoue deux fois, meme point). Il n'est pas cause par le re-import
en lui-meme : il apparait des que le miroir emet des appels WebGPU depuis le pthread.

**Conclusion** : l'architecture « le renderer dessine directement via le miroir, depuis le
pthread » est morte. C'est exactement ce que le handoff disait : le rejeu doit se faire sur le
thread principal. La breche `__proxy: 'sync'` de S1 permet de PEINDRE depuis un pthread (prouve
sur un clear), mais pas de tenir un debit de rendu : chaque appel est un aller-retour bloquant.

---

## 4. CE QUI N'EST PAS FAIT

1. **Le logo n'est pas peint.** Aucune capture ne montre le logo. Les `page-canvas-*.png` sont
   d'une seule couleur ; les `compositor-*.png` a ~2000 couleurs sont le **log DOM en overlay**,
   pas le canvas (verifie a l'oeil : du texte monospace sur fond noir).
2. **`popped` reste a 0** : aucun consommateur C++ de l'anneau sur le thread principal n'a ete
   ecrit. `runtime/include/gpu/rsx_replay.h` declare la couture, mais
   `runtime/src/gpu/rsx_replay.cpp` et `t1_replay_main.cpp` n'existent pas.
3. Le rejeu sur thread principal reste **la** prochaine action, et elle est inchangee par ce tour.

---

## 5. Les fichiers produits

| fichier | contenu |
|---|---|
| `tools/t1_run_page.sh` | assemble le repertoire web et sert la page |
| `runtime/include/gpu/rsx_replay.h` | la couture enregistrement/rejeu (declaree, non implementee) |
| `docs/explore/T1/RESULTS.md` | ce fichier |

---

## 6. La prochaine action, unique

Ecrire le consommateur C++ de l'anneau sur le thread principal
(`runtime/src/gpu/rsx_replay.cpp` + `t1_replay_main.cpp` via `emscripten_set_main_loop`),
mesurer `popped > 0` et `popped == recs`, puis capturer. Le renderer doit cesser d'appeler
`wgpu::draw` depuis le pthread (c'est ce qui fige la FIFO, mesure ci-dessus) et se contenter
d'enregistrer.
