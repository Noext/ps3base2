# Etat consolide des cinq jalons

Ce document rassemble ce qui est **fait et prouve**, ce qui est **ecrit mais non valide**, et ce qui est
**bloque**, avec pour chaque ligne la preuve correspondante.

## Jalon 1 -- runtime sans mprotect

**Complet.** Verifie nativement des le debut du portage.

## Jalon 2 -- backend WebGPU natif

| etape | etat | preuve |
|---|---|---|
| choix wgpu-native vs Dawn | **fait** | wgpu-native v29 dans `third_party/` |
| table de formats | **fait, testee** | `tests/wgpu_map.cpp` -- 11 checks, 0 failures |
| cle de pipeline | **fait, testee** | `tests/pipeline_key.cpp` -- 17 checks, 0 failures |
| cache de pipelines | **fait, teste** | `tests/pipeline_cache.cpp` -- 15 checks, 0 failures |
| table GLSL -> WGSL | **fait, testee** | `tests/wgsl_map.cpp` -- 53 entrees, 0 failures |
| renderer porte (55 entrees) | **non** | -- |
| generateur WGSL | **non** | -- |
| nombre de pipelines sur 60 s | **bloque** | exige le binaire natif |
| temps CPU du thread de rendu | **bloque** | idem |

## Jalon 3 -- wasm jusqu'au menu

| critere du prompt | seuil | mesure | verdict |
|---|---|---|---|
| taille du module | < 80 Mo | **49,7 Mo** | **PASSE** |
| temps de compilation | a rendre | **51,23 s** | **rendu** |
| pic RSS | a rendre | **1 552 184 ko** | **rendu** |
| nombre de fonctions | a rendre | **49 018** | **rendu** |
| plus grosse fonction | < 1 Mo | **1 358 012 o** | **echoue** |
| ligne de log du menu | existe | **emise** | **PASSE** |

    ps3base-wasm: MAIN MENU REACHED -- menu screen thread 3 times, 3 interface threads corroborate

La plus grosse fonction est `f_0070d138` (`out/ppu/mc_071.cpp`, 2 266 097 octets de source). **Non
decoupable** : 89 % du corps dans un seul segment, 0 marqueur de dispatch, 4 pistes fermees.

## Jalon 4 -- page web

| element | etat | preuve |
|---|---|---|
| ring de commandes | **fait, teste** | `tests/command_ring.cpp` -- 0 failures |
| boucle qui cede | **fait, mesure** | `emscripten_thread_sleep(0)` ; run de 200 s, 0 crash |
| ring audio | **fait, teste** | `tests/audio_ring.cpp` -- 0 failures |
| entrees web | **seam C++ fait** | 2 fonctions verifiees, JavaScript ecrit |
| page + worker + worklet | **ecrits** | `web/` -- les 3 passent `node --check` |
| serveur avec isolation croisee | **fait, verifie** | `web/serve.py` -- en-tetes COOP/COEP confirmes |
| page chargee dans Chrome | **non** | aucun navigateur installe |
| capture coeurs + hotbar | **non** | idem |

## Jalon 5 -- performance

| metrique | natif | wasm |
|---|---|---|
| methodes RSX | 1 745k/s | **645k/s** |
| occupation FIFO | 78 % | **100 %** |
| sommeil oisif | 173/s | **0/s** |
| horloge guest | temps reel | **100,0 %** |

Goulots nommes : decodage des methodes, absence de sommeil, attente GPU non mesurable.

**fps dans le monde : non mesure** -- le wasm n'atteint pas le monde.

## L'inventaire

| | nombre |
|---|---|
| documents de mesure | **203** |
| fichiers de test source | 27 |
| binaires de test construits | 7 |
| assets web | 4 |
| outils de build | 3 |

## Ce que les mesures ont etabli

Dix pistes fermees par la mesure, pas par l'intuition : lenteur, ressources absentes, HLE manquant, pool de
threads, fibres, filtre de table, `-ffunction-sections`, `--gc-sections`, compilation selective (10 % de
gain), decoupage de la geante (89 % dans un segment).

## Les trois blocages, nommes

| blocage | consequence |
|---|---|
| **binaire natif ne demarre pas** | 4 mesures du prompt inaccessibles (pipelines, CPU rendu, fps, compteur d'etat) |
| **aucun navigateur** | validation visuelle du jalon 4 impossible |
| **`f_0070d138` non decoupable** | 1 critere du jalon 3 echoue, defaut structurel du codegen |

Les deux premiers sont **exterieurs au travail** : un build partage avec un autre agent, et un environnement
sans GPU. Le troisieme est **demontre** et documente.