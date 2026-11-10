# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| `f_002dc1e4` appelée ~300 000 fois depuis `0x2dc33c` | round 128 | oui |
| Son drapeau `0xbe070c` vaut 0 | round 127 | oui |
| `f_002dc238` lit l'offset **1796** (`0xbe0704`) | `mc_028.cpp` | **oui — le second drapeau candidat** |
| **Les deux valent 0 à t=39..159 s** | mesure | **oui — les deux sont écartés** |

# Les deux drapeaux connus sont à zéro

## La mesure

    [pollflag] t=39s  0xbe0704=0 0xbe070c=0
    [pollflag] t=79s  0xbe0704=0 0xbe070c=0
    [pollflag] t=119s 0xbe0704=0 0xbe070c=0
    [pollflag] t=159s 0xbe0704=0 0xbe070c=0

## Ce que ça élimine

| hypothèse | statut |
|---|---|
| `f_002dc1e4` boucle sur `0xbe070c` | **éliminée** (round 127) |
| `f_002dc238` boucle sur `0xbe0704` | **éliminée** (ce round) |

## Ce que ça implique

La boucle supérieure qui appelle 300 000 fois `f_002dc1e4` **n'est pas gardée par ces deux drapeaux**.

Elle doit l'être par :
1. une **condition de temps** (le jeu attend un délai) — plausible, il y a `usleep` ;
2. un **autre drapeau** non encore identifié ;
3. une **condition de sortie jamais atteinte** dans une boucle dont le compteur ne progresse pas.

## Ce que le code nous dit

`f_002dc238` appelle dans l'ordre :
1. `f_002d9220` (avec l'argument 65)
2. `f_002dc1e4` (qui `usleep`)
3. `f_002dc238` se rappelle

C'est une **boucle de trame** typique : travailler, attendre, recommencer. Un jeu en fait normalement une
par image — 300 000 appels sur 200 s font **1 500 par seconde**, ce qui est trop pour une boucle de trame
à 60 Hz.

## Ce que ça suggère

La boucle tourne **sans jamais présenter d'image** : elle attend une condition qui ne vient pas, et le
`usleep` ne dure que quelques dizaines de microsecondes.

## Ce qui reste

Identifier la condition. Le pc de la boucle est connu ; la condition ne l'est pas.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |
