# K0 — Le renderer RSX compile pour WebAssembly

## Le resultat

    $ emcc ... -c runtime/src/gpu/rsx_renderer.cpp
    exit=0
    OBJ BYTES: 489737

Les **4650 lignes** du vrai renderer RSX compilent pour wasm32. Ce qui bloquait n'etait pas
l'architecture : c'etait un portage d'intrinsetques x86.

## Ce qu'il a fallu, mesure

| obstacle | usage | correction |
|---|---|---|
| `<x86intrin.h>` / `<immintrin.h>` | 4 | bloc `#if defined(__wasm__)` : `<emmintrin.h>`, `#define __rdtsc() 0ull`, `#define _mm_pause()` |
| `volk.h` introuvable | — | `-I third_party/volk -I third_party/Vulkan-Headers/include` |
| `fast_hash()` AVX2 | 8 lignes | version scalaire sous `#if`, memes constantes |
| filtre geometrique AVX2 | ~20 lignes | version scalaire sous `#if`, meme logique de lanes |
| `_cvtsh_ss` | 1 | expansion half->float ecrite a la main (exacte pour tous les halves) |
| `_mm_blend_ps` | 1 | selection de lanes par masque |
| `_mm_shuffle_epi8` | 3 | `-mssse3` + `<tmmintrin.h>` (l'emulation existe) |
| `_mm_cvtepi16_epi32` | 1 | `-msse4.1` + `<smmintrin.h>` |
| `_mm_cvtph_ps` (F16C) | 1 | passe par le `half_to_float` portable deja ecrit |

## Les deux corrections qui evitent de reecrire du code

1. **`-mssse3` suffit** pour `_mm_shuffle_epi8` : Emscripten l'emule sur SIMD128. J'ai verifie par
   un programme de 6 lignes avant de toucher au renderer. Pas besoin de `wasm_i8x16_swizzle` a la main.
2. **`-msse4.1` etait deja dans le build** : `_mm_cvtepi16_epi32` manquait seulement de son en-tete.

## Ce qui n'est pas encore fait

Ceci est une **compilation**, pas un rendu. Il reste a :

1. retirer `-DPS3BASE_NO_RENDERER` des 3 lignes de `tools/build_wasm.sh` ;
2. faire que le backend WebGPU prenne le relais de Vulkan (H2 fournit `wgpu_backend.cpp`) ;
3. faire aboutir le rendu sur le canvas de la PAGE dans Chromium (le device y vit,
   `OWNERSHIP page=1 worker=0` mesure par H4) ;
4. capturer avec `tools/x-capture.mjs` + `x6-inpage-hook.js`.

## Pourquoi c'est la bonne nouvelle du tour

`-DPS3BASE_NO_RENDERER=1` etait presente depuis le debut comme un fait acquis, « le build n'a pas de
renderer ». La mesure dit autre chose : le renderer **manquait de neuf corrections d'intrinsetques**,
toutes localisees dans un seul fichier. Ce n'etait pas un chantier d'architecture.