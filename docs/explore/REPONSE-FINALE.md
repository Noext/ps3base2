# Reponse finale de l'orchestration

## Les quatre livrables demandes

| livrable | etat | preuve |
|---|---|---|
| ligne de log du menu en WebAssembly | **NON OBTENUE** | `main menu NOT reached -- menu 0, ui 0` |
| capture du menu dans Chromium via WebGPU | **NON OBTENUE** | le menu n'est pas atteint |
| resultat du test C | **OBTENU** | 13 PASS / 0 FAIL, docs/explore/C/RESULTS.md |
| regress.sh natif vert | **OBTENU** | 3 titres verts, docs/explore/V/RESULTS.md |

Je ne presente pas de capture du menu, parce qu'il n'y en a pas. Faire passer autre chose pour le
livrable serait exactement le faux succes que j'ai refuse toute la session.

## Ce qui EST capture dans Chromium via WebGPU (piste G)

Une chaine reelle et verifiee : WebGPU dans Chromium headless, adaptateur NVIDIA lovelace, render
pass loadOp=clear, readback GPU vers PNG 1280x720.

    clear-green-5184-32-labelSeq1.png     md5 14a347c2...
    clear-green-5184-96-labelSeq11.png    md5 ed322b58...

**Deux empreintes differentes pour deux instants du flux** : la couleur suit les labels ecrits par
le runtime ((5,184,32) puis (5,184,96)). Ce n'est pas le menu, c'est la preuve que la chaine
commande -> WebGPU -> pixel -> fichier fonctionne de bout en bout.

Deux pieges mesures par G : page.screenshot() renvoie tout canvas en noir, et la texture canvas ne
se relit pas apres presentation -- il faut rendre la meme valeur deux fois.

## Les cinq deblocages de la session

1. **Le module ne se chargeait pas dans Chromium** : mix of legacy and new exception handling.
   Corrige par -mllvm -wasm-use-legacy-eh=false. Toute mesure navigateur anterieure etait invalide.
2. **La boucle de label est franchie** : _cellGcmInitBody recoit un vm::pptr (pointeur vers un
   pointeur) ; c'etait le call_indirect(0) a lr=0x005d2c08 qui a occupe 40 rounds. Mesure :
   pc 0x002da5b4 -> 0x002dc20c, label[255] ecrit 3 336 fois, 2 247 flips soumis.
3. **Le natif n'etait pas bloque** : exit 124 = code de timeout sur un processus vivant. Il
   manquait DISPLAY=:0. L'oracle atteint le menu a t=54,97 s.
4. **Premier point de divergence** natif/wasm : index 10122, LLE contre HLE de _cellGcmInitBody.
5. **cellGcmSetFlipHandler jetait son argument** : le handler 0x00bbe658 n'etait jamais appele,
   267 220 attentes a vide contre 3 985 series de mediane 22 en natif. Corrige et verifie.

## Les seams prouves dans le navigateur

| seam | mesure | piste |
|---|---|---|
| WebGPU adaptateur | gpu in navigator true, NVIDIA lovelace | F |
| WebGPU rendu | 68 917 texels, 6 269 couleurs des vrais shaders du menu | H |
| WebGPU chaine complete | clear pilote par le flux, 2 PNG differents | G |
| audio | 961 024 trames ecrites, 940 672 drainees (97,9 %), 0 perdue | K |
| entrees | KeyZ -> cellPad b[3]=0x40, Enter -> b[2]=0x08 | K |
| FIFO | 4 499 617 methodes, 1 999 833 paquets | A |

## Les deux consignes de l'orchestrateur refutees par la mesure

| consigne | verdict |
|---|---|
| release = 16 octets, valeur puis zeros | FAUX : 4 octets ; 16 = flip seulement, 16 zeros |
| ui >= 2 prouve le menu | FAUX : thread_options_load cree 42 s avant le menu |

## Les erreurs que j'ai commises et corrigees

| erreur | correction |
|---|---|
| le log du menu est apparu | c'etait main menu NOT reached ; mon grep -i a matche le negatif |
| 25 740 usleep repete toute la session | la mesure brute donne 903 444 sur 150 s |
| extraction de corps tronquee sur les wrappers | borne corrigee |
| hot pcs comptait les cases vides d'un anneau | lu comme une boucle a 0 pendant 3 rounds |
| libgcm_sys absent du wasm | faux, verifie charge en LLE |

## Ou en est le jeu

    t=197s  pc=0x002dc20c  syscalls=529510  exited=0
    gcmfifo: methods=4499617 packets=1999833 draws=49995 clears=0 releases=33333
    cellGcmSetFlipHandler(0x00bbe658)   <- enregistre et appele
    MAIN MENU: 0

Le jeu tient 197 s dans sa boucle de trame, soumet 4,5 M de methodes, ne crashe pas, n'atteint pas
le menu. Il n'est plus bloque : il attend un renderer.

Le verrou restant, nomme par la piste D : l'evenement de frame 0x00be070c n'a aucun ecrivain dans
ce build. Et CLEAR_SURFACE (0x1D94) n'apparait pas dans le flux wasm alors que le natif emet la
sienne a t=8,67 s -- mesure de G.

## Les 12 pistes : toutes ont livre, aucune n'a atteint le menu

| piste | livrable |
|---|---|
| A | FIFO reader, boucle de label franchie |
| B | semantique RPCS3, 60 checks 0 fail |
| C | 13 PASS / 0 FAIL |
| D | parks 45,7x, verrou 0x00be070c nomme |
| E | divergence index 10122 |
| F | Chromium + WebGPU operationnels |
| G | clear pilote par le flux, 2 PNG differents |
| H | vrais shaders du menu en WebGPU |
| I | natif = oracle, menu t=54,97 s |
| J | bug EH trouve et corrige |
| K | audio 97,9 %, entrees jusqu'a cellPad |
| V | regress wasm + natif verts |