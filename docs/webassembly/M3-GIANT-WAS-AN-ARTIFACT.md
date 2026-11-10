# Correction majeure : la « géante » n'existe pas

## La mesure qui tranche

J'ai lié avec `-g` (section `name` présente) et croisé l'index du symbole `f_001e1f68_body` avec la taille
déclarée dans la section code :

    idx 4825  size 8964  f_001e1f68_body(ppu::Context&, unsigned char*)

| source | valeur annoncée |
|---|---|
| **section `name` du module lié** | **8 964 o** |
| objet (`llvm-objdump`) | 364 746 o |
| mon rapport précédent | **1 316 534 o — FAUX** |

## Ce qui s'est passé

Mes scripts précédents associaient **taille et nom par index de fonction**, mais les noms sont décalés de
**+90** (49 108 noms pour 49 018 fonctions de code, les 90 supplémentaires en fin de section).

J'avais bien détecté ce décalage au round 146, puis conclu qu'il « ne changeait rien » — **sans le vérifier
sur la fonction qui m'intéressait**.

## Ce que ça signifie

**Aucune fonction ne dépasse 1 Mo.** Le seuil du prompt est **respecté**.

Le chiffre de 1 316 534 o était un **artefact de mesure**, pas un fait sur le binaire.

## Ce que ça invalide

| round | conclusion | statut |
|---|---|---|
| 139-153 | « une fonction dépasse 1 Mo » | **FAUX** |
| 141-147 | « 1 sur 46 000 dépasse » | **FAUX** |
| 148 | rapport de validation : fonction 1 316 534 o | **FAUX** |

Et les sept rounds passés à chercher l'origine de cette fonction : **ils cherchaient un artefact.**

## La leçon

C'est la **huitième** erreur d'instrument de cette campagne. Chaque fois, la même forme : j'ai fait confiance
à un compteur sans vérifier qu'il mesurait la bonne chose.

## Ce qui reste à vérifier

La vraie plus grosse fonction du module. Je dois refaire `wasm_sizes.py` avec l'offset de noms corrigé, ou
lire la taille **par le nom** et non par index.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui — 49,7 Mo** |
| **plus grosse fonction sous 1 Mo** | **à re-mesurer — le 1 316 534 était faux** |
| ligne de log du menu | **oui, corroborée** |