# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| Le drapeau `0xbe0704` « vaut 0 » | rounds 127-130 | **FAUX — je lisais un octet** |
| Le code lit `ld32` (un mot) | `mc_028.cpp` | **oui — l'erreur de largeur** |
| **L'octet 0 ne dit rien ; le mot bascule 0/1** | mesure | **oui — la correction** |

# Erreur de largeur d'accès

## La mesure, aux deux largeurs

    t=39s   byte@704=0  word@704=1  byte@70c=0  word@70c=0
    t=79s   byte@704=0  word@704=0  byte@70c=0  word@70c=0
    t=119s  byte@704=0  word@704=0  byte@70c=0  word@70c=0
    t=159s  byte@704=0  word@704=1  byte@70c=0  word@70c=0

## Ce que ça montre

| adresse | octet | mot |
|---|---|---|
| `0xbe0704` | **toujours 0** | **bascule 1 / 0 / 0 / 1** |
| `0xbe070c` | toujours 0 | toujours 0 |

Le mot à `0xbe0704` **change**. Mon échantillonnage d'octet ne pouvait pas le voir : les octets de poids fort
d'un mot big-endian valant 1 sont `00 00 00 01`, donc l'octet à l'adresse est `00`.

## Les rounds invalidés

| round | conclusion | statut |
|---|---|---|
| 127 | « le drapeau est 0, la boucle n'est pas prise » | **fausse** |
| 128 | « les deux drapeaux sont à zéro » | **fausse** |
| 129 | « le drapeau lu n'est jamais écrit » | **fausse** |
| 130 | « les setters tournent mais le drapeau ne change pas » | **la contradiction est résolue** |

**La contradiction du round 130 avait une cause simple : une erreur de largeur.**

## Ce que ça signifie

Le drapeau **est vivant**. Il bascule. Donc :

1. la boucle `f_002dc238` **avance** ;
2. le guest **n'est pas bloqué** dans une attente infinie ;
3. les 300 000 `usleep` sont une **cadence normale de boucle de trame**, pas un blocage.

## La leçon de méthode

C'est la **quatrième** erreur d'instrument de cette campagne (rounds 103, 122, 129, 131). Chaque fois, j'ai
conclu d'un instrument qui ne mesurait pas ce que le code utilise.

**Un octet n'est pas un mot.** La source lit `ld32`, il faut lire 32 bits.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |