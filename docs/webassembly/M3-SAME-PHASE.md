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