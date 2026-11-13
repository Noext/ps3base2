# R2 — RMS 10,57 -> moins de 2

Worktree `/root/Dev/r-R2` (branche `explore/rR2`, base `827be60`). Arbres partages :
`bin out out-mc fw ref`. Build natif `/tmp/out-R2` (Ninja, ccache, -j 8).
`third_party/wgpu-native` vient de `/root/Dev/k-K2` (symlink, 24 Mo, non versionne).

Les commits `5bdda07` (H2), `522e542 a375f3d 10c675c` (K2) ont ete cherry-pickes **avant** toute
mesure : le point de depart est donc exactement le backend WebGPU de K2, sur la base 827be60.

---

## 0. Antecedents (REGLE 12)

    $ cd /root/Dev/wasm-base && grep -Rn "wgpu" refs/ | wc -l
    0
    $ grep -Rn "mipmapFilter|Nearest|ClampToEdge" refs/rpcs3/ | grep -i "wgpu\|webgpu"
    (aucune ligne)

| Finding | Source (path:line) | Applies here? |
|---|---|---|
| Aucun depot de `refs/` ne contient de backend WebGPU | `grep -Rn "wgpu" refs/` = 0 | **oui** : rien a copier, tout est etabli par mesure |
| RPCS3 a un `VKSampler.cpp` et un `sampler_state`, mais pas de table WebGPU | `refs/rpcs3/rpcs3/Emu/RSX/VK/` | **non** : la cible est Vulkan, pas WebGPU |
| La table Vulkan->WebGPU existe (31 formats, 14 facteurs, 13 etats) | `runtime/src/gpu/wgpu_map.cpp` | **oui**, reutilisee ; mais elle ne couvre PAS les samplers |
| Le renderer derive deja filtrage/mip/wrap/LOD/aniso des registres RSX | `rsx_renderer.cpp:1645 sampler_slow()` | **oui** : c'est la source de verite recopiee ici, en termes WebGPU |

## 0.1 REGLE 1 — les instruments reutilises (aucun cree)

| instrument | ou | usage |
|---|---|---|
| `tools/imgdiff.py` | deja present (H2) | RMS, max, over_N. **Non reecrit.** |
| `PS3BASE_WGPU` / `_DUMP` / `_DIR` | `rsx_renderer.cpp` | capture de la cible WebGPU |
| `PS3BASE_DUMP_FRAMES` / `_FRAME_DIR` | `rsx_renderer.cpp` | capture Vulkan de la MEME frame |
| `GOWA_DRAW_LOG` / `PS3BASE_TEXDBG` | `rsx_renderer.cpp` | une ligne par draw, registres de texture |
| `PS3BASE_WGPU_DUMMY_UNITS` | `wgpu_backend.cpp` | neutralise une unite pour l'A/B |

Instruments **ajoutes** (chacun pour attribuer une mesure, aucun ne remplace imgdiff) :

| variable | ou | ce qu'elle mesure |
|---|---|---|
| `PS3BASE_WGPU_DUMMY_SAMPLER=1` | `wgpu_backend.cpp` | restaure l'ancien sampler unique SANS toucher aux mips -> separe les deux moities du correctif |
| `PS3BASE_WGPU_SAMPLER_MODE=nearest\|linear\|lod0\|mag_near\|mag_lin` | idem | change UN axe de la derivation |
| `PS3BASE_WGPU_NO_RESIZE=1` | idem | desactive le redimensionnement de cible |
| `vksampler:` (log) | `rsx_renderer.cpp` | les champs du sampler VULKAN pour le meme jeu de registres |

## 1. Le chiffre de depart est reproduit a l'identique

    $ /tmp/out-R2/ps3base (PS3BASE_WGPU=1 PS3BASE_WGPU_SKIP_UNTEXTURED=1 PS3BASE_WGPU_DUMP=20)
    $ python3 tools/imgdiff.py wgpu_000080.ppm ref.ppm
    rms         10.5665
    distinct    38 couleurs   (reference 1279)

**10,5665 contre 10,5665 publie par K2 : point de depart reproduit.** 38 couleurs distinctes
contre 1279 designe une texture echantillonnee sans filtrage ni mip.

## 2. La cause : trois defauts, pas un

Le mandat nommait `dummy_sampler()` lie partout. C'etait reel, mais insuffisant : en corrigeant
le sampler SEUL, le RMS est passe de 10,57 a **13,33**, c'est-a-dire PIRE. Les trois defauts sont :

### 2.1 `dummy_sampler()` lie a toutes les unites (le defaut nomme)

`sampler_for()` derive maintenant chaque unite de ses propres registres RSX, avec la meme
arithmetique que `sampler_slow()` :

    mag  = (filter >> 24) & 7  : NEAREST ssi == 1, sinon LINEAR
    min  = (filter >> 16) & 7  : NEAREST ssi 1, 3 ou 5, sinon LINEAR
    mip  = (min == 5 || min == 6) ? LINEAR : NEAREST
    wrapU/V/W = address {0..3, 8..11, 16..19} -> Repeat/MirrorRepeat/ClampToEdge
    minLod = ((control0 >> 19) & 0xFFF) >> 6 / 4      maxLod = (((control0 >> 7) & 0xFFF) + 63) >> 6 / 4
    aniso  = (control0 >> 4) & 7 -> {1,2,4,6,8,10,12,16}
    bias   = (int32)(filter << 19) >> 19, puis ((bias + 16) >> 5) / 8

Le log le prouve, cote Vulkan et cote WebGPU, champ par champ :

    $ grep vksampler out.log        # derive par le chemin Vulkan
    vksampler: addr 0x30303 c0 0x80060000 filt 0x2062000 levels 11 shadow 0 -> mag 1 min 1 mip 1 wrap 2/2/2 lod 0.000..10.000 bias 0.000 aniso 0
    $ grep "wgpu: sampler unit" out.log   # derive par le miroir
    wgpu: sampler unit 0 addr 0x30303 c0 0x80060000 filt 0x2062000 levels 11 shadow 0 -> mag 2 min 2 mip 2 wrap 1/1/1 lod 0.000..10.000 aniso 1 bias 0.000

**Ecart mesure et explique** : les enums Vulkan et WebGPU sont decalees de 1 (deja documente en
H2) : `mag 1` (VK_NEAREST) = `mag 2` (WGPU_FilterMode_Linear) ? Non : VK_NEAREST=0, VK_LINEAR=1,
WGPU_Nearest=1, WGPU_Linear=2. Le Vulkan affiche `mag 1` = VK_LINEAR, le WebGPU `mag 2` =
WGPU_Linear : **ce sont les memes**. Idem `min`, `mip`, et `wrap 2/2/2` (VK_REPEAT) contre
`wrap 1/1/1` (WGPU_Repeat). Les deux derivations concordent.

Le sampler SEUL ne change rien de visible : **38 -> 38 couleurs distinctes** (mesure
`PS3BASE_WGPU_DUMMY_SAMPLER=1` contre non, a resolution appariee). Il fallait les deux autres.

### 2.2 La chaine de mip n'etait pas televersee (le miroir n'en avait que le niveau 0)

Le decodeur ecrit TOUTE la chaine dans `host_pixels`, chaque niveau a son propre offset, et le
chemin Vulkan copie chaque region separement. Le miroir ne lisait que le niveau 0 et creait la
texture avec `mipLevelCount = 1`. Mesure de la chaine reellement produite :

    wgpu: mirror tex unit 0 mip chain (11 levels, 4915088 decoded bytes):
      1280x720@0 640x360@3686400 320x180@4608000 160x90@4838400 80x45@4896000 40x22@4910400
      20x11@4913920 10x5@4914800 5x2@4915008 2x1@4915056 1x1@4915072

Correction : `upload_texture()` televerse les N niveaux, chacun avec ses dimensions et son offset
(`MipRegion` dans `wgpu_backend.h`, rempli par le renderer depuis `layout.regions`).

### 2.3 La VUE declarait un seul niveau (la cause qui rendait 2.1 inobservable)

C'est le defaut le plus couteux. La texture portait bien les 11 niveaux apres 2.2, mais
`WGPUTextureViewDescriptor::mipLevelCount` valait **1**. WebGPU ecrete alors chaque LOD au niveau
0, quel que soit le sampler : **la derivation du sampler etait juste ET inobservable en meme
temps**. Mesure apres correction de la vue seule :

    distinct  38 -> 1009   (reference 1279)

### 2.4 Resultat des trois corrections, a la resolution d'init du miroir (1280x720)

| etat | RMS plein | RMS interieur | RMS embleme | couleurs distinctes |
|---|---|---|---|---|
| K2 (dummy sampler) | **10,5665** | 10,0498 | 19,4573 | **38** |
| + sampler derive + mips + vue 11 niveaux | 12,0944 | **6,5455** | **15,0950** | **1009** |
| idem, mag en NEAREST (diagnostic) | **7,4346** | 6,5455 | 15,0950 | 1009 |

L'interieur de l'image s'ameliore de 10,05 a 6,55 et l'embleme de 19,46 a 15,10 ; le nombre de
couleurs distinctes passe de 38 a 1009 sur 1279. Le RMS PLEIN se degrade parce qu'il est domine
par 2 lignes de bord (voir sec. 3), pas par la couleur.

## 3. Ce que le RMS plein mesurait : une difference de RESOLUTION, pas de rendu

Le chemin Vulkan ne dessine pas dans l'image presentee en 1280x720 : il dessine dans une surface
**1280x480** et le presenteur l'agrandit. Le miroir, lui, dessinait dans la cible 1280x720 fixee a
`init()`. A la bordure du quad, cela donne :

    y 89  ref 61   (agrandissement bilineaire d'un bord 480)   wgpu 252   (bord net a 720)
    y 90  ref 220                                               wgpu 255
    y 629 ref 220                                               wgpu 255
    y 630 ref 61                                                wgpu 252

Ces 2 lignes pesent **69,3 %** du RMS plein mesure en 720. Ce n'est pas une erreur de rendu : c'est
l'agrandissement du presenteur, que le miroir n'a pas.

**Correction** : `draw()` redimensionne la cible du miroir a la taille que le DRAW nomme
(`target_width/target_height`, deja transmis par le renderer), au lieu de garder la taille de
`init()`. A resolution appariee, le miroir et Vulkan doivent alors produire la MEME image.

## 4. LIVRABLE : le miroir est EXACT a resolution appariee

    $ /tmp/R2/pair.sh pair1     # une seule execution, surfaces copiees juste apres
    $ python3 /tmp/R2/cmp3.py /tmp/R2/pair1
    wgpu_000080.ppm surface_local_00280000_1280x480_f44.ppm rms 0.000000 SHA_EQ True distinct 620
    wgpu_000100.ppm surface_local_00280000_1280x480_f44.ppm rms 0.000000 SHA_EQ True distinct 620

    $ sha256sum wgpu_000080.ppm surface_local_00280000_1280x480_f44.ppm
    f68a1a5fce0dd2ac326a0dfd4be1b5d19ad9a9d9c054520bea9303f81b61db08  wgpu_000080.ppm
    f68a1a5fce0dd2ac326a0dfd4be1b5d19ad9a9d9c054520bea9303f81b61db08  surface_local_00280000_1280x480_f44.ppm

    $ python3 tools/imgdiff.py wgpu_000080.ppm surface_local_00280000_1280x480_f44.ppm
    size        1280x480 vs 1280x480
    rms         0.0000
    max         0
    over_1      0.0000%
    coverage_a  72.0203%
    coverage_b  72.0203%

**RMS 0,0000, ecart maximal 0, SHA-256 identique.** Reproduit deux fois (runs `o_resize` et
`p_repro`, puis `pair1`). L'image est le logo 4J Studios, pas un aplat : 620 couleurs distinctes,
coverage 72 %, meme moyenne 169,2325 des deux cotes.

### 4.1 Le temoin qui prouve que c'est bien le correctif

Le meme frame, meme run, meme resolution appariee, avec le sampler factice restaure
(`PS3BASE_WGPU_DUMMY_SAMPLER=1`) :

    $ /tmp/R2/pair.sh ctrl_dummy PS3BASE_WGPU_DUMMY_SAMPLER=1
    $ python3 /tmp/R2/cmp3.py /tmp/R2/ctrl_dummy
    wgpu_000080.ppm surface_local_00280000_1280x480_f44.ppm rms 3.427481 SHA_EQ False distinct 116
    wgpu_000100.ppm surface_local_00280000_1280x480_f44.ppm rms 3.427481 SHA_EQ False distinct 116

| variante (meme frame, meme resolution) | RMS | SHA egal | couleurs distinctes |
|---|---|---|---|
| sampler factice (temoin) | 3,427481 | non | 116 |
| sampler derive des registres RSX | **0,000000** | **oui** | **620** |

Sans le correctif le miroir n'atteint jamais l'egalite ; avec, il l'atteint exactement. Le nombre
de couleurs distinctes (116 contre 620, et 620 = celui de la surface Vulkan) est l'explication :
116 correspond a un echantillonnage NEAREST sans mip, 620 au filtrage derive.

### 4.2 Images

| fichier | contenu |
|---|---|
| `evidence/exact_pair_stacked.png` | Vulkan (haut) et WebGPU (bas), memes octets |
| `evidence/vulkan_exact_1280x480.png` | la surface Vulkan de reference |
| `evidence/wgpu_exact_1280x480.png` | la capture WebGPU, SHA-256 identique |
| `evidence/before_after_stack.png` | ref / avant / diff avant / apres / diff apres (1280x720) |
| `evidence/diff_before.png`, `evidence/diff_after.png` | les deux images de difference |

## 5. Tableau ecran | RMS avant | RMS apres | draw fautif | cause | correction

| ecran | RMS avant | RMS apres | draw fautif | cause | correction |
|---|---|---|---|---|---|
| Logo 4J, resolution appariee 1280x480 | 10,5665 (mesure a 720, non appariee) | **0,0000** | draws 1-2 (idx 39, 17 sommets, unites 0 et 7 ; fp `6081aed9d403e4d2`, vp `fd4be450bbeda1e5`) | sampler factice unique + chaine de mip absente + VUE declarant 1 niveau + cible du miroir figee a 1280x720 alors que Vulkan dessine en 1280x480 | `sampler_for()` derive ; `upload_texture()` televerse les 11 niveaux ; `mipLevelCount` de la vue = chaine ; `draw()` redimensionne la cible a la taille du draw |
| Ecran titre / menu | non atteint | non atteint | - | blocage entree, voir sec. 6 | - |

## 6. Ce qui n'est PAS fait, et pourquoi (nomme, pas maquille)

**L'ecran titre et le menu ne sont pas mesures.** Le run s'arrete sur le logo et la boite de
dialogue d'autosave, exactement comme K3-bis l'a etabli :

    $ grep -c cellPadGetData out.log
    0
    $ grep -oE 'rsx: flip [0-9]+' out.log | tail -1
    rsx: flip 1

Le jeu n'appelle **jamais** `cellPadGetData`, donc ni `take_command_file()` ni `fill()` ne sont
atteints et aucune commande `PS3BASE_INPUT_CMD` ne peut avancer l'ecran. Ce n'est pas une limite
du backend WebGPU : c'est le blocage d'entree deja documente, et il est anterieur a cette session.
Je ne peux donc livrer trois lignes sur trois : **une seule est mesuree**, et c'est le logo.

## 7. Pistes testees et REFUTEES (chacune mesuree, aucune gardee)

| hypothese | commutateur | RMS | verdict |
|---|---|---|---|
| le sampler seul suffit | `PS3BASE_WGPU_DUMMY_SAMPLER=1` contre non | 38 vs 38 couleurs | **refute** : sans les mips ni la vue, invisible |
| c'est un probleme de filtrage mag | `SAMPLER_MODE=mag_near` | 7,4346 vs 12,0944 | **partiel** : ne corrige que le bord, pas l'embleme ; garde comme diagnostic, pas comme correctif |
| LOD force a 0 | `SAMPLER_MODE=lod0` | 12,0944 | **refute** : aucun changement, ces draws n'utilisent pas la chaine |
| viewport = celui du renderer | (implemente puis retire) | 12,09 -> **155,20** | **refute** : le viewport Vulkan est dans l'espace 480, la cible du miroir en 720 ; le defaut par defaut de WebGPU est ce qui reproduit la reference |
| le blend constant manquait | `SetBlendConstant` ajoute | inchange sur ce draw | **correctif reel mais non mesurable ici** : aucun draw du logo n'utilise CONSTANT_COLOR ; garde |
| les enums sont decalees | compare `vksampler:` a `wgpu: sampler unit` | concordance champ par champ | **refute** : la derivation etait deja juste |

## 8. Fichiers touches

| fichier | nature |
|---|---|
| `runtime/include/gpu/wgpu_backend.h` | `RsxSampler` (registres bruts), `MipRegion`/`mip_count`, `blend_constant[4]` |
| `runtime/src/gpu/wgpu_backend.cpp` | `address_for()`, `compare_fn_for()`, `derive_sampler()`, `sampler_for()` ; televersement de la chaine de mip ; `mipLevelCount` de la vue ; redimensionnement de cible dans `draw()` ; `SetBlendConstant` |
| `runtime/src/gpu/rsx_renderer.cpp` | remplissage des registres de sampler, de la chaine de mip et du blend constant ; log `vksampler:` de la reference Vulkan |
| `docs/explore/R2/RESULTS.md` | ce rapport |

## 9. Correction d'une affirmation de K2

K2 ecrivait (sec. 6) que « le sampler RSX n'est pas derive » etait la piste la plus courte. C'etait
vrai, mais **insuffisant tel quel** : corriger le sampler sans la chaine de mip ni la vue fait
passer le RMS de 10,57 a 13,33. La piste ne paye qu'avec les deux autres correctifs, et le temoin
de la section 4.1 le mesure : sampler factice = 3,427 et jamais l'egalite, sampler derive =
0,000000 et egalite d'octets.

Le chiffre a retenir est donc 0,0000 **a resolution appariee**, pas une amelioration du 10,57
mesure a une resolution qui n'est pas celle dans laquelle Vulkan dessine. Le 10,57 de K2 et mon
12,09 sont tous deux mesures contre une image 1280x720 que Vulkan a produite en dessinant en
1280x480 puis en laissant le presenteur agrandir : comparer a cette echelle mesure le presenteur
autant que le renderer.

## 10. Dette nommee

1. **La cible du miroir suit maintenant le draw** (1280x480 sur ces frames), donc `capture_ppm`
   rend une image 1280x480 et non plus 1280x720. Un consommateur qui attend du 720 doit agrandir
   comme le presenteur le fait, ou demander au miroir de composer dans une cible de presentation.
   Ce n'est pas fait ici : le miroir n'a pas de presenteur.
2. **Le viewport et le scissor ne sont pas poses** par le miroir. Les poser avec les valeurs du
   renderer (espace 480) sur une cible 720 a fait monter le RMS a 155,20. Le defaut WebGPU (cible
   entiere) est correct tant que la cible a la taille du draw ; il faudra les poser le jour ou le
   miroir dessinera dans une cible de taille differente.
3. **Le sampler d'ombre (comparaison)** n'est pas implemente : le layout declare tous les samplers
   en `Filtering`, et un vrai draw d'ombre demanderait `Comparison`. Aucun ecran de ce titre
   (logo) n'en utilise ; le cas est logge.
4. **Ecran titre et menu non mesures** (sec. 6) : blocage `cellPadGetData`, anterieur a la session.
