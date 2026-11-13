# I5 — Bilan du tour affichage et entrees

## Les quatre livrables demandes

| livrable | etat | preuve |
|---|---|---|
| ligne du menu | **NON** | thread `(4J) Basic Event Handler` jamais cree |
| capture du menu dans Chromium | **NON** | module boote et flippe, mais `NO_RENDERER` |
| tableau de validation des shaders | **OUI** | 11/11 naga, 11/11 Chromium, 0 rejete |
| comparaison images WebGPU natif / Vulkan | **OUI** | RMS 69,51 ; controle 0,0000 |

## Ce qui a progresse, et qui n'etait pas demande

### Le module boote et flippe enfin dans Chromium

    I 102.448850 [ 10] rsx: flip 1741 (buffer 0) worst 51.0 ms, >50ms 44, >100ms 0
    ps3base-wasm: guest clock +80095600 ticks/s (100.1% of 80MHz realtime)

C'etait le blocage du Jalon 4. Quatre causes, toutes mesurees :

1. `PTHREAD_POOL_SIZE=4` sans `PROXY_TO_PTHREAD` : node etend le pool, le navigateur non
   (`STRICT`), donc le 13e thread echoue et `_gcm_intr_thread` ne demarre jamais.
2. `python3 -m http.server` ne pose pas COOP/COEP : `SharedArrayBuffer` absent, pthreads morts.
   `web/serve.py` existait deja pour cela.
3. `web/worker.js` utilisait `import.meta.url`, qui jette dans un worker classique (celui
   qu'Emscripten instancie en interne). Remplace par `self.location.href`.
4. Le module n'etait pas un module ES6 : `-sMODULARIZE=1 -sEXPORT_ES6=1` ajoutes.

### Le transport des draws est mesure

Piste H4. Deux compteurs **independants** donnent le meme resultat, ce qui est la bonne facon de
compter :

    pushed=56183 bytes=3509842584 state_methods=913232 vtx_bytes=136704
    tex_bytes=3501539200 tex_objects=5708 shader_bytes=6144 draws=1424 clears=1424 flips=1423
    refused=0 geom_unmapped=0 threads=1
    consumed_records=56183 consumed_bytes=3509842584

Et la propriete unique du device, mesuree dans Chromium :

    OWNERSHIP page_device_calls=1 worker_device_calls=0

Run rejoue : `45649 / 2851287480 / refused=0` — les volumes varient avec les frames, la **relation**
est reproductible.

### Le backend WebGPU natif existe et se compare a Vulkan

`wgpu-native v29.0.1.1` vendorise (19 Mo, 8,4 s de rebuild), compile **a cote** de Vulkan. 8/8
programmes du menu passent en SPIR-V.

| mesure 1280x720 | valeur |
|---|---|
| RMS | 69,51 / 255 |
| pixels >8 niveaux | 17,44 % |
| controle (Vulkan vs lui-meme) | **0,0000** |

La piste a **auto-corrige** son premier chiffre (RMS 188,26 a 100 % de couverture, faux : image
d'une seule couleur, prouve en changeant la texture factice de magenta a vert) et **refute sa propre
hypothese** `texscale[u>>2][u&3]`.

## Les deux blocages restants, nommes

### 1. Le thread `(4J) Basic Event Handler`

    cellPadGetData : natif 2549 appels, wasm 0
    input chain 0x2219d8=1 0x2c86a4=1 0x2c97b0=1 0x2c8734=0 0x2c9e24=0 0x2cc6b0=0
    wait-loop sleeps lr=0x2221fc:1198
    octet d'attente 0xCE5AA0, remis a 0 par f_0021d850 (ce thread)

Le wasm cree **8 threads**, le natif **12+**. Le site d'appel du pad (`0x222274`) est en aval de la
boucle d'attente : le jeu n'atteint jamais l'ecran qui lit le pad. Aucune injection d'entree ne peut
donc le piloter — ce n'est pas un probleme d'entree.

### 2. `PS3BASE_NO_RENDERER=1`

Le module ne dessine rien : `grep -c rsx_renderer game.js` = 0. Le hook de readback s'installe
(`hook: true`) mais il n'y a rien a lire. Tant que ce drapeau est la, aucune capture d'image du jeu
n'est possible, dans aucun navigateur.

## L'erreur de methode qui explique beaucoup de cette session

`refs/` etait casse depuis le debut : `wasm-base/refs` n'existait pas et `ps3base2/refs` pointait
vers un repertoire vide. **Tous les `grep -R refs/` ont rendu zero et je l'ai lu comme « aucun
antecedent ».** Repare en I1. Les rapports qui disent « 0 ligne dans refs/ » avant 21:53 sont a
rejouer.