# Etat consolide de l'orchestration (12 pistes)

## Ce qui est acquis et verifie

### Le natif fonctionne et depasse la boucle de label (piste I)

L'exit 124 documente pendant des dizaines de rounds etait un probleme de LANCEMENT (DISPLAY absent).
Lancement correct :

    nohup setsid env DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 PS3BASE_DISC_DIR=/root/Dev/mc-port/disc \
      PS3BASE_LOG=info timeout -s KILL 60 ./build/ps3base --no-launcher > /tmp/run.log 2>&1 < /dev/null &

Mesures : Vulkan presente a 60 Hz, shaders 0 failures, capture de surface montrant le TITRE MINECRAFT +
boite d'autosave. Et surtout, la trace native montre :

    I 7.879928 rsx: flip 1 (buffer 0)
    I 8.880988 rsx: flip 61 (buffer 0) worst 19.8 ms
    I 9.882948 rsx: flip 121 (buffer 0) worst 31.2 ms
    I 10.883070 rsx: flip 181 (buffer 0) worst 20.0 ms

**Le natif fait des flips a 60 Hz** : il traverse donc la boucle d'attente de label que le wasm ne quitte
jamais. C'est la preuve que la boucle est franchissable et que le probleme est dans notre stub GPU.

### Chromium + WebGPU fonctionnent (piste F)

    'gpu' in navigator === true
    adaptateur NVIDIA lovelace reel (pas swiftshader, lavapipe inutilise)
    flags : --no-sandbox --enable-unsafe-webgpu --enable-features=Vulkan --use-angle=vulkan
    game.html tourne 120 s, 100,1 % du temps reel 80 MHz, 12 captures
    compilation du module : 441 ms pour 45 415 256 octets
    erreurs console : une seule, favicon 404

Le jeu dans le navigateur atteint `pc=0x002da5b4` — exactement la boucle L_2da590. La limite est donc
le stub GPU, pas la plateforme web.

### La difference de chargement entre natif et wasm (piste E)

2 036 571 evenements natifs (367 211 syscalls, 1 669 349 imports, 11 threads). Differences mesurees :
adresses de chargement des images, ordre, et nombre de threads PPU (11 natif jusqu'a 7,4 s vs 19 wasm).

**Correction que j'apporte moi-meme** : une diffusion intermediaire a avance que le wasm ne charge pas
libgcm_sys. C'est FAUX : `/tmp/base-run.log` montre

    loader: loaded libgcm_sys (out-mc/gen/reloc/libgcm_sys.elf) text 0x33000000-0x3300b730

Le module EST charge en LLE. La comparaison doit porter sur les sequences normalisees, pas sur la presence
des modules.

### La base est saine

d290fda : module 45 405 029 o, 4 tests verts, jeu 152 s, exit 124, boucle usleep visible.

## Ce qui n'est pas encore obtenu

| livrable demande | etat |
|---|---|
| ligne de log du menu en WebAssembly | **absent** |
| capture du menu dans Chromium via WebGPU | **absent** (canvas noir, PS3BASE_NO_RENDERER=1) |
| resultat du test C (FIFO fabrique) | en cours |
| regress.sh natif vert | en cours (piste V) |

## Les pistes

| piste | perimetre | etat |
|---|---|---|
| A | lecteur de FIFO sans rendu en wasm | en cours |
| B | semantique des releases vs RPCS3 | en cours |
| C | test FIFO fabrique | en cours |
| D | threads et memoire partagee | en cours |
| E | trace differentielle | 99 lignes, comparison en cours |
| F | Chromium sans affichage | **TERMINEE, livree** |
| G | WebGPU clear | en cours |
| H | WebGPU etats | en cours |
| I | natif oracle | **percee : le natif marche** |
| J | taille et chargement | mesures faites |
| K | son et entrees | en cours |
| V | verificateur | en cours |

## Le fait decisif du jour

Le natif franchit la boucle de label et fait des flips. Le wasm s'y bloque. La difference n'est ni le
navigateur (F le prouve) ni le manque de modules (verifie). C'est que **notre stub GPU n'ecrit pas le
label** la ou le vrai GPU le fait en reponse aux releases du FIFO.
