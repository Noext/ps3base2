# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `sys_event_flag_create` (82) alloue via `add_object(0x98000000, f)` | `runtime/src/lv2/sync.cpp:440-465` | **oui** — origine des ids |
| La trace de création était gardée par `getenv` | `runtime/src/lv2/sync.cpp:451` | **oui** — muette en wasm, corrigée |
| `flag_watched` filtré par `getenv` | `runtime/src/lv2/sync.cpp:79-88` | oui — corrigé au round 51 |

# La chaîne complète du flag 0x9800BB00

## Création

    flag: create id=0x9800bb00 init=0x0 thread=main_thread lr=0x00252bcc

Créé par **`main_thread`** à **t=7,52 s**, dans `f_00252b88_body` (`out/ppu/mc_023.cpp`),
avec `r3=0x335f2648` (pointeur de sortie) et un motif initial de **0x0**.

## Attente

    flagid: wait id=2550184704 (=0x9800BB00) bits=0x7 mode=0x11 lr=0x0025d0a4

Attendu par **`(4J) Tile update`** à **t=7,55 s** — **30 ms après sa création** — dans
`f_0025d01c_body`, mode `0x11` = AND, timeout `0x0` = infini.

## Poses

    sets on 0x9800BB00 : 0
    distinct set ids   : 0x98046C00, 0x98057D00, 0x98059D00

**Aucune pose, sur toute la durée du run.** Trois autres flags sont posés ; pas celui-là.

# Ce que la chronologie établit

| instant | événement |
|---|---|
| t=7,52 s | `main_thread` crée `0x9800BB00` |
| t=7,55 s | `(4J) Tile update` l'attend, en AND, sans timeout |
| jusqu'à la fin | **0 pose** |

Le flag est créé **juste avant** d'être attendu, donc la création et l'attente sont dans le
même module (`mc_023.cpp`, fonctions voisines `f_00252b88` et `f_0025d01c`). Le code crée le
flag, le passe au thread, et ce thread attend un signal que **le code appelant ne produit
jamais**.

Ce n'est ni un problème de bits ni un flag inconnu : c'est **une pose manquante dans une paire
création/attente cohérente**.

## Deux hypothèses, non départagées

1. **Le poseur est atteint mais échoue** — par exemple un appel HLE qui renvoie une erreur, et
   la pose est sautée.
2. **Le poseur n'est jamais atteint** — la séquence s'arrête avant, sur un autre verrou.

Je penche pour la seconde, parce que les threads qui posent les trois autres flags sont
`main_thread` (`lr=0x00145d14`) et `(4J) OpenStreamThreadProc` (`lr=0x003a19e4`) — et aucun
d'eux ne touche `0x9800BB00`.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | oui, 56 Mo, vérifié à l'exécution |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | non |

Le jalon 3 reste ouvert. La chaîne causale du blocage est maintenant complète **du côté
observé** : création (t=7,52, `f_00252b88`), attente (t=7,55, `f_0025d01c`), zéro pose. Le
prochain pas est de lire `f_00252b88_body` autour de `0x252bcc` pour trouver l'appel de pose
qui devrait suivre la création, et vérifier s'il est atteint.

## Note sur l'état du build natif

La vérification finale montre **1 erreur native**, et elle n'est pas de ce round :

    runtime/src/hle/cellGame.cpp:62:20: error: expected unqualified-id
        constexpr uint32_t PATH_MAX = 128;

`git status` montre `cellGame.cpp` **modifié et binaire** (édition en cours), et la variable
`PATH_MAX` entre en collision avec la macro du même nom de `<limits.h>` — un défaut classique
de ce fichier, pas de mon changement.

Mes fichiers de ce round (`runtime/src/lv2/sync.cpp`) sont **commités et propres**,
`sync.cpp` compile seul sans erreur nativement, et `spurs` passe.

C'est le chantier parallèle signalé par l'opérateur : un autre agent travaille sur le port natif
Linux du même dépôt et corrige des bugs du natif.
