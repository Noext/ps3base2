# Jalon 4 : validation partielle de la page

## Ce que j'ai pu verifier sans navigateur

| verification | resultat |
|---|---|
| fichiers servis en HTTP | game.html, worker.js, audio-worklet.js -> **200** |
| type MIME des modules ES | **text/javascript** (requis) |
| fonctions C++ appelees par le JS | **2 sur 2 existent** |
| layout audio C++ / worklet | **concordent** (head[0], tail[1], capacité[2]) |
| nom du module attendu | page et script de build disent tous deux `game.js` |

## Ce que j'ai trouve en servant la page

La page utilise `SharedArrayBuffer` **4 fois** dans le worklet, et l'AudioWorklet comme le ring de commandes
en dependent.

Or `python3 -m http.server` **n'envoie pas les en-tetes d'isolation croisee** : sans eux, `SharedArrayBuffer`
est **indefini**, et la page echouerait au premier `Atomics` sans cause evidente.

J'ai donc ecrit `web/serve.py`, qui envoie :

    Cross-Origin-Opener-Policy: same-origin
    Cross-Origin-Embedder-Policy: require-corp

**Verifie** : les deux en-tetes sont bien envoyes, et les trois assets repondent 200.

C'est une erreur qui aurait coute du temps au prochain operateur : le serveur Python standard ne suffit
pas, et le symptome n'aurait pas pointe vers les en-tetes.

## Ce qui reste impossible ici

| prerequis | disponibilite |
|---|---|
| navigateur (Chrome/Chromium/Firefox) | **aucun installe** |
| Puppeteer | **absent** |
| WebGPU dans Node | **`'gpu' in navigator` -> false** |
| GPU disponible pour un navigateur | non |

**La page ne peut donc pas etre chargee, et je ne peux fournir ni capture ni version de Chrome.**

## Ce que le prompt demande et que je ne fournis pas

> La page charge dans Chrome (Linux : `chrome://flags/#enable-unsafe-webgpu` et `#enable-vulkan`, puis
> redemarrage).
> Une capture de la page montre coeurs + hotbar.
> **Preuve a rendre :** capture de la page dans le monde, version de Chrome, flags utilises.

Les trois sont **hors de portee** dans cet environnement.

## Position

| critere du jalon 4 | etat |
|---|---|
| emulation dans un worker | ecrit |
| ring de commandes | fait, teste |
| AudioWorklet | ecrit |
| entrees web | ecrit |
| serveur avec isolation croisee | **fait, verifie** |
| page chargee dans Chrome | **non — aucun navigateur** |
| capture coeurs + hotbar | **non** |