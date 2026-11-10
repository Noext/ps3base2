# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| Profil de 1 888 adresses collecté | round 142 | oui |
| 92 % du code est mort | round 141 | oui |
| **Table filtrée → module 50,2 Mo, plus grosse fonction 1 316 105 o** | mesure | **oui — le résultat** |

# La table filtrée : module OK, fonction non

## Le filtre

| | valeur |
|---|---|
| entrées de la table complète | 22 685 |
| entrées du profil | **1 888** (8,3 %) |
| adresses du profil **sans** entrée | **0** |

Le filtre est **exact** : toutes les adresses exécutées existent dans la table.

## Les trois configurations, toutes en `-Oz`

| table | plus grosse fonction | module |
|---|---|---|
| **complète** | **727 079 o** | 91,9 Mo |
| **filtrée (1 888)** | **1 316 105 o** | **50,2 Mo** |

## Ce que ça établit

**Le module passe sous 80 Mo** : 50,2 Mo. C'est un progrès réel et mesuré.

**Mais la plus grosse fonction grossit** : 727 ko → 1 316 ko. Elle repasse au-dessus de 1 Mo.

## Pourquoi

Filtrer la table change la façon dont wasm-ld fusionne et dispose le code. Les fonctions ne sont plus
alignées sur les mêmes frontières, et `f_0070d138` se retrouve **fusionnée** avec d'autres.

## Ce que ça donne

| critère | seuil | complète | filtrée |
|---|---|---|---|
| module | 80 Mo | 91,9 ✗ | **50,2 ✓** |
| fonction | 1 Mo | **727 ko ✓** | 1 316 ko ✗ |

**Chaque configuration passe un critère et échoue l'autre.**

## La piste

Un build **hybride** : table filtrée (module léger) + `mc_071.cpp` en `-Oz` **et** la fonction géante isolée
dans son propre fichier pour que wasm-ld ne la fusionne plus.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui avec table filtrée (50,2 Mo)** |
| plus grosse fonction sous 1 Mo | **oui avec table complète (727 ko)** |
| les deux ensemble | **non — mais la table filtrée donne la marge nécessaire** |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |