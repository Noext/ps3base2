# R3 — Voie B: backend WebGPU par webgpu.h, meme interface natif et web

Worktree `/root/Dev/r-R3` (branche `explore/rR3`, base `827be60`).
Arbres partages lies : `bin out out-mc fw ref`. Out dir unique : `/tmp/out-R3`.

**Ce fichier est ecrit AVANT les mesures qui suivent.** Chaque section porte la commande exacte
et la sortie brute, collee. Aucune valeur fabriquee.

---

## 0. Antecedents (REGLE 12 — grep fait AVANT d'ecrire)

| Finding | Source (path:line) | Applies here? |
|---|---|---|
| Aucun depot de `refs/` ne contient de backend WebGPU | `grep -Rn wgpu refs/` = 0 ligne (mesure K2) | **oui** : rien a copier |
| Le backend WebGPU natif existe et mesure RMS 10,57 | `explore/kK2` `runtime/src/gpu/wgpu_backend.cpp` (1766 l.) | **oui** : c'est la source a porter |
| L'emetteur WGSL direct existe (11/11 naga+Chromium) | `explore/iH3` `runtime/src/gpu/wgsl_emit.cpp` (956 l.) | **oui** : c'est lui qui produit les shaders cote web |
| Le transport par anneau existe et porte TOUT (56 183 rec. / 3,51 Go) | `explore/iH4` `runtime/src/gpu/rsx_ring.cpp` | **oui** : c'est le seul chemin de donnees |
| Le device WebGPU est en page, jamais dans le worker (petit par `requestDevice`) | `explore/iH4` doc H4 + `tools/h4_owner_probe.mjs` | **oui** : decide l'architecture (sec. 2) |
| `webgpu.h` + `--use-port=emdawnwebgpu` disponibles, port en cache | emsdk 6.0.11, `cache/ports/emdawnwebgpu` | **oui** : socle verifie |

---

## 1. Le socle est verifie, et le port exige C++

```
$ /root/Dev/ps2-web-recomp/tools/emsdk/upstream/emscripten/em++ t.cpp -o t.js --use-port=emdawnwebgpu
$ ls -la t.js t.wasm
-rw-r--r-- 1 root root 101333 Nov 13 09:34 t.js
-rwxr-xr-x 1 root root  93270 Nov 13 09:34 t.wasm
```

Le port lie sans erreur, `webgpu.h` est bien celui de Dawn (`emdawnwebgpu_pkg/webgpu/include`).

---

## 2. DECISION : ou vit le device — mesure, pas preference

La question de l'enonce (item 3) a deux reponses possibles. Les deux ont ete mesurees.

### 2.1 Le device peut-il naitre DANS le wasm ? NON

Sonde `/tmp/R3-probe/pth/p.cpp` : instance, `wgpuInstanceRequestAdapter` en mode
`AllowProcessEvents`, puis pompage par `emscripten_thread_sleep(1)`.

```
$ DISPLAY=:0 node pw3.mjs http://127.0.0.1:8799/index.html
LOGS: [ "module booted", "C: PROBE start", "C: instance 0x25128", "C: adapter 0" ]
```

Meme resultat avec et sans `PROXY_TO_PTHREAD` (deux builds, deux runs) : **le callback d'adaptateur
ne se declenche jamais**. Une promesse JS ne peut pas se resoudre pendant que le wasm occupe le
thread ; `emscripten_thread_sleep` n'y change rien (pas d'Asyncify dans ce build, et Asyncify casse
wasm-opt sur 50 Mo — mesure H7).

### 2.2 Le device peut-il etre TRANSFERE au wasm ? NON

Sonde `/tmp/R3-probe/pre/p.cpp`, device cree en JS puis passe par `preinitializedWebGPUDevice` :

```
LOGS: [ "adapter OK", "device OK (JS, before wasm)", "booted",
        "E: worker sent an error! pre.js:1759: Uncaught TypeError: Cannot read properties of undefined (reading 'queue')" ]
```

Un `GPUDevice` n'est pas transferable dans un worker (et `importJsDevice` echoue cote worker).

### 2.3 Et dans une page sans pthread ? Le JS pur marche

```
LOGS: [ "page gpu=true", "page adapter OK", "MSG worker start", "MSG worker adapter OK" ]
```

Le device se cree tres bien en JS, en page comme en worker DEDIE. Ce qui echoue est le wasm qui
bloque.

### 2.4 Decision, avec son chiffre

**Le device vit dans la PAGE.** L'emulation (wasm) vit dans le worker et n'a aucun device. Les
draws traversent par l'anneau de commandes H4. C'est la seule des deux options que la mesure
autorise, et c'est aussi celle que H4 avait mesuree (`OWNERSHIP page=1 worker=0`).

---

## 3. Le module lie avec le port, et l'anneau produit des enregistrements

Link du module complet (45,4 Mo) avec `--use-port=emdawnwebgpu` :

```
$ ls -la game.js game.wasm
-rw-r--r-- 1 root root   154776 game.js
-rwxr-xr-x 1 root root 45465101 game.wasm
```

L'anneau H4 (producteur `rsx_ring.cpp`, couture dans `lv2/rsx.cpp`) est dans ce build. Compteurs
mesures dans Chromium, apres 55 s, par `mod._rsx_ring_stat(i)` :

```
STATS {"0":923,"1":16769816,"2":21227050,"3":280608,"4":1248,"5":0,
       "6":16366788,"7":85,"8":108544,"9":27,"10":26,"11":53,"12":13,
       "13":1160458,"15":66,"16":83,"17":1,"18":70969,
       "addr":8666112,"cap":16777216}
```

Lecture : 923 enregistrements, 26 draws, 53 clears, 13 flips, 27 shaders (108 544 o d'ucode),
85 objets texture (16,4 Mo), 1 thread producteur. `refused=1160458` : l'anneau de 16 Mo est trop
petit pour le debit de textures — c'est le premier defaut a corriger cote page (drainer par frame).

La couture est bien celle qui TOURNE, pas une qui existe :

```
I 1.397780 [10] rsxring: first method through the seam: 0x0060 arg=0x56616661 io=0x00001000
```

---

## 4. La frontiere K0-bis est levee : le renderer LIE en wasm

K0-bis avait nomme le mur : `rsx_renderer.cpp` compile mais reclame 18 symboles Vulkan au link.
Ce tour mesure que le mur tombe, et par quel mecanisme.

### 4.1 `runtime/src/gpu/vk_null.cpp` — 58 entrees Vulkan en no-op, signees exactement

Les **58** fonctions (pas 18 : le compte de K0-bis portait sur les symboles distincts du premier
rapport d'erreur, pas sur l'ensemble) sont generees par script depuis les typedefs `PFN_vk*` de
`third_party/Vulkan-Headers/include/vulkan/vulkan_core.h`. Pas ecrites a la main : une signature
fausse lie silencieusement en wasm et corrompt la pile de l'appelant.

```
$ python3 /tmp/R3-probe/genvk2.py
wrote stubs 58
$ wc -l runtime/src/gpu/vk_null.cpp
574 runtime/src/gpu/vk_null.cpp
```

Deux pieges mesures :
1. **volk declare les `vk*` comme des POINTEURS de fonction** (`extern PFN_vkXxx vkXxx;`), pas
   comme des fonctions. Definir `vkCmdDrawIndexed` comme fonction est une redefinition d'une autre
   espece et ne compile pas (`error: redefinition ... as different kind of symbol`). Le backend nul
   remplace donc le POINTEUR.
2. Le regex sur `vulkan_core.h` attrape `VkPhysicalDevicePushDescriptorPropertiesKHR` comme type de
   retour de `vkCmdPushDescriptorSetKHR` (un `typedef` voisin est sur la meme ligne logique). Corrige
   a la main, documente ici.

### 4.2 `runtime/src/gpu/gpu_wasm_support.cpp` — 27 symboles de support

Apres `vk_null`, il restait 27 symboles, tous des fonctions de support et aucun du chemin de decode :
`compile_glsl`/`load_cached_glsl` (glslang, pas de build wasm — le chemin web emet du WGSL),
`edge_compute::*`, `mv::*` (DLSS), le presenter (swapchain + fenetre native), et les compteurs
`rsx::*_stats` qui vivaient dans `rsx_no_display.cpp` sous `PS3BASE_NO_RENDERER`.

Correction de methode importante : `enabled()` renvoie **true**, et c'est le seul "oui" qui ne
simule pas Vulkan. `gpu::enabled()` est lu par `rsx_renderer.cpp:4519` comme « puis-je finir une
frame ? » ; avec `false`, `present()` retourne avant `end_pass()` **et avant tous ses diagnostics** —
les compteurs du renderer (draws, skipped, raisons de skip par ligne) ne sont imprimes que la.
Mesure avec `false` : 1321 flips et pas une ligne disant pourquoi rien n'etait dessine.

`display_buffer()` est de meme : il **enregistre** la description que le guest fournit et repond
vrai, au lieu de renvoyer faux et de faire sortir `present()` tot.

### 4.3 Resultat

```
$ em++ ... -sERROR_ON_UNDEFINED_SYMBOLS=0 -o game.js obj/*.o
(0 undefined symbol)
-rwxr-xr-x 1 root root 45547321 game.wasm
```

Le module boote et flippe dans Chromium avec le VRAI renderer lie :

```
$ grep -oE 'rsx: flip [0-9]+' /tmp/R3-probe/rend3.log | tail -1
rsx: flip 241
```

---

## 5. Le chemin de decode s'execute, et ses trois murs mesures

Le renderer lie ne suffisait pas : `draw()` etait appele 72 775 fois et **skippait 72 775 fois**.
Chaque mur a ete nomme par mesure, pas par lecture.

### 5.1 Mur 1 — `gpu::enabled()` faux : le decode n'etait jamais atteint

Instrumentation (exportee, `r3_n_method_get` / `r3_n_enabled_false_get`) :

```
{"n_method":700009, "n_begin":62679, "n_end":62679, "n_ef":700009, "draws":0}
```

**700 009 appels a `method()`, et `enabled()` faux sur les 700 009.** `rsx_renderer.cpp:4386`
retourne immediatement quand `enabled()` est faux, donc aucun `BEGIN_END` n'atteint `draw()`, et
aucun etat RSX n'est decode. `gpu::enabled()` n'etait pas la fonction que je croyais patcher :
`gpu_wasm_support.cpp` en definit trois (`edge_compute::`, `mv::`, `gpu::`), et mon premier patch
etait tombe sur `edge_compute::enabled()`.

Apres correction : `n_ef=0`. C'est le seul "oui" du fichier qui ne simule pas Vulkan, parce que
cette fonction ne dit pas "le GPU marche", elle dit "tu peux executer une frame" — et `present_image()`
reste nul, donc rien n'est dessine.

### 5.2 Mur 2 — 72 775 draws, tous skippes a la ligne 3043 : pas de shader

```
skip":"3043:72775", "nlines":1, "skipped":72775, "draws":0
```

`rsx_renderer.cpp:3043` est `if (!vp || !vp->ok) return skip(__LINE__)`. Le programme de vertex
n'a pas de module : `compile_glsl` appelle glslang, qui n'a **pas de build wasm ici**. C'est
exactement ce que la piste H3 a resolu autrement : `runtime/src/gpu/wgsl_emit.cpp`, emetteur
RSX -> WGSL direct (11/11 programmes reels valides par naga et par Chromium).

Les deux sites d'emission sont ceux ou les entrees sont encore vivantes :
- vertex : `vertex_program()`, ou `g_vp_canon`/`g_vp_canon_start` sont en portee ;
- fragment : `fragment_program()`, ou `read` et `key` sont en portee.

Premier resultat de ce cablage — les shaders du LOGO apparaissent, avec les hash de K2 :

```
I 10.095145 rsx: vertex program fd4be450bbeda1e5 start 9: queued (1 seen, 1 failures)
I 10.110155 rsx: fragment program 6081aed9d403e4d2 at 0x5c850680 (7 slots, 0 literals): ok (1 seen, 1 failures)
```

`6081aed9d403e4d2` et `fd4be450bbeda1e5` sont exactement les hash du logo mesures par K2
(`docs/explore/K2/RESULTS.md`, tableau sec. 5). Le FP sort **ok**.

### 5.3 Mur 3 — debordement de pile : 64 Ko par defaut, et l'emetteur en demande plus

Avec l'emission vertex active : `RuntimeError: memory access out of bounds`, module mort.
La pile Emscripten par defaut est **64 Ko** (`settings.js:113  var STACK_SIZE = 64*1024`).

A/B mesure, meme build, meme disque, seule difference `-sSTACK_SIZE` :

| config | resultat |
|---|---|
| `-sSTACK_SIZE=8388608` (8 Mo), emission vertex ACTIVE | `draws=43`, `skipped=61`, `skip":""`, **0 crash** |
| meme build, emission vertex desactivee (`A/B: emit off`) | 601 flips, 0 crash |
| `-sSTACK_SIZE=33554432`, emission ACTIVE | pas de crash, mais le run n'atteint pas la boucle (25 s bloque `pc=0x30004da8`) |

**Le passage de 0 a 43 draws est le resultat de ce tour** : avant le cablage WGSL, `draws=0` et
72 775 skips sur la ligne 3043 ; apres, `draws=43`, `skipped=61`, et la liste des raisons de skip
est **vide** — le decode va au bout de `draw()`.

### 5.4 Ce qui n'est PAS fait, et pourquoi (nomme, pas maquille)

1. **Aucune image du jeu n'est capturee.** Le critere de l'enonce n'est pas atteint. Le chemin
   de donnees existe (anneau H4 : 923 enregistrements, 26 draws, 85 textures) et le decode tourne
   (`draws=43`), mais **la page ne rejoue rien encore** : il n'y a pas de consommateur WebGPU cote
   page qui reconstruise les draws depuis l'anneau.
2. Le run s'arrete a t=10 s sur `Aborted()` dans le thread audio `MSSAsync` (0x1000007), pas dans
   le rendu. C'est une limite independante a nommer.
3. `refused=2621` a l'anneau : 16 Mo ne suffisent pas ; il faut drainer par frame cote page.
