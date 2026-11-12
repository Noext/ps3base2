# I4 — Etat final du tour : les quatre livrables

## Livrable 1 — la ligne du menu : NON, cause mesuree

| mesure | natif | wasm |
|---|---|---|
| `cellPadGetData` | **2549** appels a t=60 s | **0** appel en 120 s |
| threads crees | 12+ | 8 |
| `(4J) Basic Event Handler` | entre | **0 entree** |

Chaine mesuree par la piste H1 :

    input chain 0x2219d8=1 0x2c86a4=1 0x2c97b0=1 0x2c8734=0 0x2c9e24=0 0x2cc6b0=0
    wait-loop sleeps lr=0x2221fc:1198
    octet d'attente 0xCE5AA0=1, remis a 0 par f_0021d850 (thread '(4J) Basic Event Handler')

**Le site d'appel du pad (`0x222274`) est en aval de la boucle d'attente.** Le jeu n'atteint jamais
l'ecran qui lit le pad, donc aucune injection d'entree ne peut le piloter. Ce n'est pas un probleme
d'entree : c'est un thread qui n'est jamais cree.

## Livrable 2 — la capture du menu dans Chromium : NON, et pourquoi

Le module **boote et flippe** en Chromium :

    I 102.448850 [ 10] rsx: flip 1741 (buffer 0) worst 51.0 ms
    ps3base-wasm: guest clock +80095600 ticks/s (100.1% of 80MHz realtime)

mais il **ne dessine pas** : `-DPS3BASE_NO_RENDERER=1` (tools/build_wasm.sh:56), `grep -c
rsx_renderer game.js` = 0. Le hook de readback s'installe (`hook: true`), il n'y a rien a lire.

Quatre causes ont ete trouvees et corrigees pour arriver au boot :

1. `PTHREAD_POOL_SIZE=4` sans `PROXY_TO_PTHREAD` — en node le pool s'etend, en navigateur
   `STRICT` s'applique et le 13e thread echoue ; `_gcm_intr_thread` ne demarrait jamais.
   Corrige : pool 24, STRICT=2, PROXY_TO_PTHREAD=1.
2. `python3 -m http.server` ne pose pas COOP/COEP : `SharedArrayBuffer` absent, pthreads morts.
   `web/serve.py` existait deja pour cela.
3. `web/worker.js` utilisait `import.meta.url`, qui jette dans un worker classique.
   Remplace par `self.location.href`.
4. Le module n'etait pas un module ES6 : `-sMODULARIZE=1 -sEXPORT_ES6=1` ajoutes.

## Livrable 3 — tableau de validation des shaders : OUI

`runtime/src/gpu/wgsl_emit.cpp` (956 lignes) : meme decodage d'opcodes que les decompilateurs
existants, sortie WGSL directe, **zero GLSL, zero glslang, zero SPIR-V**. Texture et sampler separes.

| programmes | valides naga 30.0.1 | valides Chromium | rejetes | raison |
|---|---|---|---|---|
| 11 (7 FP + 4 VP) | **11** | **11** | **0** | — |

Corpus produit par le titre via `GOWA_SHADER_DUMP=1` (etendu, pas double). 5 ecarts GLSL corriges,
5 bugs reels trouves par les validateurs. Limite : cable sur le dump, pas sur le chemin de rendu.

## Livrable 4 — comparaison WebGPU natif / Vulkan : OUI, avec une auto-correction

`wgpu-native v29.0.1.1` (pas Dawn : absent de la machine), vendorise dans `third_party/`, **19 Mo**
de lib statique, **8,4 s** de rebuild. Compile en natif **a cote** de Vulkan, pas a la place.

| mesure 1280x720 menu | valeur |
|---|---|
| RMS | **69,51 / 255** |
| pixels a plus de 8 niveaux d'ecart | **17,44 %** |
| couverture WebGPU / Vulkan | 75,0 % / 72,4 % |
| controle (Vulkan contre lui-meme) | **RMS 0,0000** |

**Auto-correction honnete de la piste** : son premier chiffre etait RMS 188,26 avec 100 % de
couverture. C'etait faux — l'image etait d'une seule couleur, et elle l'a prouve en changeant la
texture factice de magenta a vert et en regardant la frame suivre. Un draw plein ecran non texture
peignait par-dessus tout.

Erreur restante localisee : le chemin d'echantillonnage de texture. La piste a aussi **refute sa
propre hypothese** `texscale[u>>2][u&3]` : le bug existe dans le code, mais le shader du menu
n'utilise jamais ce tableau.

12 defauts bloquants trouves et corriges, dont le plus couteux : les enums Vulkan et WebGPU sont
**decalees d'un cran** (WebGPU a un `Undefined = 0` en tete), donc `VK_COMPARE_OP_LESS` = 1 devenait
WebGPU `Never` = 1 et rejetait silencieusement tous les fragments.

## Bilan

| livrable | etat |
|---|---|
| ligne du menu | **NON** (thread `(4J) Basic Event Handler` jamais cree) |
| capture du menu Chromium | **NON** (module boote, `NO_RENDERER`) |
| tableau de validation des shaders | **OUI** : 11/11 naga, 11/11 Chromium, 0 rejete |
| comparaison WebGPU / Vulkan | **OUI** : RMS 69,51, controle 0,0000, auto-correction documentee |