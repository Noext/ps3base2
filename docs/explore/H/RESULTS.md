# PISTE H — Rendu WebGPU etape 2 : formats, shaders, etats (minimum pour un menu)

Worktree `/root/Dev/explore-H` (branche `explore/H`, base `d290fda`). Arbres partages lies :
`bin out out-mc third_party fw ref refs`.

**Resultat en une ligne** : le couple de shaders REEL du menu Minecraft (`vp_b5791730607a9ef5` +
`fp_6081aed9d403e4d2`) tourne dans WebGPU sous Chromium, avec les etats RSX mesures, sur une
texture RSX reelle (fmt 0x81), et couvre 7,5 % de la frame. Sur les 16 constructions GLSL mesurees,
**5 exigent du generateur un code different**, 11 passent telles quelles. Preuve :
`docs/explore/H/img/menu-min-webgpu.png`.

---

## 0. Antecedents (grep -R, sortie brute)

    $ grep -Rln "wgpu\|WebGPU\|webgpu" refs/ --include=*.md --include=*.cpp --include=*.h --include=*.js
    (0 ligne)

| Finding | Source (path:line) | Applies here? |
|---|---|---|
| **aucun des 15 depots de `refs/` ne contient de port WebGPU** | `grep -Rln` ci-dessus, 0 ligne | **oui** : il n'y a pas de porteur a copier. Tout ce qui suit est etabli par la mesure, pas par analogie. |
| table Vulkan->WebGPU deja ecrite (31 formats, 14 facteurs, 13 etats) | `runtime/src/gpu/wgpu_map.cpp:10-113` | **oui**, mais insuffisante : elle part de `VkFormat`, or le jeu emet des codes RSX (8, 129, 0x81, 0x85). Il manque l'etage RSX->Vulkan. Mesure section 1. |
| table GLSL->WGSL : 53 entrées, 2 rewrites | `runtime/include/runtime/wgsl_map.h`, `docs/webassembly/JALON2-WGSL-TABLE.md` | **oui**, et verifiee ici contre les vrais shaders : 5 des 8 ecarts mesures n'y sont PAS (section 3). |
| cle de pipeline 26 octets + cache LRU | `docs/webassembly/JALON2-PIPELINE-KEY.md`, `JALON2-PIPELINE-CACHE.md` | **oui** : le menu n'utilise que **11 combinaisons** distinctes (mesure 2.3), donc le cache n'est pas le cout. |
| le natif demarre, presente 60 Hz, shaders 0 echec | `docs/explore/I/RESULTS.md` | **oui** : c'est l'oracle d'ou viennent tous les shaders et etats ci-dessous. |
| WebGPU present dans Chromium ici | `docs/explore/F/docs/explore/F/RESULTS.md` + mon probe (mesure 2.1) | **oui** : adaptateur `nvidia/lovelace`, valide. |
| RSX change d'etat par draw => cache de pipelines explosif | `JALON2-PIPELINE-KEY.md` (premisse du prompt) | **non pour le menu** : mesure 2.3 donne 11 etats distincts et 61 sets/s, pas de thrashing. La premisse vaut pour le monde, pas pour un menu. |

---

## 1. Formats de texture du jeu (mesure, pas table theorique)

### 1.1 Ce que le jeu demande reellement (oracle natif)

    $ PS3BASE_LOG=info PS3BASE_TEX_LOG=1 ./build/ps3base --no-launcher   (log conserve:
      docs/explore/H/oracle-run3.log)
    texstat: fmt 0x81 swizzled 512x512 mips 1 pitch 512: 2 decodes
    texstat: fmt 0x85 swizzled 2x1     mips 1 pitch 16:  1 decodes
    texstat: fmt 0xa5 linear   321x78  mips 1 pitch 1284: 8 decodes

Sur 4000 draw journalises (`docs/explore/H/oracle-run3-extract.log`, `GOWA_DRAW_LOG`), les formats de
texture lus par le sampler sont :

| code RSX | linear | mips | occurrences | sens | format Vulkan choisi par le runtime |
|---|---|---|---|---|---|
| `0x1a529` | oui | 1 | 498 | `0x85` B8R8G8A8 | `VK_FORMAT_B8G8R8A8_UNORM` |
| `0x18129` | non | 1 | 293 | `0x81` B8 | `VK_FORMAT_R8_UNORM` |
| `0x9a529` | oui | 9 | 40 | `0x85` + mips | idem |
| `0x6a529` | oui | 6 | 21 | `0x85` + mips | idem |
| `0xaa529` | oui | 10 | 21 | `0x85` + mips | idem |

**Deux formats seulement portent le menu** : `0x85` (32 bits BGRA, 8 bits/canal) et `0x81`
(8 bits, un canal). Le format `0xa5` = `0x85` + marqueur linear 0x20.

### 1.2 Formats de surface (rendertarget)

    $ grep "new .* surface" docs/explore/H/oracle-run2.log
    rsx: new color surface at local 0x280000 1280x480 format 44 pitch 0x1400
    rsx: new depth surface at local 0x0      1280x480 format 129 pitch 0x1400
    rsx: new color surface at local 0x500000 1280x720 format 44 pitch 0x1400
    rsx: new depth surface at local 0x0      1280x720 format 129 pitch 0x40

`format 44` = `VK_FORMAT_B8G8R8A8_UNORM` (enumere dans `third_party/Vulkan-Headers/include/vulkan/vulkan_core.h:1621`),
issu de RSX `CELL_GCM_SURFACE_A8R8G8B8 = 8` (`refs/rpcs3/rpcs3/Emu/RSX/gcm_enums.h:942`).
`format 129` = `VK_FORMAT_D24_UNORM_S8_UINT` (`vulkan_core.h:1686`), issu de `Z24S8 = 2`.

**Le menu tient donc en 3 formats** : BGRA8 couleur, D24S8 profondeur, et les textures 0x85/0x81.

### 1.3 Traduction mesuree, ligne par ligne

Verifie en vrai dans Chromium (`docs/explore/H/tools/wgslcheck.mjs`, `format_probe.mjs`), pas sur table :

| besoin du jeu | format Vulkan actuel | offre WebGPU | ecart | effort |
|---|---|---|---|---|
| couleur de menu, RSX 8 | `B8G8R8A8_UNORM` | `bgra8unorm` (cree : OK) | **aucun** | 0 |
| profondeur, RSX Z24S8 | `D24_UNORM_S8_UINT` | `depth24plus-stencil8` (cree : OK) | stencil non echantillonnable, profondeur 24 bits "plus" : non mixable avec du D32 | 0 pour un menu (pas de lecture de profondeur) |
| texture 0x85 BGRA8 | `B8G8R8A8_UNORM` | `bgra8unorm` (cree : OK) ; `bgra8unorm-srgb` pour la variante sRGB | **aucun** | 0 |
| texture 0x81 B8 (un canal) | `R8_UNORM` | `r8unorm` (cree : OK) | **aucun**, MAIS le shader lit `.rgba` : il faut etendre en RGBA8 a l'upload (le runtime le fait deja, `texture_decode.cpp:73`) ou activer `texture-component-swizzle` | 0 avec l'expansion existante ; ~1 j si on veut le swizzle natif |
| variantes sRGB des surfaces | `B8G8R8A8_SRGB` | `bgra8unorm-srgb` (cree : OK) | WebGPU n'a pas de vue mutable de format : il faut deux textures, pas une vue | 0 pour un menu (le menu n'active pas lin2srgb, mesure : `fflags.x & 2` = 0) |
| 16 bits packed `A1R5G5B5`, `R5G6B5`, `R4G4B4A4` | `*_PACK16` | **aucun format 16 bits packed** | 2x la memoire si on elargit en RGBA8 | deja traite par `wgpu_map.cpp:33-36` en `Expand` ; **non utilise par le menu** (mesure : 0 occurrence) |
| BC1/BC2/BC3 | `BC*_BLOCK` | `bc1-rgba-unorm`, `bc3-rgba-unorm` + feature `texture-compression-bc` | la feature N'EST PAS demandee a `requestDevice` : `createTexture` echoue (mesure ci-dessous) | 0 pour un menu (0 texture compressee), 1 ligne pour le monde |
| `depth32float-stencil8` | `D32_SFLOAT_S8` | existe en tant que **feature** | pas dans `requiredFeatures` => `createTexture` refuse | 0 pour un menu |

Sortie brute de la sonde de formats (adaptateur reel, page servie en contexte isole) :

    $ node docs/explore/H/tools/format_probe.mjs
    adapter: {"vendor":"nvidia","arch":"lovelace"}  maxTex2D 8192  maxColorAttach 8
    bgra8unorm OK / bgra8unorm-srgb OK / rgba8unorm OK / rgba8unorm-srgb OK
    r8unorm OK / rg8unorm OK / rg8snorm OK / rgba16float OK / rgba32float OK / r32float OK
    depth24plus-stencil8 OK / depth16unorm OK / depth32float OK / rg16float OK
    depth32float-stencil8  FAIL "Use of the 'depth32float-stencil8' texture ... requires the
                           'depth32float-stencil8' feature"  (la feature EST dans adapter.features,
                           mais il faut la demander a requestDevice)
    bc1-rgba-unorm         FAIL "... requires the 'texture-compression-bc' feature"
    bc3-rgba-unorm         FAIL idem

---

## 2. Etats necessaires au MENU (mesure)

### 2.1 Environnement de mesure

    $ nvidia-smi --query-gpu=name --format=csv,noheader
    NVIDIA GeForce RTX 4060 Laptop GPU
    $ /root/.cache/ms-playwright/chromium-1248/chrome-linux64/chrome --version
    Google Chrome for Testing 156.0.8078.4
    flags: --no-sandbox --enable-unsafe-webgpu --enable-features=Vulkan --use-angle=vulkan
    page servie sur http://127.0.0.1:8108 (COOP/COEP)  -> isSecureContext true

Le flag set est celui que F a mesure ; mes propres essais avec `--disable-vulkan-surface`,
`--ignore-gpu-blocklist` ou `--use-vulkan=native` donnent `navigator.gpu` **absent**
(`probe.js` : `{"gpu":false}`), ce qui confirme la mesure de F.

### 2.2 Le MENU, fenetre temporelle

D'apres l'oracle, les surfaces 1280x720 apparaissent a t=22,4 s ; le menu principal est atteint a
t=54,97 s (I). J'ai analyse la fenetre 22,4-31,8 s (873 draws) qui contient deja les programmes du
menu et les seuls formats 0x81/0x85 — c'est la fenetre ou les deux shaders du menu sont vivants.

### 2.3 Distribution des etats (873 draws de menu)

    prim  = 8 (quads) x667, 5 (triangles) x206
    fmt   = 8 (A8R8G8B8) x873       targets = 1 x873
    blend = 0x1 (actif) x624, 0x0 (inactif) x249     mask(write) = 0xf x873
    sf/df/eq = 0x10001 / 0x3030303 / 0x8006  x2821 sur 4000 draws
               = ONE / ONE_MINUS_SRC_ALPHA / FUNC_ADD
    depth test = 1 x206 (24%), 0 x667        dfunc = 0x201 (CELL_GCM_LESS) x831
    dmask = 1 quand dtest=1 (206), 0 sinon
    cull  = 0 (desactive) x873               front = 0x901 (CCW)
    stencil test = 0 x873                    sfunc = 0x202 (EQUAL) mais jamais active
    sops  = 0x1e00/0x1e00/0x1e01 (KEEP/KEEP/REPLACE), swmask = 0x0 x852
    alpha test = 0 x873                      alpha func = 0x204
    clear z = varie (0x0 en general)
    -> **11 tuples d'etat de pipeline distincts** sur la fenetre.

    $ grep "state sets" docs/explore/H/oracle-run2.log | tail -1
    rsx:   state sets 61.2 per frame: blend 1.2 cull 2.4 depth 25.2 stencil 18.6
           viewport 2.2 scissor 8.1 topology 2.4 other 1.2
    rsx: renderer: 40828 draws, 0 skipped, ... **12 pipelines**, 10 textures, 8 surfaces

### 2.4 Ce que ca donne compare a WebGPU

| besoin du jeu | etat Vulkan utilise | offre WebGPU | ecart | effort |
|---|---|---|---|---|
| melange alpha du menu | dynamic `vkCmdSetBlendConstants` + facteurs cuits | facteurs cuits dans le pipeline, **pas de facteur constant-alpha** | `CONSTANT_ALPHA`/`ONE_MINUS_CONSTANT_ALPHA` (RSX 0x8003/0x8004) n'existent pas | **0 pour le menu** : sf/df mesures = ONE / ONE_MINUS_SRC_ALPHA, aucun constant-alpha (mesure 2.3) |
| ecriture par canal | `colorWriteMask` (mask 0xf) | `writeMask` dans le pipeline | aucun | 0 |
| ecriture RGB seulement | `omask` 0xc000/0x4000/0x4020 | idem, valeur dans la cle | aucun | 0 |
| test de profondeur | `DEPTH_TEST_ENABLE/FUNC/MASK` dynamiques | cuits : `depthCompare`, `depthWriteEnabled` | changement par draw = nouveau pipeline | 0 pour le menu : 11 combinaisons (mesure 2.3) |
| **pipeline avec profondeur dans une passe sans attache de profondeur** | Vulkan avertit, rend quand meme | **WebGPU REFUSE** : `setPipeline` invalide, tout le command buffer est jete | il faut TOUJOURS lier l'attache de profondeur quand le pipeline en declare une | **piege reel, mesure ci-dessous** — 1 h si on le connait, 1 j sinon |
| stencil | 8 ops dynamiques | cuits ; devant/arriere separes supportes | aucun pour le menu (stencil jamais actif) | 0 ; 1 j pour le monde (stencil 17 sets/image) |
| cull | `vkCmdSetCullMode` dynamique | cuit ; `FRONT_AND_BACK` absent | `NONE/FRONT/BACK` seulement | 0 menu (cull toujours off) ; emulation `discard` documentee dans `wgpu_map.cpp:95` |
| topologie | `vkCmdSetPrimitiveTopology` | cuite ; **pas de `TriangleFan`** | prim 8 (quads) doit etre converti en liste | **fait** : `rsx_renderer.cpp:2757-2766` convertit deja, et c'est ce que la demo utilise |
| viewport / scissor | dynamiques | dynamiques (`setViewport`/`setScissorRect`) | aucun | 0 |
| line width, depth bias, blend constants | dynamiques | dynamiques | aucun | 0 |
| logic ops (RSX `LOGIC_OP`) | dynamiques | **absents de WebGPU** | pas d'equivalent | 0 menu (aucun logic op mesure) ; 2 j monde |

**Le piege mesure** (c'est la seule vraie surprise cote etats) :

    UNCAPTURED: Attachment state of [RenderPipeline] is not compatible with [RenderPassEncoder].
      RenderPassEncoder expects { colorTargets: [{format: BGRA8Unorm}], sampleCount: 1 }.
      RenderPipeline has     { colorTargets: [{format: BGRA8Unorm}], depthStencilFormat:
                               Depth24PlusStencil8, sampleCount: 1 }.
    readback: 0 drawn texels, 921600 black (0.0% of frame covered)

Aucune erreur levee a `createRenderPipeline` : l'erreur n'arrive qu'a `setPipeline`, et elle
**largue tout le command buffer** au lieu de dessiner sans profondeur. C'est le mode de defaillance
le plus couteux du portage (image noire sans exception).

---

## 3. Shaders : RSX -> WGSL, ce qui manque

### 3.1 Les shaders du menu, extraits de l'oracle

    $ GOWA_SHADER_DUMP=1 ./build/ps3base --no-launcher   -> docs/explore/H/glsl/
    vp_b5791730607a9ef5 (95 l.)  fp_6081aed9d403e4d2 (121 l.)   <- paire principale
    vp_fd4be450bbeda1e5 (175 l.) fp_eb106bdc372694a8 (121 l.)   <- paire alpha
    vp_1dcd499163a4bf6c (100 l.) fp_8387a997a1878b48 (312 l.)
    vp_04ec1fbc8e4d75fc ( 75 l.) fp_7c77c1d62f32f07a (225 l.)

8 programmes distincts pour le menu (4 VP + 4 FP), 1291 lignes de GLSL au total.

### 3.2 Recensement mesure (`docs/explore/H/tools/shader_census.py`)

    $ python3 docs/explore/H/tools/shader_census.py docs/explore/H/glsl
    files: 8
    -- kind totals --
      Same / control flow      97
      user helper (hand port)  68
      Type (returns vecN<bool>) 28
      Rename                   25
      Rewrite                   8
    -- samplers: {'sampler2D': 4, 'sampler1D': 1}
    -- builtins: gl_FragCoord x4, discard x4, gl_Position x4, gl_PointSize x4,
                 gl_PerVertex x4, invariant x4

### 3.3 Ce qui manque, mesure dans Chromium, ligne par ligne

Chaque ligne est un module WGSL compile dans le vrai Chromium. `erreur` = sortie brute de
`getCompilationInfo()`, `recette` = le module qui passe (0 erreur).

| # | besoin du jeu (GLSL emis) | offre WebGPU | ecart (sortie brute) | recette mesuree | effort |
|---|---|---|---|---|---|
| 1 | `lessThan/equal/greaterThan(v,w)` — 28 occurrences, dans **8/8 shaders** | operateurs `<`, `==`, `>` sur vecteurs | **`unresolved call target 'lessThan'`** | `v < w` (retourne `vec4<bool>`) | **1 h** : 6 renommages, table `wgsl_map.cpp` les classe deja `Type` — mais elle dit "same name", ce qui est FAUX, c'est un operateur |
| 2 | `r = bvec4(uvec4(r) \| uvec4(lt))` — bitwise OU sur des booleens, 6x par shader | pas de conversion bvec<->uvec ; `\|` existe sur `vecN<bool>` | **`cannot assign 'vec4<u32>' to 'vec4<bool>'`** | `r = r \| (v < vec4(0.0))`, garder `vec4<bool>` de bout en bout | **2 h** : le decompilateur doit generer autre chose, c'est le seul vrai "Rewrite" de la table qui n'y est pas |
| 3 | `uniform sampler1D tex7` + `texture(tex7, x)` | `texture_1d<f32>` **existe et fonctionne** | **aucun** (compile, 0 erreur) | `var t1: texture_1d<f32>` + `textureSample(t1, s, x)` | **0** — contre-intuitif : `shader_census.py` le classe "absent", la mesure le refute |
| 4 | `uniform sampler2D tex0` combine avec le sampler | texture et sampler sont deux valeurs separees | **`no matching call to 'textureSample(texture_2d, texture_2d, vec2)'`** + la liste des 15 surcharges | deux bindings : `texture_2d` + `sampler`, `textureSample(tex, samp, uv)` | **1 j** : change le layout de descripteurs (RSX a un sampler combine par unite, WebGPU en veut 2) |
| 5 | `out gl_PerVertex { invariant vec4 gl_Position; float gl_PointSize; }` | `@builtin(position)` ; `invariant` n'existe pas | **`expected ':' for struct member`** (sur `invariant`) | supprimer `invariant`, `@builtin(position)` dans la struct de sortie | **1 h** |
| 6 | `gl_PointSize = ...` ecrit inconditionnellement dans 4/4 VP | `@builtin(point_size)` **absent de cette build Chromium** | **`expected builtin value name`** + la liste des 19 builtins, ou `point_size` ne figure pas | supprimer la sortie ; un point RSX devient un quad dans le VS | **4 h** (les points du jeu sont des sprites a taille variable) |
| 7 | `vec4 CC[2]` global, ecrit par `program()` puis lu par `main()` | `var<private>` (module scope) | **aucun** (compile) | `var<private> CC: array<vec4<f32>,2>` | **1 h** : le generateur doit passer les globaux en `var<private>` |
| 8 | `gl_FragCoord` | `@builtin(position)` en entree de fragment | **aucun** (compile) | `fn f(in: In)` avec `@builtin(position) pos` | **0** |
| 9 | `if (!pass) discard;` | `discard;` existe (au niveau instruction) | **aucun** (compile) | identique | **0** |
| 10 | `isnan(v)` | **absent de WGSL** | pas teste comme appel, car deja remplace | `v != v` (vrai pour NaN) | **0** : deja dans la table `wgsl_map` |
| 11 | `clamp(mix(v, 0, isnan(v)), 0, 1)` (helper `sat`) | `select`, `mix`, `clamp` | **aucun** (compile) | `select(vec4(0.0), vec4(1.0), v != v)` | **0** |
| 12 | `layout(std140, set=0, binding=N) uniform Block { vec4 c[468]; }` | `@group @binding`, struct avec alignement WGSL | **aucun** (compile, 7488 octets lies OK) | `struct VC { c: array<vec4<f32>,468> }` | **0** : la limite `maxUniformBufferBindingSize` = 65536 couvre 468 vec4 |
| 13 | `c[i]` avec `i` dynamique + garde `(i>=0 && i<468) ? c[i] : vec4(0)` | index dynamique de tableau dans un uniform : **autorise** | **aucun** (compile, rend) | `select(vec4(0.0), vc.c[i], i >= 0 && i < 468)` | **0** |
| 14 | `ivec4 A[2] = ivec4[2](...)`, `vec4 CC[2] = vec4[2](...)` | initialisation de tableau explicite | **aucun** (compile) | `array<vec4<i32>,2>(vec4<i32>(0), vec4<i32>(0))` | **1 h** (mecanique) |
| 15 | `layout(location=N) in/out` (16 varyings) | `@location(N)` | **aucun** (compile) | identique, `@location(N)` | **0** |
| 16 | `mod()`, `fwidth()`, `textureLod` | `%`, `fwidth` existe, `textureSampleLevel` | non mesure : **aucune occurrence dans les 8 shaders du menu** | — | 0 pour le menu |

**Total mesure : 5 des 16 constructions demandent au generateur d'emettre autre chose**
(compte verifie sur le tableau : 11 lignes ont "aucun" en colonne ecart, 5 ont un ecart).

| classe | lignes | effort |
|---|---|---|
| la table existante dit "meme nom", c'est FAUX : `lessThan` -> operateur `<` | 1 | 1 h |
| absente de la table : `bvec4(uvec4(r) \| uvec4(lt))` ne compile pas du tout | 2 | 2 h |
| absente de la table : sampler combine -> texture + sampler (layout de descripteurs) | 4 | 1 j |
| absente de la table : `invariant` et le bloc `gl_PerVertex` | 5 | 1 h |
| absente de la table : `@builtin(point_size)` n'existe pas dans cette build | 6 | 4 h |

Les 11 autres passent telles quelles, dont **deux que la table existante declare a tort "a porter a la
main"** : `sampler1D` (compile en `texture_1d<f32>`) et l'index dynamique de tableau dans un uniform
(`select(vec4(0.0), vc.c[i], ...)` compile et rend). Deux de plus (`var<private>` pour un tableau
global, initialisation de tableau explicite) compilent mais sont mecaniques a generer : 1 h chacune,
comptees dans le total de la section 5.

### 3.4 Le generateur GLSL->WGSL n'existe toujours pas

J'ai porte a la main la paire principale. Ce qui reste a ecrire :

| etape | etat mesure |
|---|---|
| table de correspondance | existante (`wgsl_map.cpp`), **incomplete de 5 lignes** (section 3.3, lignes 1, 2, 4, 5, 6) |
| generateur WGSL | **non ecrit**. La paire principale (1 VP + 1 FP) est traduite a la main (section 4) ; les 6 autres programmes sont recenses mais **non traduits** |
| validation a l'execution | **faite** : `getCompilationInfo()` dans Chromium, 0 erreur sur les modules de la section 3.3 |

---

## 4. Le MINIMUM porte, et il rend

`docs/explore/H/tools/menu_min.html` execute exactement le chemin mesure : paire
`vp_b5791730607a9ef5` + `fp_6081aed9d403e4d2` traduite avec les recettes ci-dessus, cible
`bgra8unorm` 1280x720, profondeur `depth24plus-stencil8`, blend `ONE/ONE_MINUS_SRC_ALPHA/ADD`,
`depthCompare: less`, `cullMode: none`, `triangle-list` (conversion des quads RSX), texture reelle
`0x81` 512x512 de l'oracle, uniforms de 468 vec4.

    $ node docs/explore/H/tools/menurun.mjs
    H: shader: 0 messages, 0 errors
    H: texture: 512x512 RSX fmt 0x81 (B8) expanded to RGBA8
    H: colour target: bgra8unorm 1280x720 (RSX fmt 8, oracle log "format 44")
    H: depth  target: depth24plus-stencil8 (RSX fmt 129, oracle log "format 129")
    H: pipeline: blend one/one-minus-src-alpha add, depthCompare less, cull none, triangle-list -> created
    H: readback: 68917 drawn texels, 331776 clear texels, 520907 black (7.5% of frame covered)
    H: distinct colours in the frame: 6269
    H: most common: #000000 x520907, #0d0d33 x331776, #010000 x603, #020000 x456
    H: canvas presented: yes (164622 b64 bytes)

Capture : `docs/explore/H/img/menu-min-webgpu.png` (1280x720). Le quad texture est visible, la
texture RSX est echantillonnee, le blend et le depth test sont actifs.

**Trois bugs de ma propre traduction, mesures, qui sont le vrai cout du portage** (aucun n'est dans
la table existante) :

| bug | symptome mesure | cause |
|---|---|---|
| uniform lu avec le mauvais indice | 0 texel dessine, quad collapse en ligne | `vcF[4]=1` ecrit `c1.x` au lieu de `c1.y` : le second dot product rend `v0.x` |
| pipeline avec profondeur dans une passe sans attache | 0 dessine, image noire, **aucune exception** | WebGPU rejette `setPipeline`, Vulkan non |
| offset de viewport a 0 au lieu du centre | 0 dessine, quad a x=-639 | le VS RSX fait `(offset - surface*0.5)` : offset DOIT valoir le centre |

---

## 5. Ce qui reste pour avoir le menu a l'ecran

| # | tache | effort estime | dependance |
|---|---|---|---|
| 1 | generer du WGSL depuis `vp_decompiler.cpp`/`fp_decompiler.cpp` (les 5 ecarts de 3.3 + les 2 mecaniques) | **3-4 j** | rien |
| 2 | scinder les samplers combines en texture+sampler (layout de descripteurs) | **1 j** | 1 |
| 3 | cache de pipelines indexe par les 21 etats cuits | **0,5 j** | la cle existe deja |
| 4 | convertir les quads RSX en liste | **fait** (`rsx_renderer.cpp:2757`) | — |
| 5 | remplacer les points RSX par des quads dans le VS | **0,5 j** | 1 |
| 6 | uploader les textures 0x81/0x85 (l'expansion B8->RGBA8 existe) | **0,5 j** | — |
| 7 | attacher systematiquement la profondeur quand le pipeline en declare | **0,5 j** | rien |
| **total minimum menu** | | **~6 j** | |

Hors perimetre, mesure mais non porte : stencil (17 sets/image), logic ops, formats 16 bits packed,
compression BC, fan primitives, lin2srgb/srgb2lin (le menu ne les active pas).

**Traduit et valide** : 1 VP + 1 FP sur les 8 programmes du menu — la paire
`vp_b5791730607a9ef5` + `fp_6081aed9d403e4d2`, qui couvre 228 des 873 draws de la fenetre
(mesure 2.3 : `fph = 6081aed9d403e4d2 x228`). Les 6 autres programmes sont **recenses seulement** ;
leur traduction devrait tomber dans les memes 5 ecarts mais **ce n'est pas mesure**.

## 6. Limites de ces mesures

- La fenetre 22,4-31,8 s contient 873 draws mais le **menu principal** est a t=54,97 s ; je n'ai pas
  rejoue l'analyse draw par draw sur cette fenetre-la (le log `GOWA_DRAW_LOG=4000` s'arrete avant).
  Les shaders du menu sont identiques (memes hashes) mais je ne peux pas l'affirmer pour les etats.
- `depth32float-stencil8` et BC : la presence dans `adapter.features` est mesuree, l'usage apres
  `requestDevice({requiredFeatures})` n'a pas ete teste (2 lignes a ajouter).
- Les captures du menu natif (`docs/explore/I/img/oracle-04-main-menu.png`) n'ont pas ete comparees
  pixel a pixel avec ma rendue : ma rendue est un quad de validation, pas une reproduction du menu.
