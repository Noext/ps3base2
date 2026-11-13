# T2 — la premiere image du jeu dans Chromium

Base : worktree `/root/Dev/t-T2`, branche `explore/tT2`, depuis `d41c834`.
Binaire de reference deja sur disque : `/tmp/out-T/game.wasm`, md5 **2204ffc4d8e3e81c**,
data 1 704 884 266 o (build `DISK=/tmp/disk-r4 bash tools/build_wasm.sh . /tmp/out-T`).

## Antecedents (grep fait avant toute modification)

| Finding | Source (path:line) | Applies here? |
|---|---|---|
| le device vit dans la page, l'emulation dans un worker, les draws passent par l'anneau | `docs/explore/R-FINAL.md` §R3 | **oui** — c'est la topologie mesuree par la piste R |
| la page ne draine pas l'anneau : c'est le chainon manquant | `docs/explore/R0TER-ETAT.md` fin | oui — mais voir ci-dessous : ce n'est pas la bonne porte |
| `Module.preinitializedWebGPUDevice` + `emscripten_webgpu_get_device()` : le contrat page -> C synchrone | `refs/../ps3-c1/libs/video/rsx_webgpu_backend.h:66`, `ps3-c1/runtime/host/wasm/probe_webgpu.html:43` | oui — c'est exactement le mecanisme a adopter |
| `emdawnwebgpu` exige `--use-port=emdawnwebgpu`, `-sUSE_WEBGPU=1` est rejete, link C++ obligatoire | `ps3-c1/docs/RSX_WEBGPU_BACKEND.md` §"The Emscripten idiom" | oui |
| `emscripten_webgpu_get_device()` n'est pas wasm64-safe dans ce port | ibid. | **non** — notre build est wasm32 |
| la sequence native reelle : (1) logotype PlayStation, (2) **MOJANG**, (3) **4J STUDIOS**, (4) splash Minecraft + autosave, (5) TERMS OF USE, (6) menu | `docs/explore/I/RESULTS.md` §8, captures `/tmp/I/scan/caps/c-1794477293..` | oui — c'est l'oracle de comparaison |
| le logo 4J natif fait 620 couleurs distinctes en 1280x480 | `docs/explore/R-FINAL.md` §R2 | oui — ordre de grandeur du critere "centaines de couleurs" |

## Ce que T2 change, et pourquoi (avant toute mesure)

R0-ter conclut "la page ne draine pas l'anneau". Mesure refaite ici sur l'arbre :
`grep -rn 'command_ring\|CommandRing' runtime/src` ne rend **que** `gpu/command_ring.cpp` et le
test ; **aucun producteur, aucun consommateur**. `runtime/src/gpu/rsx_renderer.cpp` n'a aucune
mention de `webgpu`/wgpu. Autrement dit, dans **cet** arbre il n'y a pas d'anneau a drainer :
la topologie worker+anneau decrite par R vivait dans un autre arbre, jamais fusionne.

La voie qui n'a **aucun appel WebGPU depuis un pthread** et qui rejoue l'anneau depuis le thread
principal n'est donc pas "un drain a ecrire", c'est "un device a adopter". Le chemin retenu :

1. Le module est deja lie avec `-sPROXY_TO_PTHREAD=1` : `main()` tourne dans un pthread, le thread
   principal du navigateur est libre et ne fait qu'executer `requestAnimationFrame`.
2. La page cree le `GPUDevice` sur le **thread principal**, l'installe dans
   `Module.preinitializedWebGPUDevice` **avant** l'instanciation du module.
3. Le code C++ du renderer (thread RSX) **met en file** ses presentations dans un petit ring SPSC
   en memoire partagee (`runtime/include/runtime/present_ring.h`), sans toucher au device.
4. `emscripten_set_main_loop` sur le thread principal appelle une fonction C++ exportee qui
   **rejoue** ce ring : `emscripten_webgpu_get_device()` -> copie de la surface RSX dans une texture
   du canvas -> submit. Aucun rejeu JS.

## Le critere, et ce qu'il n'accepte pas

Une capture `readback-*.png` de `tools/x-capture.mjs --auto-hook docs/explore/X6/x6-inpage-hook.js`
qui (a) vient du canvas WebGPU de la page, (b) a des centaines de couleurs distinctes, (c) montre
la **structure** de l'ecran attendu, comparee a la capture native correspondante. Une image d'une
couleur, du texte DOM, ou un quad de test **ne compte pas** et sera declare tel quel.

## Mesures

(remplies au fur et a mesure ; commande + sortie brute, jamais de valeur fabriquee)
