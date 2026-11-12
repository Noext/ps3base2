# I3 — Le module boote et flippe dans Chromium, mais ne dessine pas encore

## Ce qui marche maintenant dans Chromium (mesure)

    I 102.448850 [ 10] rsx: flip 1741 (buffer 0) worst 51.0 ms, >50ms 44, >100ms 0
    ps3base-wasm: guest clock +80095600 ticks/s (100.1% of 80MHz realtime)
    hook: true

**Le module WebAssembly boote dans Chromium et flippe a 1741** — le blocage signale par H6
(« dans Chromium le vrai module boote mais ne flippe jamais ») est **leve**. Le guest clock est a
**100,1 %** du temps reel.

## Les quatre causes trouvees et corrigees, dans l'ordre

### 1. `PTHREAD_POOL_SIZE=4`, pas de `PROXY_TO_PTHREAD`

C'etait la cause du blocage de H6 : en node le pool s'etend a la demande (donc tout marchait), en
navigateur `PTHREAD_POOL_SIZE_STRICT` s'applique et le 13e thread echoue. `_gcm_intr_thread` ne
demarrait donc jamais. Corrige :

    -sPTHREAD_POOL_SIZE=24 -sPTHREAD_POOL_SIZE_STRICT=2 -sPROXY_TO_PTHREAD=1

### 2. `python3 -m http.server` ne pose pas COOP/COEP

Sans ces en-tetes `SharedArrayBuffer` est absent et les pthreads echouent. `web/serve.py` existe
deja pour cela (`Cross-Origin-Opener-Policy: same-origin`, `Cross-Origin-Embedder-Policy: require-corp`)
et c'est lui qu'il faut utiliser, pas `http.server`.

### 3. `web/worker.js` utilisait `import.meta.url`

Quand Emscripten instancie son worker interne en worker CLASSIQUE, `import.meta` jette
`Cannot use 'import.meta' outside a module`. Remplace par `self.location.href`, qui marche dans les
deux cas.

### 4. Le module n'etait pas un module ES6

`web/worker.js` fait `await import('./game.js')` puis `mod.default(...)`. Sans
`-sMODULARIZE=1 -sEXPORT_ES6=1`, `game.js` est un script global et `mod.default` n'existe pas.
Ajoute. Le module expose desormais `createPs3base`.

## Ce qui ne marche pas : le dessin

    grep -n 'NO_RENDERER' tools/build_wasm.sh -> ligne 56 : -DPS3BASE_NO_RENDERER=1
    grep -c 'rsx_renderer' /tmp/out-fc2/game.js -> 0

**Le build n'a pas de renderer.** Le hook de readback s'installe (`hook: true`) mais il n'y a rien a
lire : `readbacks=0`. Ce n'est pas un defaut du hook, c'est que rien n'est dessine.

C'est exactement la limite que la piste H6 avait declaree honnetement : « la capture livree est un
clear pilote par un compteur du guest, pas une frame ».

## Les livrables demandes ce tour

| livrable | etat |
|---|---|
| ligne du menu | **NON** — cause mesuree : `cellPadGetData` jamais appele, `(4J) Basic Event Handler` jamais cree |
| capture du menu dans Chromium | **NON** — le module flip mais ne dessine pas (`NO_RENDERER`) |
| tableau de validation des shaders | **OUI** — H3 : 11 programmes, 11 valides naga, 11 valides Chromium, 0 rejetes |
| comparaison WebGPU natif / Vulkan | **NON** — H2 en cours |

## Detail du livrable shaders (H3)

`runtime/src/gpu/wgsl_emit.cpp` (956 lignes) : meme decodage d'opcodes que les decompilateurs
existants, sortie WGSL directe, **zero GLSL, zero glslang, zero SPIR-V**. Texture et sampler sont
deux bindings separes.

| programmes | valides naga 30.0.1 | valides Chromium | rejetes |
|---|---|---|---|
| 11 (7 FP + 4 VP) | **11** | **11** (0 erreur) | **0** |

Corpus produit par le titre lui-meme via `GOWA_SHADER_DUMP=1` (etendu, pas double). Les 5 ecarts
GLSL documentes par la piste H sont corriges et testes. 5 bugs reels trouves par les validateurs.

Limite declaree : l'emetteur est cable sur le dump, **pas sur le chemin de rendu**. Lier ces WGSL a
un pipeline WebGPU reste a faire.