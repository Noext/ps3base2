# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| Le drapeau `0xbe070c` vaut 0 à t=39..159 s | round 127 | oui |
| **177 663 `usleep` ont deux appelants** | mesure de ce round | **oui — nouveau** |
| `f_002dc1e4` est une enveloppe vers `_body` | `mc_028.cpp:21510-21516` | oui |

# Les deux sources des `usleep`

## La mesure

J'ai instrumenté `sys_timer_usleep` pour enregistrer l'adresse de retour :

    usleepcall: lr=0x002dc33c count  50000   (thread 2)
    usleepcall: lr=0x002dc33c count 300000   (thread 2)
    usleepcall: lr=0x008628a4 count  50000   (thread 15)

| appelant | thread | appels |
|---|---|---|
| `0x002dc33c` → `f_002dc1e4` | 2 (principal) | **~300 000** |
| `0x008628a4` | 15 | **~50 000** |

## Ce que ça établit

Le gros des `usleep` vient bien de `f_002dc1e4` — la fonction dont j'avais lu la boucle au round 127.

Mais le drapeau qu'elle teste vaut **0** à chaque échantillon.

## La contradiction

Deux faits mesurés qui ne s'accordent pas :

1. `f_002dc1e4` est appelée **300 000 fois** ;
2. le drapeau qu'elle teste est **toujours 0**.

Si le drapeau est 0, la fonction **sort** — mais elle est rappelée 300 000 fois. Donc elle est appelée **en boucle depuis l'extérieur**, par `0x2dc33c`, à chaque tour d'une boucle supérieure.

## Ce que ça signifie

Ce n'est pas `f_002dc1e4` qui boucle : c'est **son appelant** (`f_002dc238`) qui la rappelle indéfiniment.

## Ce que je n'ai pas établi

La condition de la boucle **supérieure**. `f_002dc238` lit l'offset **1796** (`0xbe0704`) — un autre drapeau, que je n'ai pas mesuré.

## La mesure suivante

Lire `0x00be0704` en même temps que `0x00be070c`, et voir lequel des deux reste bloquant.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |
