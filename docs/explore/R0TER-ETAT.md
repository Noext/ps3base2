# R0-ter — Etat du tour : l'oracle est atteint, la capture ne l'est pas

## Les quatre livrables demandes

| livrable | etat |
|---|---|
| RMS WebGPU natif contre Vulkan | **OUI — RMS 0,0000, images byte-identiques (SHA256 egal)** |
| capture du logo dans Chromium | **NON** — renderer lie et flippe, anneau non draine par la page |
| diff des disques | **OUI — 495/495 identiques, 0 manquant, 0 different, 0 en trop** |
| premier appel divergent manette | **OUI — import #9887, `cellGcmGetLabelAddress` saute (et ce n'est PAS ce qui gardait le pad : le pad est lu) |

## L'oracle WebGPU natif est exact (piste R2)

    wgpu_000080.ppm vs Vulkan surface   RMS 0.000000   SHA256 identical: True
    wgpu_000100.ppm vs Vulkan surface   RMS 0.000000   SHA256 identical: True
    f68a1a5f...db08 des deux cotes

Le logo 4J Studios, **620 couleurs distinctes**, identique au bit pres. Contre-epreuve qui attribue
le resultat : meme frame, meme resolution, `dummy_sampler()` restaure -> **RMS 3,427**, 116 couleurs,
SHA different. C'est bien le correctif qui produit l'egalite.

### Les trois causes, et pourquoi le correctif mande ne suffisait pas

| # | cause | correction |
|---|---|---|
| 1 | un seul `dummy_sampler()` lie a toutes les unites | `sampler_for()` derive filtrage, mip, wrap, LOD, anisotropie des registres RSX de l'unite |
| 2 | chaine de mip jamais uploadee (niveau 0 seul) | `upload_texture()` uploade les 11 niveaux a leurs offsets et dimensions reels |
| 3 | **la vue declarait `mipLevelCount = 1`** | WebGPU ramenait tout LOD a 0 : le sampler correct etait a la fois juste et inobservable |
| 4 | cible miroir figee a 1280x720 alors que Vulkan dessine en 1280x480 | `draw()` redimensionne la cible a la taille que le draw nomme |

Le correctif mande (cause 1), applique SEUL, **degradait** le RMS de 10,57 a 13,33. C'est ce qui a
force les causes 2 a 4 a sortir.

### Correction de deux mesures precedentes

Les RMS 10,57 (K2) et 12,09 (R2 premiere passe) etaient calcules contre une image 720 que Vulkan
produisait en dessinant en 480 puis en laissant le presenter agrandir : **cette echelle mesure le
presenter autant que le renderer**. Les 2 lignes de bord faisaient 69 % du residu.

## Le disque est identique (piste R4)

    $ python3 tools/disc_verify.py --target /tmp/disk-r4
    disc_verify: 495 expected, 495 identical, 0 missing, 0 different, 0 extra
    DISC-VERIFY: PASS

**La decouverte qui change la lecture du symptome TROPDIR** : les 313 fichiers du disque etaient
deja identiques au bit pres. Le defaut etait ailleurs — `runtime/src/vfs.cpp:86` monte `/dev_hdd0`
sur `vfs/dev_hdd0`, donc le pack portait **deux** copies de `dev_hdd0` et les fichiers poses a la
racine etaient **morts, jamais lus**.

Trois manques changent le comportement de l'invite, mesures :

| manque | natif | ancien pack |
|---|---|---|
| `NPEB01899/PARAM.SFO` | `cellGameDataCheck(3) -> BROKEN`, type 2 present | `-> none`, type 2 disparait |
| `trophy/NPWR05636_00/` | `registered (29, status 3)` INSTALLED | `status 1` NOT_INSTALLED |
| `reloc/{libfiber,libnet,librtc}.elf` | natif | contenu different |

Verification automatique en place : `tools/build_wasm.sh` **refuse de linker** un arbre divergent
(`DISC_VERIFY=refuse|warn|off`, exit 3, mesure). Le pack passe de 397 891 439 a **158 483 841 o**.

## La manette : R5 a refute mes propres compteurs

Le pad **est** lu. Mesure sur un run unifie : `padprobe: 15`, `MAIN MENU REACHED`, 15 threads,
`flip 3661`, `sceNpTrophyGetRequiredDiskSpace -> 0x0`.

Les trois compteurs qui disaient le contraire :

| compteur | valait | pourquoi c'etait faux |
|---|---|---|
| `padprobe` | 0 | `-DPS3BASE_PAD_PROBE` sur la ligne 81 (code PPU) ; `hle/cellPad.cpp` compile ligne 56 |
| `pad: cmd` | 0 | emis par `take_command_file()` — **aucun appelant** : instrument mort |
| « `fill()` non atteint » | — | `pad: queued 600` vient de `load_command_queue()`, appelee uniquement depuis `fill()` |

Premier ecart structurel reel, nomme : **import #9887**, `cellGcmSys::cellGcmGetLabelAddress`
`lr=0x002da598` cote natif contre `cellSysutil::cellVideoOutConfigure` cote wasm. Un 4e
`cellGcmGetLabelAddress(0xff)` que le wasm saute — et il tombe **18 700 appels avant** le premier
`cellPadGetData`, donc ce n'est pas la condition qui gardait le pad.

## La frontiere du rendu est levee en wasm (piste R3), mais rien n'est dessine

`runtime/src/gpu/vk_null.cpp` : **58 entrees Vulkan en no-op**, generees par script depuis les
typedefs `PFN_vk*` (pas ecrites a la main — une signature fausse lie silencieusement en wasm).
Piege mesure : volk declare les `vk*` comme des **pointeurs de fonction**, pas des fonctions.

Le module **lie** et boote dans Chromium avec le vrai renderer : `rsx: flip 241`.

L'anneau H4 tourne en page :

    STATS {"0":923,"1":16769816,"2":21227050,"4":1248,"6":16366788,
           "8":108544,"9":27,"10":26,"11":53,"13":1160458,"addr":8666112,"cap":16777216}

soit 923 enregistrements, 26 draws, 53 clears, 13 flips, 27 shaders, 85 objets texture (16,4 Mo).
**`refused=1160458`** : l'anneau de 16 Mo est trop petit pour le debit, c'est le premier defaut a
corriger cote page.

Decision mesuree du lieu du device, sur trois sondes :

| tentative | resultat |
|---|---|
| device cree DANS le wasm | **le callback d'adaptateur ne se declenche jamais** (promesse JS non resolvable pendant que le wasm occupe le thread) |
| device TRANSFERE au wasm (`preinitializedWebGPUDevice`) | **`GPUDevice` n'est pas transferable** dans un worker |
| device en JS pur, page ou worker dedie | **marche** |

=> **le device vit dans la PAGE**, l'emulation dans le worker, les draws passent par l'anneau.

## Ce qui manque, en une phrase

Le renderer lie et flippe, l'anneau porte 26 draws et 85 textures, le device est en page — mais la
page **ne draine pas l'anneau** et ne dessine donc rien. C'est le chainon entre « l'anneau se remplit »
et « le canvas se peint » qui n'est pas fait.