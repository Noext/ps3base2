# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| -Oz donne 91,9 Mo, il manque 12 Mo | round 140 | oui |
| Le code est 96,4 % du module (92,9 Mo) | mesure de section | oui |
| **1 775 fonctions exécutées sur 22 685** | boot43b.log | **oui — le chiffre décisif** |

# 92 % du code n'est jamais exécuté

## La décomposition

| section | taille | part |
|---|---|---|
| **code** | **92 913 831 o** | **96,4 %** |
| data | 3 134 395 o | 3,3 % |
| elem | 222 288 o | 0,2 % |

## La couverture

    coverage: 1775 distinct lifted functions executed of 22685 in the table

| | valeur |
|---|---|
| fonctions liées | 22 685 |
| fonctions exécutées | **1 775** |
| **part inutilisée** | **92,2 %** |

## Ce que ça donne

Le module pourrait passer de 91,9 Mo à environ 7 Mo en ne liant que les fonctions atteintes.

## Pourquoi ce n'est pas déjà fait

Le round 26 l'avait tenté avec une table filtrée (1 804 entrées) et cela cassait : un press sur START
appelait 0x657d34, absent du profil (round 43).

**Un profil est incomplet par nature.**

## La solution, dans le code

Le commentaire de coverage_dump dit : ce fichier est le keep-set pour l'élimination de code mort, et retire
les 92 % que le boot n'atteint pas.

Il manque un profil complet : plusieurs runs avec des entrées différentes, réunis.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **non — 91,9 Mo, mais 92 % du code est mort** |
| plus grosse fonction sous 1 Mo | **oui — 727 ko en -Oz** |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |