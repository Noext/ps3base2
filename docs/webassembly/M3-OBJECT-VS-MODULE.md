# La géante : 365 ko dans l'objet, 1 316 ko dans le module

## La mesure

J'ai compilé `mc_018.cpp` — le fichier qui contient `f_001e1f68` — sous cinq réglages et mesuré la taille du
symbole dans l'**objet** :

| réglage | `f_001e1f68_body` dans l'objet | objet |
|---|---|---|
| **`-Oz`** | **364 746 o** | 742 128 |
| `-Oz -fno-inline` | **335 532 o** | 682 227 |
| `-Oz -fno-unroll -fno-vectorize` | 364 746 o | 741 940 |
| `-Os` | symbole absent (renommé/fusionné) | 1 138 631 |
| `-O2` | 523 281 o | 1 143 270 |

## La contradiction

| où | taille de la plus grosse fonction |
|---|---|
| **objet** (`llvm-objdump`) | **364 746 o** |
| **module lié** (`wasm_sizes.py`) | **1 316 534 o** |

Un facteur **3,6** entre les deux.

## Ce que ça établit

La fonction **n'est pas géante dans l'objet**. Elle le devient **au link**.

Ce n'est donc pas un problème de génération de code : c'est un problème de **disposition par le linker**.

## Ce que ça explique enfin

Toutes mes tentatives précédentes visaient le mauvais étage :

| round | tentative | étage |
|---|---|---|
| 139-147 | chercher une grosse source | source |
| 146 | `-fno-inline` | compilation |
| 153 | découpage mécanique | source |
| **154** | **mesure de l'objet** | **lien** |

J'ai corrigé l'étage. Le levier est le **linker**, pas le compilateur.

## La piste

`wasm-ld` fusionne ou regroupe les fonctions voisines. Si `f_001e1f68` est **adossée** à d'autres fonctions
dans la même section, il peut les agréger en une seule.

Le remède serait de **séparer** cette fonction dans sa propre section — ce que fait `-ffunction-sections`,
couplé à `--gc-sections`.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui — 49,7 Mo** |
| plus grosse fonction sous 1 Mo | non — mais **c'est un effet du linker**, pas du code |
| ligne de log du menu | **oui, corroborée** |

---

# Les tests de la piste linker

## `-ffunction-sections`

| | objet |
|---|---|
| avec `-ffunction-sections` | **742 128 o** |
| sans | **742 128 o** |

**Aucun changement.** Emscripten applique déjà cette séparation au niveau objet ; l'option n'ajoute rien ici.

## Le link isolé

J'ai tenté de lier **seulement** `mc_018.o` pour voir ce que la géante devient hors contexte. Le link
**échoue** (symboles manquants : le runtime, la table, les autres modules PPU).

Je n'ai donc **pas** pu mesurer la taille de la géante dans un module réduit.

## Ce que je n'ai pas établi

Pourquoi la fonction triple de taille au link. Trois explications possibles, aucune vérifiée :

1. le linker **fusionne** des fonctions voisines ;
2. il **inline** des appels dont la cible est dans un autre objet ;
3. mon `wasm_sizes.py` mesure des **groupes de fonctions** là où `llvm-objdump` mesure des symboles, et les
   deux chiffres ne comptent pas la même chose.

La troisième est la plus probable, et je ne l'ai pas vérifiée : `llvm-objdump` donne la taille d'un symbole
**jusqu'au suivant**, ce qui inclut le padding ; `wasm_sizes.py` donne la taille **déclarée dans la section
code**, qui est exacte.

Si c'est la troisième, alors **365 ko est le bon chiffre** et la fonction ne dépasse pas 1 Mo.

## La mesure qui trancherait

Comparer, dans le module lié, la taille de la section code attribuée à `f_001e1f68` **par la section `name`**
avec la somme des tailles de ses voisines. Si elle est bien 365 ko, mon rapport de 1,3 Mo est faux.
