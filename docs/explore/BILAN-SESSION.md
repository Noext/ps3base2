# Ce que la session a produit, honnêtement

## Les quatre livrables demandes

| livrable | etat |
|---|---|
| ligne de log du menu en WebAssembly | **NON OBTENUE** |
| capture du menu dans Chromium via WebGPU | **NON OBTENUE** |
| resultat du test C | **OBTENU** : 13 PASS / 0 FAIL |
| regress.sh natif vert | **OBTENU** : 3 titres (minecraft, gta-sa, rdr) |

## Les cinq deblocages reels

### 1. Le module ne se chargeait pas dans Chromium (debloque)

« module uses a mix of legacy and new exception handling instructions ». `-fwasm-exceptions` fait
emettre a clang la proposition legacy, les archives `wasmexcept` sont en neuve. Corrige par
`-mllvm -wasm-use-legacy-eh=false`. Verifie `EHOK` dans le vrai Chromium. **Toute mesure navigateur
anterieure etait invalide.**

### 2. La boucle de label est franchie (debloque)

| mesure | avant | apres |
|---|---|---|
| pc | `0x002da5b4` (usleep de la boucle de label) | **`0x002dc20c`** (boucle de trame) |
| `cellGcmGetLabelAddress(255)` | `0x1fe0`, non mappe | **`0x1000ff0`**, mappe |
| ecritures `label[255]` | 0 | **3 336** |
| flips / clears soumis | 0 / 0 | **2 247 / 2 247** |

Cause trouvee par la piste A : `_cellGcmInitBody` recoit un `vm::pptr` (pointeur **vers** un pointeur).
Ecrire `begin`/`end`/`current` directement faisait passer la base du ring comme contexte — c'etait
la cause du `call_indirect(0)` a `lr=0x005d2c08` qui a occupe 40 rounds.

### 3. Le natif n'etait pas bloque (debloque)

L'« exit 124 » rapporte pendant des dizaines de rounds est le code de `timeout` sur un processus
**vivant qui presentait des images**. Il manquait `DISPLAY=:0` et `XDG_RUNTIME_DIR`.
L'oracle atteint le **menu principal a t=54,97 s**.

### 4. Le premier point de divergence est trouve (debloque)

Index **10122** du flux fusionne import+syscall : le natif execute le corps **LLE** de
`_cellGcmInitBody`, le wasm notre **HLE**. 32 evenements plus loin les deux se resynchronisent :
meme nom, meme `lr`, memes arguments — seule l'origine du corps differe.

### 5. `cellGcmSetFlipHandler` jetait son argument (corrige)

`HLE_FUNC(cellGcmSys, cellGcmSetFlipHandler) { ret(ctx, 0); }` — le pointeur enregistre par le jeu
etait ignore. La piste E avait mesure la consequence : 267 220 attentes consecutives a vide contre
3 985 series de mediane 22 en natif. Le handler est maintenant stocke ET appele sur chaque flip.
Mesure : `cellGcmSetFlipHandler(0x00bbe658)` — exactement l'adresse que E avait identifiee.

## Les deux consignes de l'orchestrateur refutees

| consigne | verdict |
|---|---|
| « release = 16 octets, valeur puis zeros » | **FAUX** : 4 octets ; 16 = flip seulement, et ce sont 16 zeros |
| « ui >= 2 prouve le menu » | **FAUX** : `thread_options_load` cree 42 s avant le menu |

## Les erreurs que j'ai commises et corrigees

| erreur | correction |
|---|---|
| « le log du menu est apparu » | c'etait `main menu NOT reached`, mon `grep -i` a matche le negatif |
| « 25 740 usleep » repete toute la session | la mesure brute donne **903 444 sur 150 s** |
| extraction de corps tronquee sur les wrappers `void f_x(` | borne corrigee |
| `hot pcs` comptait les cases vides d'un anneau | lu comme une boucle a l'adresse 0 pendant 3 rounds |

## Ou en est le jeu

    t=197s pc=0x002dc20c syscalls=529510 exited=0
    gcmfifo: methods=4499617 packets=1999833 draws=49995 clears=0 releases=33333
    cellGcmSetFlipHandler(0x00bbe658)   <- enregistre
    MAIN MENU: 0

Le jeu tient **197 secondes** dans sa boucle de trame, soumet 4,5 M de methodes, sans crash et sans
menu. Il attend un renderer qui consomme ses commandes et ecrive l'evenement de frame `0x00be070c`
— ce que la piste D a nomme comme le verrou restant.

## Ce que le rendu WebGPU a prouve isolement

La piste H a fait tourner les **vrais shaders du menu** (extraits de l'oracle natif) dans Chromium
WebGPU : **68 917 texels, 6 269 couleurs distinctes**, 0 erreur de shader. Le rendu marche ; il
n'est pas relie au flux du jeu.
