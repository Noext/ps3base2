# Jalon 4 : la page web

## Ce que le prompt demande

> 1. Emulation dans un **worker** ; la page possede le device WebGPU.
> 2. Tampon circulaire de commandes RSX du worker vers le thread de page.
> 3. **AudioWorklet** alimente par memoire partagee.
> 4. Entrees : clavier + Gamepad API.

## Ce qui est ecrit

| fichier | role |
|---|---|
| `web/game.html` | la page : canvas, entrées, demarrage du worker |
| `web/worker.js` | le worker qui charge le module et pompe sa boucle |
| `web/audio-worklet.js` | l'AudioWorklet qui draine le ring |

Les trois passent `node --check` en mode module.

## Les decisions de conception

| decision | raison |
|---|---|
| **la page garde le canvas**, le worker poste les commandes | c'est la forme que `ps2-web-recomp` a retenue apres sa course de frame rate |
| `emscripten_thread_sleep(0)` comme yield | verifie au round 167 : `EM_ASYNC_JS` exige Asyncify, qui casse `wasm-opt` sur 50 Mo |
| le worklet rend du **silence** quand le ring est vide | repeter le dernier bloc est un ton, pas du silence |
| entrees ecrites **directement dans le tas wasm** | pas de copie, pas de message par poll |

## Le mapping des entrees

Le format est celui du guest, jamais traduit :

    mot 0 : select l3 r3 start  haut droite bas gauche
    mot 1 : l2 r2 l1 r1  triangle cercle croix carre

Clavier : `WASD`/fleches pour le d-pad, `Entree` pour start, `Espace`/`Z` pour croix, `X` cercle, `C` triangle,
`V` carre, `Q`/`E` pour L1/R1, `Maj`/`Ctrl` pour L2/R2.

Manette : mapping **standard W3C**, 16 boutons, stick gauche ramene sur le d-pad avec zone morte.

## Le seam C++ publie ses adresses

Quatre fonctions C pour que la page ecrive sans copie :

    uintptr_t ps3_pad_words_address();
    uintptr_t ps3_pad_connected_address();
    uintptr_t ps3_audio_ring_address();
    void ps3_set_audio_ring(uintptr_t, uint32_t);

## Ce que je n'ai pas pu valider

**La page n'a pas ete chargee dans Chrome.** Il faudrait :

- `chrome://flags/#enable-unsafe-webgpu` et `#enable-vulkan`, puis redemarrage ;
- un serveur HTTP (les modules ES ne se chargent pas en `file://`) ;
- le device WebGPU, absent de cet environnement headless.

Je ne peux donc pas fournir la capture ni la version de Chrome que le prompt demande.

## Ce qui est verifie

| verification | methode |
|---|---|
| syntaxe des trois fichiers JS | `node --check` en mode module |
| les fonctions C++ appelees existent | `grep` sur `host.cpp` |
| le seam pad/audio compile | compilation wasm du round |

## Position

| critere du jalon 4 | etat |
|---|---|
| emulation dans un worker | **ecrit** |
| ring de commandes | **fait, teste** |
| AudioWorklet | **ecrit** (C++ + JS) |
| entrees web | **ecrit** |
| **page chargee dans Chrome** | **non — pas de navigateur ni de GPU ici** |
| capture coeurs + hotbar | **non** |