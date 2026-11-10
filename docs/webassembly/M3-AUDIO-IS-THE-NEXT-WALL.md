# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `cellAudioInit` / `cellAudioPortOpen` / `cellAudioGetPortConfig` sont implémentés côté RPCS3 | `refs/rpcs3/rpcs3/Emu/Cell/Modules/cellAudio.cpp` | oui — la sémantique des ports et de l'index area |
| flOw a une table HLE audio | `refs/flow/src/hle_modules.cpp` | oui — confirme que c'est un module standard |
| Notre `runtime/src/hle/cellAudio.cpp` implémente 7 fonctions HLE | `runtime/src/hle/cellAudio.cpp:239-309` | oui — **mais il n'est pas compilé pour wasm** |

# Ce que j'ai trouvé

## La découverte

En maintenant START, le jeu **quitte le régime de rendu** : 361 frames au lieu de 11 341, et
`draw 0k/s` au lieu de 3k/s. Il passe dans un autre état — puis échoue sur :

    W import: unimplemented cellAudio::cellAudioInit (0x0b168f92)
    W import: unimplemented cellAudio::cellAudioPortOpen (0xcd7bc431)
    W import: unimplemented cellAudio::cellAudioGetPortConfig (0x74a66af0)

**L'état qu'il atteint a besoin d'audio, et l'audio n'est pas dans le build.**

C'est la même classe de défaut que `cellImageDec` au round 31 : un fichier HLE complet,
présent dans l'arbre, **absent du lien wasm**.

## Ce que le jeu importe

    cellAudioInit            cellAudioPortOpen       cellAudioPortStart
    cellAudioQuit            cellAudioPortClose      cellAudioPortStop
    cellAudioOutConfigure    cellAudioOutGetSoundAvailability  cellAudioOutSetCopyControl

Neuf imports. Notre `cellAudio.cpp` en couvre sept ; les deux `cellAudioOut*` manquent
également.

## L'obstacle et pourquoi je n'ai pas abouti

`cellAudio.cpp` dépend de `SDL3/SDL.h` pour ouvrir le device de sortie, et son thread de
pompage utilise `std::chrono::steady_clock`, **qui ne lie pas contre les libs précompilées
d'Emscripten** (mesuré plus tôt dans ce port).

J'ai tenté de porter le fichier : garder la logique des ports, remplacer SDL par un stub, et
remplacer `std::chrono` par `emscripten_get_now()`. **J'ai cassé la structure de directives
du fichier** — les `#ifdef _WIN32` / `#else` / `#if wasm` se sont mal emboîtés — et le build
natif est passé à 10 erreurs.

**J'ai restauré le fichier depuis git.** Le build natif est revenu à 0 erreur et `spurs` passe.
Ce round ne laisse aucun changement de code : il laisse un diagnostic.

## Ce que ça vaut

Le diagnostic est net et actionnable :

1. **Le chemin qui manque est l'audio.** Maintenir START fait entrer le jeu dans un état qui
   initialise l'audio ; l'HLE est absent du lien wasm.
2. **Le fichier existe déjà** et couvre 7 des 9 imports.
3. **Deux dépendances bloquent** : SDL3 (device de sortie) et `std::chrono` dans le thread de
   pompage. Les deux ont des substituts déjà utilisés ailleurs dans ce port.

Le prochain essai doit **éditer le fichier par petites étapes compilées**, pas en une passe :
ajouter l'include conditionnel, compiler ; puis le struct, compiler ; puis le thread,
compiler. Mon erreur a été de faire les trois à la fois et de ne pas recompiler entre elles.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | oui, 55 Mo |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | non |

Le jalon 3 reste ouvert. Ce round identifie le prochain mur — l'audio — avec une preuve
comportementale (le START change l'état et le jeu demande l'audio), et laisse l'arbre propre.
