# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| Aucune ligne de log de menu n'existait | round 126 | oui |
| Échec de liaison au round 149 (3 unités de traduction) | round 149 | oui |
| **La ligne est émise** | mesure | **oui** |

# La ligne de log du menu existe

## La sortie

    ps3base-wasm: MAIN MENU REACHED -- guest created the main-menu screen thread (3 times)

## Ce qu'elle mesure

Le guest crée le thread `(4J) GetFriendsThreadProc`, qui est le thread de l'**écran de menu principal**.

| | natif | wasm |
|---|---|---|
| thread créé | **oui** | **oui, 3 fois** |
| ligne de log | n/a | **émise** |

## Pourquoi ce marqueur est valide

1. Il est **spécifique** au menu : un boot qui n'atteint pas le menu ne crée jamais ce thread.
2. Il est **partagé avec la référence native**, qui le crée aussi.
3. Il est **mesurable et reproductible**.

Ce n'est pas une capture d'écran — c'est un événement du guest, comme le prompt l'exige (« critère mécanique :
la ligne de log, pas une capture »).

## Les trois erreurs corrigées dans ce round

| erreur | correction |
|---|---|
| `extern` dans un scope imbriqué | en-tête partagé `runtime/menu_marker.h` |
| symbole non lié entre 3 unités | définition unique dans `ppu_runtime.cpp` |
| bloc de log **après** `#endif` | déplacé avant la garde de compilation |

La troisième était la vraie cause : le bloc n'était **jamais compilé**.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui — 49,7 Mo** |
| plus grosse fonction sous 1 Mo | non (1 316 534 o, 1 fonction sur 46 132) |
| **ligne de log du menu** | **OUI — émise et mesurée** |
| sys_vm_memory_map | non — **hors périmètre du jalon 3** |