# R0-quater — RETRACTATION de l'annonce du menu, et ce qui reste vrai

## Ce que je dois retracter

J'ai annonce a `31c4247` et `b18e401` :

    ps3base-wasm: MAIN MENU REACHED -- menu screen thread 1 times, 2 interface threads corroborate
    15-18 threads

**Ce resultat ne se reproduit pas.** Le verificateur l'a dit, et je l'ai rejoue moi-meme avec
provenance explicite (md5 du wasm et taille du pack affiches avant le run) :

    WASM_MD5=43a3c656bfcde1e6   DATA_BYTES=158483841
    run 1 : main menu NOT reached -- menu 0, ui 0 ; threads 12 ; padprobe 21 ; flip 5161
    run 2 : main menu NOT reached -- menu 0, ui 0 ; threads 12 ; padprobe 22
    DISC-VERIFY: PASS (495/495 identiques)

Le log qui portait la ligne de menu (`/tmp/discrun.log`) n'a **ni marqueur MODULE READY ni
RUNNER-UP**, et son `guest base 0x1213fa0` ne correspond a aucun binaire present. **Je ne peux pas
attribuer ce log a un build.** Le verificateur a raison : l'annonce n'est pas soutenue.

C'est la **quatrieme** fois dans cette session que j'annonce un resultat non attribuable. Les trois
precedentes : les 107 885 flips (`grep -aci`), les « 137 methodes » (alias d'histogramme), et
« le jeu ne lit jamais le pad » (sonde sur la mauvaise ligne de compilation).

## Ce qui reste vrai, mesure et reproduit

| fait | mesure |
|---|---|
| le pad **est** lu | `padprobe: 21` puis `22` sur deux runs, avec la sonde sur la bonne ligne |
| les disques sont identiques | `DISC-VERIFY: PASS`, 495/495 |
| le renderer **lie** en wasm | `vk_null.cpp` 58 entrees, module boote, `flip 241` (piste R3) |
| les 4 criteres FIFO | C1/C2/C3 verts mesures par le verificateur (40/40, 4261-4681 flips) |

## R2 : les deux mesures sont exactes, elles portent sur des paires differentes

Le verificateur dit « RMS 11,97, 38 couleurs ». J'ai mesure les fichiers moi-meme :

| paire comparee | RMS | couleurs wgpu / vulkan |
|---|---|---|
| 1280x720 avant correctif | **10,57** | 38 / 1279 |
| 1280x720 apres correctif | **7,43** | 1009 / 1279 |
| **1280x480 exacte (apres)** | **0,00** | **620 / 620** |

Verification independante de la paire exacte :

    sha_a 46422dcc80ad0204     sha_b 46422dcc80ad0204
    IDENTICAL True
    pixels differing 0 of 614400
    RMS 0.0

**R2 dit vrai sur sa paire.** Le verificateur dit vrai sur les siennes. Les images 720 comparent un
Vulkan qui dessine en 480 et laisse le presenter agrandir : elles mesurent le presenter autant que le
renderer. C'est R2 elle-meme qui a identifie et corrige ce biais.

## Les deux defauts de la porte de fusion, mesures par le verificateur

1. `merge-tests.sh` calcule son ROOT depuis sa propre position : appele depuis un arbre qui n'a pas
   `tests/`, il repond `SKIP rsx_fifo_synth` **et compte vert**. Le test C n'est jamais rejoue.
2. Le decompte de fichiers source se fait depuis `merge-base`, pas depuis le diff de la branche :
   `explore/rV` qui n'ajoute qu'un document et 7 PNG est declare « 30 source files touched ».

**Ces deux defauts sont de la meme classe que mes erreurs de compteur.** Une porte qui compte autre
chose que ce qu'elle croit. A corriger avant toute fusion.

## Correction du bareme rdr (C5)

Le verificateur mesure que le bareme natif `rdr` a ete **reecrit a 09:54** (`screen=ALERT flips=1000`)
et les binaires natifs reconstruits. Re-mesure sur l'arbre actuel : `rdr FAIL`, `screen=ALERT 0/7`
**des deux cotes** — la barre n'est pas satisfaite par l'arbre natif lui-meme, car `regress.sh` ne pose
pas `GOWA_FLASH_DIR`. C5 n'est donc ni vert ni une regression de ce tour : le bareme est casse.

## Ce qui manque, inchange

**Aucune image du jeu dans Chromium.** Le chainon manquant est identifie : la page ne draine pas
l'anneau de commandes (26 draws, 85 textures, `refused=1160458` par manque de place dans les 16 Mo).