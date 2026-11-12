# Cloture de l'orchestration

## Les quatre livrables

| livrable | etat |
|---|---|
| ligne de log du menu en WebAssembly | **NON OBTENUE** (`main menu NOT reached`) |
| capture du menu dans Chromium via WebGPU | **NON OBTENUE** |
| resultat du test C | **OBTENU** : 13 PASS / 0 FAIL |
| regress.sh natif vert | **OBTENU** : 3 titres |

## Les captures Chromium WebGPU qui existent

Deux captures veritables, verifiees independamment (PIL) :

| fichier | contenu | verification |
|---|---|---|
| `docs/explore/G/shots/gpu-clear-headed.png` | canvas entier vert | 754 795 px (0,255,0), centre (0,255,0) |
| `docs/explore/G/img/clear-green-*.png` | deux verts differents | md5 differents a deux instants |

Le readback GPU confirme : `READBACK centre=[0,255,0,255] corner=[0,255,0,255]`.

**Mais aucune n'est le menu.** Le clear vient d'un auto-test explicite (`origin=1`), pas d'une
commande du jeu : ce titre n'emet AUCUN `CLEAR_SURFACE` (0x1D94) en wasm, alors que le natif emet
le sien a t=8,67 s. `clears=0` sur 24,8 M de paquets decodes.

## Les deux pieges WebGPU mesures par G, qui valent pour tout le projet

1. **Headless ne compose pas le canvas WebGPU** : readback et `createImageBitmap` rendent des
   zeros pour les 5 jeux de drapeaux essayes. La capture headed est la seule qui montre la couleur.
   **Toute capture WebGPU headless anterieure est invalide.**
2. **Canvas sans `COPY_SRC`** : usage 16 => pixel (0,0,0,0) ; avec COPY_SRC, usage 17 => (0,255,0,255).

## L'etat final du jeu, verifie sur le build de la base

    t=128s  pc=0x002dc20c  syscalls=350953  exited=0
    cellGcmSetFlipHandler(0x00bbe658)   <- enregistre et appele
    main menu NOT reached -- menu 0, ui 0

197 s mesures plus tot, 128 s sur ce dernier build : le jeu tient, soumet des millions de
methodes, ne crashe pas, et n'atteint pas le menu.

## Une regression que j'ai introduite puis retiree

En portant le `webgpu_bridge.cpp` de G dans la base, le chemin FIFO de la piste A a ete casse :
`pc=0x002da5b4` (retour a la boucle de label) et `cellGcmSetFlipHandler: 0`. J'ai retire le pont
et verifie le retour a `pc=0x002dc20c`. Les deux travaux sont incompatibles tels quels ; les fusionner
demande un travail qui n'a pas ete fait.

## Ce que les 12 pistes ont livre

| piste | livrable cle |
|---|---|
| A | FIFO reader : label[255] ecrit, boucle de label franchie |
| B | semantique RPCS3 des releases, 60 checks 0 fail |
| C | 13 PASS / 0 FAIL sur FIFO fabrique |
| D | parks 45,7x ; verrou nomme : `0x00be070c` sans ecrivain |
| E | divergence index 10122 (LLE contre HLE) |
| F | Chromium + WebGPU : `gpu in navigator true`, NVIDIA lovelace |
| G | chaine commande -> WebGPU -> PNG, 2 pieges mesures |
| H | 68 917 texels des vrais shaders du menu en WebGPU |
| I | natif = oracle, menu a t=54,97 s, capture |
| J | bug EH : le module ne se chargeait pas dans Chromium |
| K | audio 97,9 % draine, entrees jusqu'a cellPad |
| V | regress wasm + natif verts, mes 2 consignes fausses rejetees |

## Ce qui reste pour le menu

Le jeu n'est plus bloque : il est dans sa boucle de rendu et **attend un renderer**. Trois faits
mesures disent precisement quoi faire :

1. l'evenement de frame `0x00be070c` n'a aucun ecrivain (piste D) ;
2. `CLEAR_SURFACE` n'apparait pas dans le flux wasm alors que le natif l'emet (piste G) ;
3. les draws ne sont pas transportes par l'anneau (piste G, non fait).

Le pont WebGPU existe et fonctionne isolement ; il n'est pas fusionne avec le chemin FIFO qui, lui,
debloque la boucle de label. **Fusionner les deux est le prochain travail, et il est identifie.**