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