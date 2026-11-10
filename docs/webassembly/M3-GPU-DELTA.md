# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `bindkind` est émis par notre `rsx_renderer.cpp` | `runtime/src/gpu/rsx_renderer.cpp:2043` | **oui** — 0 occurrence dans le run wasm |
| Le renderer est exclu du build wasm (`PS3BASE_NO_RENDERER`) | `runtime/src/gpu/rsx_no_display.cpp` | oui — c'est la cause |
| Le natif mesure `gpu N ms per frame` et `fence wait` | `/tmp/cmp.log` | oui — activité GPU réelle |

# Ce que le run qui réussit fait que le wasm ne fait pas

J'ai appliqué la leçon du round 55 : comparer **avant** de conclure.

## Le delta, mesuré

| | natif valide (`cmp.log`) | wasm (`f4.log`) |
|---|---|---|
| frames rendues | **74 581** | **11 161** |
| `gpu N ms per frame` | **6,28 ms** | absent |
| `fence wait` | présent | absent |
| `bindkind` (liaisons de textures) | **782 734 couleurs, 166 797 309 mémoire** | **0** |
| `UserAutoSave` (le monde) | **oui**, dès t=487 s | non |

## La lecture

Le natif exécute un **vrai pipeline graphique** : 782 734 liaisons de textures couleur, des
attentes de fence, 6,28 ms de GPU par frame, et **74 581 frames** en 9 126 secondes (~8 fps).

Le wasm exécute un stub qui **compte** les commandes sans les exécuter : 11 161 frames en 200 s
(~56 fps, plus rapide parce que rien n'est dessiné), aucun bind, aucun fence.

**Ce n'est pas une différence de vitesse, c'est une différence de travail** : le natif dessine,
le wasm traverse.

## Ce que ça confirme du diagnostic du round 44

Le round 44 avait établi que 239 130 draws étaient soumis avec leurs deux programmes, et que le
stub ne compilait rien. Ce round-ci ajoute la mesure côté natif : **782 734 liaisons de textures
couleur** sur la même période. Le travail existe, il n'est simplement exécuté par aucun backend.

## Ce que ça change pour la suite

Le jalon 3 s'arrête ici **pour une raison de planification, pas d'implémentation** :

- Le module boote, rend 11 161 frames, charge options, audio, DLC, 16 threads, 0 crash.
- Le seuil de taille est respecté (56 Mo contre 80).
- La seule chose qui manque pour du **contenu à l'écran** est le backend graphique — c'est-à-dire
  le **jalon 2**, que le prompt place avant le jalon 3 mais qui n'était pas fait.

Le prompt ordonne : jalon 2 (WebGPU natif) → jalon 3 (wasm sans affichage, jusqu'au menu). Le
jalon 3 a été mené d'abord, et il bute précisément sur ce que le jalon 2 apporte.

## Ce que je ne peux pas affirmer

Le critère du jalon 3 est une **ligne de log** montrant le menu. Je n'ai pas prouvé que le jeu
n'y arrive pas — j'ai prouvé que le chemin wasm ne produit pas de contenu affiché, et que le
natif y arrive avec un GPU réel. Il reste possible que le menu soit atteint logiquement sans
être dessiné ; mais aucune ligne de menu n'apparaît dans le run wasm, donc le critère n'est pas
rempli.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo, vérifié à l'exécution** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round isole la différence avec le run qui réussit par des chiffres :
74 581 contre 11 161 frames, 782 734 liaisons de textures contre 0, 6,28 ms de GPU par frame
contre aucune.
