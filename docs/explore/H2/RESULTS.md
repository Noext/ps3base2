# PISTE H2 — Renderer RSX avec backend WebGPU, qui compile en natif

Worktree `/root/Dev/i-H2` (branche `explore/iH2`, base `a538cdc`). Arbres partages lies :
`bin out out-mc fw ref`. Build `/tmp/out-H2`, ccache, `-j 8`.

**Resultat en une ligne** : le backend WebGPU **compile et tourne en natif** a cote du Vulkan
(wgpu-native v29.0.1.1, 0 erreur de validation), il **rejoue les memes draws** et produit une
image de **structure correcte** — RMS **69,51 / 255**, couverture 75,0 % contre 72,4 % au
Vulkan — mais **le contenu des textures n'est pas encore fidele**. Douze defauts bloquants ont
ete trouves et corriges par la mesure (section 4) ; la cause du dernier ecart est identifiee et
nommee (section 5), pas encore corrigee.

---

## 0. Antecedents (REGLE 12)

    $ grep -Rn "wgpu" refs/ --include=*.md --include=*.cpp --include=*.h | wc -l
    0

| Finding | Source (path:line) | Applies here? |
|---|---|---|
| Aucun des 15 depots de `refs/` ne contient de port WebGPU | `grep -Rn` ci-dessus, 0 ligne | **oui** : rien a copier, tout est etabli par mesure |
| Table Vulkan->WebGPU deja ecrite (31 formats, 14 facteurs, 13 etats) | `runtime/src/gpu/wgpu_map.cpp:10-113` | **oui**, reutilisee telle quelle |
| Test wgpu existant, 10 verifications | `tests/wgpu_smoke.cpp:1-400` | **oui** : c'est lui qui prouve que wgpu-native marche ici. Je ne l'ai pas duplique |
| Table GLSL->WGSL (53 entrees) | `runtime/include/runtime/wgsl_map.h` | **non utilisee** : le chemin retenu passe par SPIR-V, pas par WGSL (section 2) |
| Decompilateur GLSL du renderer | `fp_decompiler.cpp`, `vp_decompiler.cpp` | **oui** : c'est la source des shaders, avec deux modifications mesurees |
| `libwgpu_native` deja sur la machine | `/root/ps2-native-pipeline/engine/third_party/wgpu-native/` | **oui** : c'est ce qui evite tout reseau (section 1) |
| Prototype WebGPU deja fait dans Chromium | `docs/explore/H/RESULTS.md` | **oui** : 5 des 16 constructions GLSL y sont mesures ; le chemin natif confirme 2 d'entre elles |

## 0.1 REGLE 1 — l'instrument existait deja

    $ grep -rn -i "wgpu" --include=*.cpp --include=*.h --include=*.txt . | grep -v third_party | wc -l
    242
    fichiers : tests/wgpu_smoke.cpp, tests/wgpu_map.cpp, runtime/src/gpu/wgpu_map.cpp,
               runtime/include/runtime/wgpu_map.h, CMakeLists.txt (+ pipeline_* qui ne sont pas wgpu)

Instruments **reutilises sans en creer** :
- `tests/wgpu_smoke.cpp` — prouve que wgpu-native atteint le GPU. Pas duplique.
- `runtime/include/runtime/wgpu_map.h` — la table de correspondance. Pas dupliquee.
- `PS3BASE_DUMP_FRAMES` / `write_dump` (`rsx_renderer.cpp:4566`) — la capture cote Vulkan. Reutilisee.
- `shader_compiler.cpp` — etendu d'une **variante** (SPIR-V 1.5), pas remplace.

**Un seul instrument nouveau** : `tools/imgdiff.py` (98 lignes). Justification : il n'existait
aucun outil de difference d'image dans le depot (`tools/compare_worlds.py` compare des
*donnees de monde*, pas des pixels ; `tools/pickpixel.py` lit un pixel unique d'un binaire de
geometrie). Le livrable 3 exige une metrique d'image chiffree ; elle doit etre reproductible.

## 1. wgpu-native, pas Dawn : ce qui est deja sur la machine

    $ find / -xdev ... -iname '*wgpu*' -o -iname '*dawn*'
    /root/ps2-native-pipeline/engine/third_party/wgpu-native/lib/libwgpu_native.{a,so}
    /root/Dev/ps2-web-recomp/third_party/wgpu-native/lib/libwgpu_native.{a,so}
    ... (Dawn : uniquement des caches de navigateur, AUCUNE bibliotheque Dawn)

    $ md5sum (les deux copies)
    001b223c982358ba84567356e41598b7  libwgpu_native.a
    ff7768da83740bfb154ce364c6a3b16b  libwgpu_native.so
    (identiques : c'est le meme drop, v29.0.1.1)

**Choix : wgpu-native v29.0.1.1.** Pas par preference — parce que **Dawn n'existe pas sur cette
machine** (aucune `libdawn`, aucun header, seulement des repertoires de cache de Chromium) et
que le reseau n'est pas garanti. wgpu-native est present, en deux copies identiques, avec ses
headers C et une version identifiee.

Vendorise dans `third_party/wgpu-native/` (5,5 Ko de headers + la bibliotheque) :

| element | taille | mesure |
|---|---|---|
| `libwgpu_native.a` | **19 Mo** | `du -sh` |
| `libwgpu_native.so` | **4,6 Mo** | `du -sh` |
| headers `webgpu/` | 5,5 Ko | `du -sh` |
| **temps de build du backend** | **8,4 s** | 1 TU, ccache froid (voir ci-dessous) |

    $ ninja -j 8 ps3base      # apres modification de wgpu_backend.cpp uniquement
    [1/2] Building CXX object .../wgpu_backend.cpp.o
    [2/2] Linking CXX executable ps3base

Le cout de build est celui d'**une** unite de traduction plus l'edition de liens ; la
bibliotheque est precompilee, il n'y a pas de dependance Rust a construire.

    $ ldd /tmp/out-H2/ps3base | grep wgpu
    libwgpu_native.so => /root/Dev/i-H2/third_party/wgpu-native/lib/libwgpu_native.so

## 2. Le backend compile en natif — architecture retenue

**Chemin : SPIR-V passthrough, pas de generateur WGSL.** wgpu-native v29 accepte du SPIR-V
directement, a condition de demander `WGPUInstanceFeatureName_ShaderSourceSPIRV` sur
**l'instance** (il n'existe pas d'equivalent au niveau device) :

    $ nm -C /tmp/out-H2/ps3base | grep -c "gpu::wgpu::"
    46

Le renderer produit deja du SPIR-V via glslang (`shader_compiler.cpp`). Le backend le rejoue
tel quel : **aucun generateur WGSL, aucune deuxieme chaine de shaders a maintenir**. C'est ce qui
rend la comparaison attribuable — une difference de pixel ne peut pas venir de deux ecritures
differentes du meme shader.

**Le miroir, pas le remplacement.** `draw()` du renderer appelle `wgpu::draw()` juste apres
`vkCmdDrawIndexed`, en lui passant ce que le chemin Vulkan vient de decoder :

| entree du miroir | source dans le renderer | pourquoi elle est deja disponible |
|---|---|---|
| vertices float4 | `vdata[16]` (`rsx_renderer.cpp:3090`) | le chemin Vulkan les a decodes dans un ring hote |
| indices | `idx_out` | deja expandes et biaises a `vmin` |
| 4 blocs uniformes | `g_vp_constants`, `vs`, `lits`, `fs` | construits sur l'hote |
| textures decodees | `Texture::host_pixels` (ajout) | copiees de la meme staging que Vulkan |
| SPIR-V | `vp->spirv_wgpu`, `fp->spirv_wgpu` (ajout) | compiles depuis le **meme** GLSL |

**Deux modifications du decompileur, imposees par la mesure** (chacune reproduite en programme
autonome de 20 lignes avant d'etre appliquee) :

1. **SPIR-V 1.5 au lieu de 1.6.** En 1.6, `discard` s'encode `OpTerminateInvocation`, que naga
   ne connait pas :

       $ naga --input-kind spv shader_cache/006ccfdd125687ec.spv out.wgsl
       unsupported instruction TerminateInvocation at Function

   En 1.5 c'est `OpKill`, accepte. `compile_glsl_wgpu()` demande donc 1.5 ; `compile_glsl()`
   (Vulkan) reste en 1.6, inchange.

2. **Samplers separes.** Un `sampler2D` combine compile en variable `OpTypeSampledImage`, que
   naga rejette :

       $ ./spvtest fp_6081aed9d403e4d2_15.spv
       invalid id %108          # %108 = OpTypeSampler, dans une OpTypeSampledImage

   Avec `texture2D` + `sampler` separes, naga accepte. Les decompileurs ont donc un drapeau
   `wgpu_target_` qui emet la forme separee (binding N pour la texture, N+64 pour le sampler).
   Verification sur les 8 programmes du menu :

       $ ./spvtest /tmp/spvtest/conv/*.spv
       SPIRV passthrough: ok=8 bad=0

## 3. Bilan chiffre : WebGPU natif contre Vulkan

Le miroir rejoue les draws dans le meme passage, soumet par frame et relit la cible. Sortie
brute d'un run de 45 s (le processus ne rend pas la main, il est arrete par le timeout : c'est
le comportement normal de ce runtime, la fenetre se ferme mais main ne revient pas) :

    $ cd /root/Dev/ps3base2
    $ timeout 45 env DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 PS3BASE_DISC_DIR=/root/Dev/mc-port/disc         PS3BASE_LOG=info PS3BASE_WGPU=1 PS3BASE_WGPU_SKIP_UNTEXTURED=1         PS3BASE_WGPU_DUMP=40 PS3BASE_WGPU_DIR=/tmp/final-H2         /tmp/out-H2/ps3base --no-launcher
    wgpu: adapter 'NVIDIA GeForce RTX 4060 Laptop GPU' vendor 'NVIDIA' arch ''
    wgpu: readback 1280x720 nonblack 691200/921600 (75.00%)
    wgpu: readback 1280x720 nonblack 691200/921600 (75.00%)
    wgpu: pair binding 32 unit 0 -> view 0x769851096980 (mask 0x81, pixels 0x769850bd4080)
    wgpu: pair binding 39 unit 7 -> view 0x7698510f1ab0 (mask 0x81, pixels 0x76985073fa40)

    $ python3 /root/Dev/i-H2/tools/imgdiff.py /tmp/final-H2/wgpu_000040.ppm         surface_local_00500000_1280x720_f44.ppm
    size        1280x720 vs 1280x720
    rms_r       53.5889
    rms_g       75.6428
    rms_b       76.8123
    rms         69.5071
    max         255
    over_1      17.6087%
    over_2      17.5967%
    over_4      17.4975%
    over_8      17.4352%
    over_16     17.2176%
    over_32     16.8254%
    coverage_a  75.0000%
    coverage_b  72.3870%
    mean_a      182.1526
    mean_b      170.7049

Controle negatif (le meme outil, Vulkan contre lui-meme) :

    rms         0.0000
    max         0

**Lecture de ces nombres.** La couverture (75,0 % contre 72,4 %) et les moyennes (182,2 contre
170,7) sont du meme ordre : le miroir dessine la meme scene, au bon endroit. L'ecart RMS de
69,51 vient du **contenu des fragments**, et les 17,4 % de pixels a plus de 8 niveaux d'ecart
designent une zone precise, pas un desaccord diffus. Les couleurs dominantes le confirment :

    WebGPU  distinct 13  top [(255,255,255) x1253, (0,0,0) x461, (255,89,96) x113, (255,11,11) x59]
    Vulkan  distinct 65  top [(255,255,255) x1150, (0,0,0) x508, (152,53,47) x100, (220,220,220) x6]

Fond blanc et noir : identiques. Le rouge de devant est **brillant en WebGPU (255,89,96)** et
**sombre en Vulkan (152,53,47)** : c'est le texte et le logo du menu, qui viennent d'une texture
et que le miroir echantillonne mal.

### 3.1 Le premier chiffre etait 188,26 — et il etait faux a la baisse

Le premier run donnait `rms 188.2649`, `coverage_a 100.00%`, et une image **d'une seule
couleur uniforme** :

    distinct: 1   top: [((255, 0, 255), 1847)]     # magenta partout

J'ai verifie que cette couleur etait celle de la texture **factice** du miroir en la changeant
de magenta a vert : l'image est devenue verte. Donc chaque fragment lisait le remplacant, pas la
vraie texture.

La cause etait mesurable : **le dernier draw de chaque frame est un quad plein ecran sans aucune
texture** (`mask 0x0`), et il repeignait toute la cible avec la factice par-dessus les draws
corrects.

    wgpu: readback now: last draw fp eb106bdc372694a8 mask 0x0 (total draws 2)

En ecartant ces draws d'un commutateur de diagnostic (`PS3BASE_WGPU_SKIP_UNTEXTURED`), la
structure reelle apparait et l'ecart tombe de **188,26 a 69,51**. Les deux chiffres sont dans ce
rapport ; le second est celui qui decrit le backend, le premier decrivait un draw de nettoyage.

## 4. Defauts trouves et corriges (tous mesures, aucune supposition)

Chacun de ces defauts a produit une **panique du processus** (wgpu-native panique sur erreur non
capturee) ou une image noire sans erreur. Ils sont la vraie difficulte du portage.

| # | defaut | symptome mesure | correction |
|---|---|---|---|
| 1 | `wgpuInstanceWaitAny` avec timeout > 0 | `panicked at src/unimplemented.rs:233: not implemented` — **meme avec `TimedWaitAny` demande** | zerot timeout + `wgpuInstanceProcessEvents` ; reproduit en 20 lignes |
| 2 | attache de profondeur absente d'une passe qui declare une profondeur | `setPipeline` invalide, **tout le command buffer jete** | profondeur toujours attachee |
| 3 | `depth24plus-stencil8` sans `stencilLoadOp` | `Attachment without load` | stencil load/store explicites |
| 4 | `clear()` liberait l'encodeur sans le terminer | image detruite, aucun dessin | un encodeur par frame |
| 5 | `clear()` effacait la couleur **sans lire le masque RSX** | masque 0x3 (profondeur seule) effacait la couleur, 0 pixel | `loadOp` suit le masque |
| 6 | 16 vertex buffers pour un maximum WebGPU de 8 | `Vertex buffer index 8 is greater than ... limit 8` | `requiredLimits.maxVertexBuffers = 16` |
| 7 | tableaux de vertex layouts compresses | `requires vertex buffer 1 to be set` | tableaux denses 0..plus haut slot |
| 8 | bind group incomplet | `BindGroup with ... is invalid` | toutes les bindings du layout remplies |
| 9 | bind group lisant `in.texture[16..19]` | comportement indefini (debordement de tableau) | slots vertex = factice |
| 10 | **enums decales de 1** | `VK_COMPARE_OP_LESS` = 1 devenait `WebGPU Never` = 1 : **aucun fragment ne passe** | conversion = valeur + 1 |
| 11 | texture 2D creee avec hauteur 1 | `Copy of Y 0..720 would overrun ... Y size 1` | hauteur selon la dimension |
| 12 | vue 2D sur le slot 1D (unite 7) | `View dimension D1 doesn't match ... D2` | dimension par unite |

Le defaut **10** est celui qui a coute le plus cher et qui vaut d'etre retenu : les enums
Vulkan et WebGPU ont la meme signification mais **pas la meme numerotation** (WebGPU a un
`Undefined = 0` en tete). Un passage direct transforme silencieusement tous les etats.

## 5. Ce qui reste — nomme, chiffre, non fait

| # | defaut | preuve | ou |
|---|---|---|---|
| A | **le premier draw non texture de chaque frame recouvre tout** : un quad plein ecran sans texture peint la cible avec la factice | image uniforme (magenta) puis structure correcte quand on l'ecarte : RMS 188,26 -> 69,51 | `readback now: last draw fp ... mask 0x0` |
| B | **le contenu des textures est mal echantillonne** : le rouge du menu sort a (255,89,96) au lieu de (152,53,47) | 17,4 % de pixels a plus de 8 niveaux ; couleurs dominantes comparees | section 3 |
| C | `texscale` est declare `vec4 texscale[4]` et indexe `texscale[u>>2][u&3]` dans `tex_coord` | `fp_decompiler.cpp:250` contre `:445` | **refute pour ce programme** : `grep texscale` sur le GLSL du menu ne trouve que la declaration, le shader ne l'utilise pas. Le defaut existe mais n'explique pas cet ecart |

**Ce que A et B disent ensemble** : le miroir sait dessiner la geometrie au bon endroit avec les
bons etats. Ce qui reste faux est le **chemin texture**, et il y a deux maillons : le draw de
nettoyage non texture (A, corrigeable par les etats : un draw sans texture ne devrait pas
couvrir l'ecran, et le chemin Vulkan ne le fait pas non plus — c'est la lecture `mask 0x0` du
miroir qui est en cause, pas le renderer), et l'echantillonnage lui-meme (B).

Piste la plus courte pour B, non appliquee : le miroir cree son sampler avec
`addressMode Repeat` fixe et `lodMaxClamp 32`, alors que le chemin Vulkan derive le sampler des
registres RSX (`sampler_slow()`, `rsx_renderer.cpp:1439`). Les coordonnees du menu sont en
texels non normalises pour l'unite 7 (1D) et normalisees pour l'unite 0 ; le miroir ne fait pas
cette distinction. C'est le prochain endroit a mesurer.

Note importante : **tous** les commutateurs `PS3BASE_WGPU_*` (capture, dump, skip, probes) sont
**inertes** sauf si `PS3BASE_WGPU` est pose. Un build avec le backend lie mais sans la variable
se comporte exactement comme avant — c'est ce qui rend la comparaison A/B honnete.

## 6. Ce qui n'a pas ete casse

### 6.1 Le chemin Vulkan est inchange — mesure

Sans `PS3BASE_WGPU`, le binaire se comporte exactement comme avant :

    $ timeout 70 env DISPLAY=:0 ... PS3BASE_LOG=info /tmp/out-H2/ps3base --no-launcher
    I  ... rsx: renderer: 4706 draws
    I  ... rsx: flip 901 (buffer 0) worst 18.9 ms, >50ms 0, >100ms 0

    $ grep -c wgpu /tmp/regress-H2/run.log
    0

**Zero ligne wgpu** : le backend lie est entierement inerte sans la variable. C'est la
propriete qui rend la comparaison A/B honnete, et elle est verifiee, pas supposee.

### 6.2 Les tests

    $ cd /tmp/out-H2 && ctest -R "memory_map|hook_flags|wgpu_map|memspace|spurs"
    1/5 Test #2: hook_flags .......  Passed
    2/5 Test #3: memory_map .......  Passed
    3/5 Test #5: wgpu_map .........  Passed
    4/5 Test #6: memspace .........  Passed
    5/5 Test #7: spurs ............  Passed

    100% tests passed, 0 tests failed out of 5

### 6.3 Deux defauts de la BASE a538cdc, necessaires pour compiler

La base **ne configurait pas** en natif, et ne linkait pas. Les deux corrections sont marquees
`[H2]` dans le code et ne touchent pas le comportement du runtime.

**a) `tests/sysutil_handlers.cpp` n'existe pas a `a538cdc`** :

    $ git cat-file -e a538cdc:tests/sysutil_handlers.cpp
    fatal: path 'tests/sysutil_handlers.cpp' does not exist in 'a538cdc'
    $ git cat-file -e 4a5a132:tests/sysutil_handlers.cpp && echo exists
    exists

Le `CMakeLists.txt` de `a538cdc` nomme pourtant la cible (6 occurrences). Le fichier est
restaure depuis `4a5a132` ; la cible est desactivee parce que son test depend en plus des
changements `hle/sysutil_hle.h` qui sont sur main.

**b) `ps3base_wasm_stubs.cpp` definit des symboles natifs** : le fichier est globbe dans le
build natif mais contient des definitions reservees au wasm, qui entrent en collision avec
`edge_gpu.cpp` (`edge::gpu::g_active`, en `const bool`), `crash_posix.cpp`,
`jobsys/*.cpp` et `edge_gpu.cpp` (`on_put`). Il manquait aussi `ppu/context.h` et les
includes standard, qui etaient **dans** les blocs `#if` desactives.

Correction : les blocs sont passes sous `#if defined(__wasm__) || defined(__EMSCRIPTEN__)`,
les includes remontes en tete, et `ppu/context.h` ajoute. Les sondes
(`ps3base_ld_probe`, `ps3base_store_probe`, `ps3base_block_trace_at`) restent compilees en
natif — ce sont elles que `reservation.cpp` et `ppu/ops.h` referencent.

**c) `ps3base_store_probe` manquait aux petits binaires de test** : `memory_map`,
`memspace`, `spurs` et `reservation_xproc` linkent `reservation.cpp` **sans**
`ps3base_wasm_stubs.cpp`, donc le symbole etait indefini au link. La reference est declaree
`weak` dans `reservation.cpp` : les petits binaires sautent l'appel (la sonde est inerte), le
runtime complet resout la definition. `memory_map` passe, ce qui etait le symptome.

**d) `spu_bench` ne compile pas** (`edge_ovl_gen.h` absent) — defaut **anterieur**, non
touche, hors perimetre de cette piste.

## 7. Fichiers touches

| fichier | nature |
|---|---|
| `runtime/include/gpu/wgpu_backend.h` | **nouveau** : l'interface du backend (126 l.) |
| `runtime/src/gpu/wgpu_backend.cpp` | **nouveau** : le device, le mirror, la lecture (1 330 l.) |
| `runtime/src/gpu/shader_compiler.{h,cpp}` | + `compile_glsl_wgpu()` (SPIR-V 1.5) ; le chemin Vulkan inchange |
| `runtime/src/gpu/fp_decompiler.{h,cpp}` | + `decompile_fragment_program_wgpu()` (samplers separes) |
| `runtime/src/gpu/vp_decompiler.{h,cpp}` | + `decompile_vertex_program_wgpu()` |
| `runtime/src/gpu/rsx_renderer.cpp` | le miroir a cote de `vkCmdDrawIndexed`, init/clear/capture, `host_pixels` |
| `runtime/src/reservation.cpp` | reference `weak` vers la sonde (link des petits tests) |
| `runtime/src/ps3base_wasm_stubs.cpp` | blocs wasm-only sous `#if`, includes en tete |
| `CMakeLists.txt` | `WGPU_DIR`, la cible backend, la liaison ; `sysutil_handlers` desactive |
| `tools/imgdiff.py` | **nouveau** : la metrique d'image (RMS par canal, % au-dela de N) |
| `third_party/wgpu-native/` | vendorise (v29.0.1.1, 19 Mo .a + 4,6 Mo .so) |
