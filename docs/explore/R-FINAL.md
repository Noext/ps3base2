# R-final — Etat du tour Voie B, sans embellissement

## Les quatre livrables demandes

| livrable | etat | mesure |
|---|---|---|
| **RMS WebGPU natif vs Vulkan** | **OUI** | **RMS 0,0000, SHA256 identique, 0 pixel different sur 614 400, 620 couleurs des deux cotes** |
| capture du logo dans Chromium | **NON** | le renderer lie et flippe, la page ne draine pas l'anneau |
| diff des disques | **OUI** | `DISC-VERIFY: PASS`, 495/495 identiques, 0 manquant / 0 different / 0 en trop |
| premier appel divergent manette | **OUI** | import #9887, `cellGcmGetLabelAddress` `lr=0x002da598` saute — et le pad EST lu (`padprobe` 21/22) |

## R2 : l'oracle WebGPU est exact au bit pres

    sha_a 46422dcc80ad0204    sha_b 46422dcc80ad0204
    IDENTICAL True            pixels differing 0 of 614400            RMS 0.0

Verifie par moi sur les fichiers, pas sur l'annonce. Les trois paires, toutes exactes :

| paire | RMS | couleurs |
|---|---|---|
| 1280x720 avant correctif | 10,57 | 38 / 1279 |
| 1280x720 apres correctif | 7,43 | 1009 / 1279 |
| **1280x480 exacte** | **0,00** | **620 / 620** |

Les paires 720 mesurent le presentateur autant que le renderer (Vulkan dessine en 480 et laisse le
presenter agrandir). C'est R2 elle-meme qui a identifie et corrige ce biais.

### Pourquoi le correctif mande ne suffisait pas
`dummy_sampler()` -> `sampler_for()` applique SEUL **degradait** le RMS de 10,57 a 13,33. Trois
autres causes: chaine de mip jamais uploadee (niveau 0 seul), **la vue declarait `mipLevelCount = 1`**
(WebGPU ramenait tout LOD a 0 : le sampler correct etait juste et inobservable), et cible miroir
figee a 1280x720 alors que Vulkan dessine en 1280x480.

## R1 : la frontiere etait de 60 points d'entree, pas 18

Les 18 de K0-bis etaient la ou `wasm-ld` s'arretait. Mesure sur l'ensemble des objets wasm :
**60 symboles `vk*` non definis, 0 defini**.

Mecanisme qui a rendu le cout faible : des shims inline declares dans la **namespace anonyme de
chaque unite de traduction**, sous le nom Vulkan, donc les **85 sites d'appel et 251 mentions de type
compilent inchanges**. Occultation, pas renommage — 85 editions manuelles seraient 85 occasions de
deplacer l'oracle.

Trois preuves :
1. **Vulkan inchange** : RMS **0,0000** contre le binaire d'avant refactor sur 3 frames ;
2. **les deux backends bootent en natif** : Vulkan 2401 flips ; WebGPU 121 flips, 800 `DrawIndexed`,
   **0 erreur de validation** ;
3. **zero `vk*` non defini dans le chemin GPU wasm** : le renderer passe de 54 a 0.

Limite declaree par R1 : **les pixels WebGPU sont encore noirs**
(`readback 1280x480 nonblack 0/614400`). Deux causes trouvees et corrigees (le miroir H2/K2
dessinait en double ; `cmdClearAttachments` recevait les bits d'aspect Vulkan au lieu du masque RSX,
donc un clear de couleur devenait un clear de profondeur qui effacait la frame). Aucune suffisante :
la parite de pixels est le travail de R2, pas de R1.

## R3 : ou vit le device, tranche par la mesure

| tentative | resultat |
|---|---|
| device cree DANS le wasm | **le callback d'adaptateur ne se declenche jamais** — une promesse JS ne se resout pas pendant que le wasm occupe le thread |
| device TRANSFERE au wasm (`preinitializedWebGPUDevice`) | **`GPUDevice` n'est pas transferable** dans un worker |
| device en JS pur, page ou worker dedie | **marche** |

=> **le device vit dans la PAGE**, l'emulation dans le worker, les draws passent par l'anneau H4.

L'anneau tourne en page : 923 enregistrements, 26 draws, 53 clears, 13 flips, 27 shaders,
85 objets texture (16,4 Mo). **`refused=1160458`** : les 16 Mo de l'anneau sont trop petits.

## R4 : le disque, et la vraie cause du symptome TROPDIR

Les 313 fichiers du disque etaient **deja identiques au bit pres**. Le defaut etait que
`runtime/src/vfs.cpp:86` monte `/dev_hdd0` sur **`vfs/dev_hdd0`** : le pack portait deux copies de
`dev_hdd0` et les fichiers poses a la racine etaient **morts, jamais lus**.

Trois manques changent le comportement de l'invite, mesures : `NPEB01899/PARAM.SFO` (type 2 disparait),
`trophy/NPWR05636_00/` (`status 1` au lieu de 3), `reloc/{libfiber,libnet,librtc}.elf` (contenu
different). Verification automatique en place : le build **refuse de linker** un arbre divergent.
Pack reduit de 397 891 439 a **158 483 841** octets.

## R5 : le pad est lu, mes trois compteurs mentaient

| compteur | valait | pourquoi c'etait faux |
|---|---|---|
| `padprobe` | 0 | `-DPS3BASE_PAD_PROBE` sur la ligne 81 (code PPU) ; `hle/cellPad.cpp` compile ligne 56 |
| `pad: cmd` | 0 | emis par `take_command_file()` — **aucun appelant** |
| « `fill()` non atteint » | — | `pad: queued 600` vient de `load_command_queue()`, appelee uniquement depuis `fill()` |

`-sNODE_HOST_ENV=1` manquait au link : sans lui, `grep -c getenv game.js` = 0 et **toutes** les
variables `PS3BASE_*` sont inertes sous Node.

## R6 : plus de suppression a la main

`tools/wt_gc.py` : dry-run par defaut, supprime seulement si termine + branche fusionnee + arbre
propre + aucune activite recente. Alerte sous 50 Go sans supprimer. Teste : +10 Go, 77 worktrees
gardes avec motif.

## Ce que je retracte

L'annonce `MAIN MENU REACHED` (commits `31c4247`, `b18e401`) **ne se reproduit pas** : 12 threads,
`NOT reached`, deux runs, avec md5 du wasm affiche. Le log d'origine n'a ni `MODULE READY` ni
`RUNNER-UP` et son `guest base` ne correspond a aucun binaire present. **Quatrieme annonce non
attribuable de la session.**

## Ce qui manque, en une phrase

Le renderer lie et flippe, l'anneau porte 26 draws et 85 textures, le device est en page, l'oracle
natif est exact au bit pres — mais **la page ne draine pas l'anneau** et ne peint donc rien.
C'est le seul chainon entre « l'anneau se remplit » et « le canvas se peint ».