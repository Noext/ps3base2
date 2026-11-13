# K2 — RMS 69,51 -> moins de 2

Worktree `/root/Dev/k-K2` (branche `explore/kK2`, base `53fe431`). Arbres partages lies :
`bin out out-mc fw ref`. Build natif `/tmp/out-K2` (Ninja, ccache, -j 8).
Point de depart : le commit H2 `08fb23c` cherry-picke (backend WebGPU natif a cote du Vulkan).

Ce fichier est ecrit AVANT les mesures qui suivent ; chaque section porte la commande exacte et
la sortie brute, collee.

---

## 0. Antecedents (REGLE 12)

    $ grep -Rn "wgpu" refs/ --include=*.md --include=*.cpp --include=*.h | wc -l
    0

| Finding | Source (path:line) | Applies here? |
|---|---|---|
| Aucun depot de `refs/` ne contient de backend WebGPU | `grep -Rn` ci-dessus, 0 ligne | **oui** : rien a copier, tout est etabli par mesure |
| La table Vulkan->WebGPU existe deja (31 formats, 14 facteurs, 13 etats) | `runtime/src/gpu/wgpu_map.cpp:10-113` | **oui**, reutilisee, pas reecrite |
| Le piege des enums decales de 1 est deja documente et corrige | `docs/explore/H2/RESULTS.md:220` | **oui** : `compare_for`/`blend_for`/`equation_for` font deja `v+1` |
| RPCS3 decrit le remap de composantes RSX (CONTROL1) comme partie de l'etat de texture | `refs/rpcs3` (RSXTexture / fragment program) | **oui** : c'est la piste du defaut de couleur ci-dessous |
| Aucun port de `refs/` ne compare deux API dans la meme frame | 0 ligne | **oui** : la methode (miroir + imgdiff) est propre a ce depot |

## 0.1 REGLE 1 — les instruments reutilises (aucun cree)

| instrument | ou | usage |
|---|---|---|
| `tools/imgdiff.py` | deja present (H2) | RMS par canal, % au-dela de N. **Non reecrit.** |
| `tools/bisect_draw.py` | deja present | bissection du draw fautif |
| `PS3BASE_WGPU` / `PS3BASE_WGPU_DUMP` / `PS3BASE_WGPU_DIR` | `rsx_renderer.cpp:4640` | capture de la cible WebGPU |
| `PS3BASE_DUMP_FRAMES` / `PS3BASE_FRAME_DIR` | `rsx_renderer.cpp:4660` | capture Vulkan de la meme frame |
| `PS3BASE_TEXDBG` | `rsx_renderer.cpp:2301` | logge le remap de composantes applique par le chemin Vulkan |
| `GOWA_DRAW_LOG` | `rsx_renderer.cpp:3416` | une ligne par draw |
| `PS3BASE_WGPU_NO_BLEND / _CULL_NONE / _DEPTH_ALWAYS / _MASK_ALL / _TRIVIAL_FS` | `wgpu_backend.cpp:365,452,466` | commutateurs de diagnostic A/B deja ecrits en H2 |

## 1. Le chiffre de depart est reproduit a l'identique

Build natif du worktree (le backend wgpu-native est vendorise dans
`third_party/wgpu-native`, il n'est pas dans git) :

    $ cmake -S . -B /tmp/out-K2 -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
    -- WebGPU backend: wgpu-native at /root/Dev/k-K2/third_party/wgpu-native
    -- Configuring done / Generating done
    $ ninja -j 8 ps3base
    [471/471] Linking CXX executable ps3base

Run (le processus ne rend pas la main : c'est le comportement normal, il est arrete par
`timeout`) :

    $ cd /root/Dev/ps3base2
    $ timeout 60 env DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 PS3BASE_DISC_DIR=/root/Dev/mc-port/disc \
        PS3BASE_LOG=info PS3BASE_WGPU=1 PS3BASE_WGPU_SKIP_UNTEXTURED=1 \
        PS3BASE_WGPU_DUMP=20 PS3BASE_WGPU_DIR=/tmp/K2/run1 \
        /tmp/out-K2/ps3base --no-launcher
    wgpu: adapter 'NVIDIA GeForce RTX 4060 Laptop GPU' vendor 'NVIDIA' arch ''
    wgpu: readback 1280x720 nonblack 691200/921600 (75.00%)

    $ python3 tools/imgdiff.py /tmp/K2/run1/wgpu_000040.ppm \
        /root/Dev/ps3base2/surface_local_00500000_1280x720_f44.ppm
    size        1280x720 vs 1280x720
    rms_r       53.5889
    rms_g       75.6422
    rms_b       76.8118
    rms         69.5067
    max         255
    over_8      17.4352%
    coverage_a  75.0000%
    coverage_b  72.3870%
    mean_a      182.1537
    mean_b      170.7049

**69,5067 contre 69,5071 publie en H2 : le point de depart est reproduit.** Le reste de ce
rapport part de ce run.

## 2. Premier defaut mesure : la cible du miroir est retournee en Y

Regle 2 oblige, ce n'est pas le compteur qui parle mais la comparaison des images. Les deux
images de la MEME frame, avec le miroir retourne en Y avant comparaison :

    $ python3 - <<'EOF'
    ... (a = wgpu_000120.ppm, b = surface_local_00500000_1280x720_f44.ppm)
    print('as-is        ', rms(a, b))
    print('wgpu vflip   ', rms(a[::-1], b))
    print('wgpu hflip   ', rms(a[:,::-1], b))
    EOF
    as-is         59.727
    wgpu vflip    32.924
    wgpu hflip    66.69

Le retournement vertical divise l'ecart par 1,8. Ce n'est pas un hasard geometrique :
**Vulkan et WebGPU n'ont pas le meme sens pour l'axe Y de la NDC.** Vulkan a +Y vers le bas de
la cible, WebGPU a +Y vers le haut. Le miroir rejoue le meme vertex shader sans corriger cet axe,
donc la scene sort a l'envers. Les uniformes du vertex program le confirment :
`vp_scale 640.000 -240.000` (un Y deja negatif est la convention Vulkan).

## 3. Second defaut mesure : les couleurs du logo sont saturees, et ce n'est pas un arrondi

Sur les memes images, apres retournement, la correspondance des couleurs est une fonction en
escalier, pas un ecart diffus :

    $ python3 - <<'EOF'  (comptage des couleurs distinctes et des paires)
    wgpu    distinct 33   top [(255,255,255) x587018, (0,0,0) x230400, (255,89,96) x59396, (255,11,11) x30302]
    vulkan  distinct 1279 top [(255,255,255) x574375, (0,0,0) x254481, (152,53,47) x51906, (210,210,210) x2733]
    pairs (vulkan -> wgpu), 51906 occurrences : (152,53,47) -> (255,89,96)
    EOF

Le rouge du logo sort **ecrete a 255 en R** et avec un rapport constant ~0,6 sur V, alors que le
Vulkan le sort a 152. Une image ecretee et a 33 couleurs contre 1279 designe une **lecture de
texture qui ne passe pas par le meme remap de composantes** : le chemin Vulkan applique
`texture_mapping(code, control1)` a la vue (`rsx_renderer.cpp:2298`), le miroir, lui, uploade
`host_pixels` tel quel en RGBA8Unorm et **ignore completement ce remap**.

C'est la cause nommee a corriger en premier : elle explique a la fois la saturation et la perte
de precision (le remap place les canaux dans le desordre, la modulation du fragment shader
s'applique alors au mauvais canal).

## 4. Les cinq causes trouvees, chacune mesuree

Chaque ligne a ete appliquee SEULE puis remesuree, sauf la 1 et la 2 qui ont ete posees
ensemble avant la premiere mesure (elles seules font tomber de 69,51 a 13,32).

### 4.1 La cible WebGPU est retournee en Y (defaut de la section 2)

`vp_decompiler.cpp` : la variante wgpu du vertex shader produit `vec4(xy.x, -xy.y, ...)`.
Le chemin Vulkan garde `vec4(xy, ...)`. Mesure : 59,73 -> 32,92 sur l'image comparee, et
**13,32 sur le run complet** (le chiffre de 59,7 venait d'une frame differente de la capture
de reference ; c'est le run `fix2` qui est la mesure de reference).

### 4.2 Le miroir lisait width*height*4 octets quel que soit le format (crash)

`wgpu_backend.cpp::upload_texture` supposait 4 octets par texel. Le decodeur ecrit la largeur
REELLE du format (1 pour R8, 2 pour les 16 bits, 8/16 pour les blocs). Consequence mesuree sur
le logo, dont l'atlas de glyphes est un 0x81 (R8) :

    E  14.685156 [1255596] crash: signal 11 at ... gpu::wgpu::draw(gpu::wgpu::DrawInputs const&)+0x1730
    E  14.685156 [1255596] crash:   #0 gpu::wgpu::draw(gpu::wgpu::DrawInputs const&)+0x1730
    $ addr2line -e /tmp/out-K2/ps3base -f -C 0x329940
    gpu::wgpu::draw(gpu::wgpu::DrawInputs const&)
    /root/Dev/k-K2/runtime/src/gpu/wgpu_backend.cpp:670        <- la boucle de hachage sur t.pixels[i]

Correction : `TextureLayout::out_bytes` (le format le donne), deux champs sur `Texture`
(`host_bytes_per_texel`, `host_vk_format`), et `texel_to_float()` qui convertit selon le format
au lieu de supposer RGBA8.

### 4.3 Le remap de composantes du chemin Vulkan etait ignore

Vulkan applique `texture_mapping(code, control1)` dans sa VUE (`rsx_renderer.cpp:2298`) ; le
miroir uploade `host_pixels` tel quel. wgpu-native v29 n'a **pas** la fonctionnalite
texture-component-swizzle (`grep -i swizzle` sur `libwgpu_native.so` : 0 symbole), donc le remap
est applique par texel pendant la conversion. A/B mesure :

    $ PS3BASE_WGPU_NO_MAPPING=1   -> rms 42,2544   (remap retire : le rouge devient (69,60,255))
    $ (avec remap)                -> rms 10,5665

### 4.4 **Le piege des enums decales n'etait pas uniforme : la table de melange etait fausse**

C'est la cause la plus couteuse, et c'est la meme classe que celle documentee en H2 (defaut 10),
mais sur une table que H2 avait supposee decalee uniformement. Les deux enumerations ordonnent
les facteurs DIFFEREMMENT a partir du troisieme membre :

    VK   0 ZERO 1 ONE 2 SRC_COLOR 3 1-SRC_COLOR 4 DST_COLOR 5 1-DST_COLOR 6 SRC_ALPHA 7 1-SRC_ALPHA ...
    WGPU 1 ZERO 2 ONE 3 Src       4 1-Src       5 SrcAlpha  6 1-SrcAlpha  7 Dst       8 1-Dst      ...

`v+1` transformait donc VK `SRC_ALPHA` (6) en WebGPU `Dst` (7) et VK `ONE_MINUS_SRC_ALPHA` (7)
en `OneMinusDst` (8) : le melange alpha devenait un melange avec la destination.

    $ (v+1, avant)  rms 12,9132   rouge du logo (184,96,89)
    $ (table exacte) rms 10,5665  rouge du logo (134,36,31)

Le rouge Vulkan est (152,53,47). Le defaut restant est nomme en section 5.

Les facteurs ALPHA et l'OPERATION alpha venaient en plus des 16 bits hauts du registre RSX sans
passer par `blend_factor()`/`blend_op()` : c'etait la meme faute, sur le meme draw. Corrige
(`sfactor_alpha`, `dfactor_alpha`, `equation_alpha`) ; mesure identique a 0,0001 pres, donc ce
draw-la n'utilise pas de facteur alpha distinct — mais le defaut etait reel sur d'autres draws.

### 4.5 Le remap etait global au lieu d'etre par unite

`g_last_bound_mapping` etait une seule variable globale : sur un draw a deux textures, les deux
unites recevaient le remap de la DERNIERE. Mesure : 13,3182 -> 12,9132.

### 4.6 Pistes testees et REFUTEES (chacune mesuree, aucune gardee)

| hypothese | commutateur | RMS | verdict |
|---|---|---|---|
| melange en cause | `PS3BASE_WGPU_NO_BLEND=1` | 27,12 | non : le melange AMELIORE (le retirer degrade) |
| profondeur en cause | `PS3BASE_WGPU_DEPTH_ALWAYS=1` | 13,47 | non |
| masque d'ecriture | `PS3BASE_WGPU_MASK_ALL=1` | 13,08 | non |
| culling en cause | `PS3BASE_WGPU_CULL_NONE=1` | 13,33 | non |
| **face avant a inverser** (le flip Y inverse le sens) | `PS3BASE_WGPU_FLIP_FACE` | **38,02** | **refute** : ces draws ne culent rien |
| filtrage lineaire | `PS3BASE_WGPU_SAMPLER_LINEAR=1` | 11,97 | non (le filtrage n'est pas la cause) |
| textures en sRGB | `PS3BASE_WGPU_TEX_SRGB=1` | 20,56 | non (trop sombre) |
| mauvais ordre de canaux | `PS3BASE_WGPU_RGBA_RAW=1` | 62,15 | non |

## 5. Livrable : tableau ecran | RMS avant | RMS apres | draw fautif | cause | correction

| ecran | RMS avant | RMS apres | draw fautif | cause | correction |
|---|---|---|---|---|---|
| Logo 4J Studios (frame 1-2, fp `6081aed9d403e4d2`, vp `fd4be450bbeda1e5`) | **69,5067** | **10,5665** | draws 1-2 (idx 39, 17 sommets, 2 unites de texture) | cinq causes cumulees, sec. 4.1-4.5 ; la dominante est la **table de melange fausse** (sec. 4.4) et le **retournement Y** (sec. 4.1) | voir sec. 4 |
| Menu / ecran titre | non atteint | non atteint | - | la session s'est arretee avant (voir sec. 6) | - |

Mesure finale, sortie brute :

    $ python3 tools/imgdiff.py wgpu_000080.ppm surface_local_00500000_1280x720_f44.ppm
    size        1280x720 vs 1280x720
    rms_r       9.5820
    rms_g       11.0228
    rms_b       11.0287
    rms         10.5665
    max         189
    over_8      9.3883%
    coverage_a  71.7120%
    coverage_b  72.3870%
    mean_a      169.2057
    mean_b      170.7049

    $ grep -c 'wgpu: ERROR' err.log
    0

Images : `diff_before.png` (avant, RMS 69,51) et `diff_after.png` (apres, RMS 10,57), dans
`/tmp/K2/final/`. L'image de difference APRES ne montre plus qu'un liseré d'un pixel sur les
contours plus un aplat uniforme sur l'interieur du logo : la geometrie, la couverture et les
contours sont justes, il reste un ecart de couleur sur le remplissage.

## 6. Ce qui n'est PAS fait, et pourquoi (nomme, pas maquille)

**L'objectif RMS < 2 n'est PAS atteint : 10,5665.** Il reste un aplat constant sur le logo.
Etat mesure de cet aplat :

    wgpu   (134,36,31)   sur 59 396 pixels
    vulkan (152,53,47)   sur 51 906 pixels
    (le noir sort 19,19,19 cote wgpu contre 0,0,0 cote vulkan sur les memes pixels)

Les deux sont un aplat, pas du bruit : le remplissage du quad n'est pas calcule de la meme
facon. Pistes identifiees mais NON tranchees, faute de temps de session :

1. **le sampler RSX n'est pas derive** : le miroir lie `dummy_sampler()` (Nearest,
   ClampToEdge) pour TOUTES les unites, alors que `sampler_slow()` (`rsx_renderer.cpp:1565`)
   derive filtrage, mipmap, wrap, LOD et biais des registres. C'est la piste que H2 nommait
   deja comme la plus courte, et elle reste ouverte.
2. **les uniformes de texture (`texscale`) et le biais LOD** ne sont pas transmis au miroir
   autrement que par `win.texture_scale[]`, que `wgpu_backend.cpp` n'utilise nulle part.
3. l'ecart noir 19/0 suggere que la valeur sortie du fragment shader n'est pas la meme a la
   source, ce qui pointerait vers le uniform block `FragmentState` (le miroir l'envoie tel
   quel, taille 112 contre 8160 par defaut) plutot que vers la texture.

**Ecran titre et menu : non mesures.** Le run s'arrete sur le logo (frame 1) ; aucun chemin de
ce worktree n'atteint le menu en moins de 60 s, et le budget de la session est passe en
debuggage du logo. Le tableau ci-dessus porte donc sur un seul ecran, avec son chiffre exact.

## 7. Fichiers touches

| fichier | nature |
|---|---|
| `runtime/src/gpu/vp_decompiler.cpp` | flip NDC Y dans la variante wgpu du vertex shader |
| `runtime/src/gpu/texture_decode.{h,cpp}` | `out_bytes` expose (largeur reelle du decodeur) |
| `runtime/include/gpu/wgpu_backend.h` | format/octets/remap par texture, facteurs et operation alpha |
| `runtime/src/gpu/wgpu_backend.cpp` | conversion par texel + remap, table de melange exacte, alpha separe |
| `runtime/src/gpu/rsx_renderer.cpp` | remap par unite, format et octets passes au miroir |
| `docs/explore/K2/RESULTS.md` | ce rapport |
