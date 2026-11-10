# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| Le jeu progresse au-delà de 200 s, 19 threads à t=367 s | round 109 | oui |
| Le wasm à t=444 s : draw 3k/s, flip 91,7 % | long2.log | oui |
| **Le natif reste à 5k/s draw jusqu'à t=411 s, puis bascule à 68k/s vers t=511 s** | cmp.log | **oui — la découverte** |

# Le wasm est dans le régime natif d'avant la bascule

## La chronologie native

| temps | draw | flip | régime |
|---|---|---|---|
| t=103 s | **5k/s** | 43,7 % | chargement |
| t=210 s | **5k/s** | 97,5 % | chargement |
| t=310 s | **5k/s** | 96,9 % | chargement |
| t=411 s | **4k/s** | 97,3 % | chargement |
| **t=511 s** | **68k/s** | 32,4 % | **JEU** |
| t=611 s | 70k/s | 31,7 % | jeu |
| t=912 s | 70k/s | 29,3 % | jeu |

## Où en est le wasm

| temps | draw | flip |
|---|---|---|
| t=444 s | **3k/s** | **91,7 %** |

**Le wasm est exactement dans le régime natif de t=210-411 s.**

# Ce que ça change

Le wasm **n'est pas bloqué** : il est dans la **phase de chargement**, comme le natif jusqu'à t=411 s.

La bascule native se produit entre **t=411 et t=511 s**. Le wasm est à t=444 s — il n'a pas encore atteint
ce point.

**Il faut laisser le wasm tourner jusqu'à t≈500 s au moins.**

# Ce que ça corrige

Plusieurs rounds comparaient le wasm à 200 s (chargement, 3k/s) avec le natif à 9 126 s (jeu, 65k/s), et
concluaient que le wasm ne dessine pas. **Il était simplement plus tôt dans le même parcours.**

# La prédiction testable

Si le wasm suit le même parcours, il devrait basculer vers draw 60k/s autour de **t=500-600 s**.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 MB | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| sys_vm_memory_map | non (0 contre 2) |

---

# La prédiction est infirmée

## Le résultat

| instant | draw | threads |
|---|---|---|
| t=444 s | 3k/s | 19 |
| t=497 s | 3k/s | 19 |
| t=605 s | **3k/s** | **20** |

**Le wasm n'a pas basculé à t=500-600 s**, contrairement au natif qui passe de 4k/s à 68k/s entre t=411 et
t=511 s.

## Ce que ça établit

La prédiction « le wasm suivra le même parcours et basculera vers 60k/s » est **fausse**.

Le wasm reste à `3k/s` de draws pendant que le natif, au même âge, est à `68k/s`.

## Ce qui n'est pas infirmé

Le wasm **progresse toujours** : il est passé de 19 à **20 threads**. Le jeu continue son démarrage, mais
**pas par la même voie** que le natif.

## La lecture

Le natif, au moment de sa bascule (t=411→511), gagne l'accès à un régime de rendu complet. Le wasm, à
t=605 s, ne l'a pas gagné.

**Quelque chose manque au wasm pour franchir cette bascule** — et ce n'est pas le temps.

## Ce que ça rétablit

Ma conclusion du round 109 (« il suffit d'attendre ») est **infirmée**. Le temps n'est pas le facteur
manquant.

C'est la deuxième fois que je concluais « il suffit d'attendre » et que la mesure me contredit — la première
était au round 89 (« la lenteur est la cause »).

## La question reste

Qu'est-ce qui, chez le natif, provoque la bascule entre t=411 et t=511 s ? Et pourquoi le wasm ne le produit-il
pas ?

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |
