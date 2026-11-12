# Orchestration — etat final de la session 12 pistes

## Les quatre livrables demandes

| livrable | etat | preuve |
|---|---|---|
| ligne de log du menu en WebAssembly | **NON OBTENUE** | `MAIN MENU: 0` |
| capture du menu dans Chromium via WebGPU | **NON OBTENUE** | au mieux un clear, et il rend `requestAdapter() null` |
| resultat du test C (FIFO fabrique) | **OBTENU** | 13 PASS / 0 FAIL, `docs/explore/C/RESULTS.md` |
| regress.sh natif vert | **OBTENU** | 3 titres verts, `docs/explore/V/RESULTS.md` |

Conformement a la consigne, voici les RESULTS.md de chaque piste au lieu du menu.

## Ce que la session a reellement debloque

### 1. Le module ne se chargeait pas dans Chromium (corrige, verifie)

`WebAssembly.compile` refusait le module : « module uses a mix of legacy and new exception handling
instructions ». `-fwasm-exceptions` fait emettre a clang la proposition legacy, mais les archives
`wasmexcept` sont bâties avec la neuve. Corrige par `-mllvm -wasm-use-legacy-eh=false`.
Verifie dans le vrai Chromium : `EHOK`. **Toute mesure navigateur anterieure etait invalide.**

### 2. La boucle de label est franchie (piste A, porte et verifie)

| mesure | avant | apres |
|---|---|---|
| pc echantillonne | `0x002da5b4` (usleep de la boucle de label) | **`0x002dc20c`** (boucle de trame) |
| `cellGcmGetLabelAddress(255)` | `0x1fe0`, **non mappe** | **`0x1000ff0`**, mappe |
| ecritures `label[255]` | 0 | **3 336** |
| `usleep` dans la boucle de label | 25 740 | **0** |
| flips / clears soumis par le jeu | 0 / 0 | **2 247 / 2 247** |

Quatre correctifs, chacun valide par une mesure negative : l'indirection du contexte
(`vm::pptr` — c'etait la cause du `call_indirect(0)` qui a occupe 40 rounds), une zone RSX
reellement mappee (`vm::alloc(RsxContext)` echoue, le repli `0xd00000` etait hors pagemap et les
ecritures de label etaient jetees), `get = put = 0x1000` a l'armement, et le semaphore de flip a 1.

### 3. Le natif n'etait pas bloque (piste I)

L'« exit 124 » documente pendant des dizaines de rounds est le code de `timeout` : le processus
etait **vivant** et presentait des images. Il manquait `DISPLAY=:0` et `XDG_RUNTIME_DIR`.
L'oracle atteint le **menu principal a t=54,97 s** (`docs/explore/I/img/oracle-04-main-menu.png`).

### 4. Le premier point de divergence natif/wasm (piste E)

Import **9850** : le natif execute le corps **LLE** de `_cellGcmInitBody`, le wasm notre **HLE**.
Le natif appelle 19 `sys_rsx_*` et cree `_gcm_intr_thread` ; le wasm aucun.

### 5. Deux consignes de l'orchestrateur refutees par la mesure

| consigne | verdict |
|---|---|
| « release = 16 octets, valeur puis zeros » | **FAUX** : 4 octets (`nv47_sync.hpp:62`) ; les 16 octets sont le flip, et ce sont 16 zeros |
| « ui >= 2 prouve le menu » | **FAUX** : `thread_options_load` est cree a t=12,16 s, 42 s avant le menu |

### 6. Le CLEAR etait avale par le stub (corrige)

Dans `rsx_no_display.cpp`, la fenetre texture `[0x1A00,0x2000)` capturait `CLEAR_SURFACE=0x1D94`,
rendant son `case` mort. Corrige.

## Les pistes

| piste | etat | livrable |
|---|---|---|
| A | **TERMINEE** | FIFO reader en wasm, boucle de label franchie |
| B | **TERMINEE** | semantique RPCS3, `tests/rsx_sync.cpp` 60 checks 0 fail |
| C | **TERMINEE** | 13 PASS / 0 FAIL sur un FIFO fabrique |
| D | en cours | reveil atomique |
| E | **TERMINEE** | premier point de divergence = import 9850 |
| F | **TERMINEE** | Chromium + WebGPU, `'gpu' in navigator === true`, 16 captures |
| G | en cours | `requestAdapter()` rend null au 1er appel (piege connu de F) |
| H | **TERMINEE** | vrais shaders du menu rendus en WebGPU : 68 917 texels |
| I | **TERMINEE** | natif = oracle, menu a t=54,97 s |
| J | **TERMINEE** | bug EH trouve, module charge maintenant |
| K | en cours | son et entrees |
| V | **TERMINEE** | regress wasm + natif verts, rejeu des annonces |

## Ce qui reste pour le menu

Le jeu n'est plus bloque : il est dans sa **boucle de rendu**, soumet 2 247 flips et 2 247 clears,
et attend que le stub les consomme. Le drain s'arrete sur un auto-saut a `get 0x1454`.

Il faut donc que le stub **traite** les commandes et **presente** les images — c'est exactement le
travail des pistes G et H, dont le rendu WebGPU fonctionne isolement mais n'est pas encore relie au
flux du jeu.
