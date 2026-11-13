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

## 4. Ce qui reste a faire (etat a l'instant de l'ecriture)

1. corriger le retournement Y du miroir (uniforme de viewport, cote wgpu seulement) ;
2. porter le remap de composantes dans l'upload du miroir ;
3. remesurer, puis bissecter le premier draw qui diverge encore s'il en reste un ;
4. tableau ecran | RMS avant | RMS apres | draw fautif | cause | correction.
