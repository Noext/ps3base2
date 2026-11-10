# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| « Les deux critères sont en tension » | round 139 | **FAUX — corrigé ici** |
| Le module à 56 Mo utilisait une table **filtrée** | round 26 | oui |
| **À contenu égal : `-Oz` gagne sur les DEUX critères** | mesure | **oui — la correction** |

# À contenu égal, -Oz gagne partout

## La mesure, table complète des deux côtés

| optimisation | plus grosse fonction | module |
|---|---|---|
| **`-O1`** | 1 444 886 o | **109,5 Mo** |
| **`-Oz`** | **727 079 o** | **91,9 Mo** |

## Ce que ça détruit

Le round 139 concluait : « aucun réglage ne satisfait les deux seuils ». **C'est faux.**

My linker comparait 56 Mo (`-O1`, table filtrée) à 92 Mo (`-Oz`, table complète). **Deux variables
changeaient à la fois.**

À contenu égal, `-Oz` est **meilleur sur les deux** : 727 ko contre 1 445 ko pour la fonction, et 92 Mo
contre 110 Mo pour le module.

## La leçon de méthode

**Cinquième erreur d'instrument de la campagne** (rounds 103, 122, 129, 131, 140) : j'ai comparé des choses
qui différaient par deux facteurs.

Le module à 56 Mo **n'existe plus** : il venait d'une table filtrée à 1 804 entrées, qui ne peut pas faire
tourner le jeu correctement.

## Ce que reste à faire

| critère | seuil | `-Oz` | verdict |
|---|---|---|---|
| module | 80 Mo | **91,9 Mo** | échoue de **12 Mo** |
| plus grosse fonction | 1 Mo | **727 ko** | **passe** |

Il manque **12 Mo** sur le module. C'est un objectif atteignable : quelques gros objets en `-Oz` combinés à
l'élimination de code inutilisé.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **non — 91,9 Mo en -Oz (meilleur cas)** |
| plus grosse fonction sous 1 Mo | **oui — 727 ko en -Oz** |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |