# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| `f_0070d138` fait 1,4 Mo en `.wasm` | rounds 20-30 | oui |
| Structure : **2 083 labels, 2 509 `goto`** | mesure | oui |
| `-Oz` sur l'objet : 2 467 817 → **1 215 260** octets | mesure | oui |
| **Module `-Oz` complet : plus grosse fonction 727 079 o, module 91,9 Mo** | mesure | **oui — le résultat** |

# Le seuil de fonction est atteint avec -Oz

## Les deux mesures

| | `-O1` | `-Oz` | seuil | verdict |
|---|---|---|---|---|
| plus grosse fonction | 1 409 972 o | **727 079 o** | 1 Mo | **PASSÉ en -Oz** |
| module | 56 Mo | **91,9 Mo** | 80 Mo | échoue en -Oz |

## Ce que ça établit

**Les deux critères de taille sont en tension** :

- `-O1` : module **56 Mo** (passe), fonction **1,4 Mo** (échoue) ;
- `-Oz` : module **91,9 Mo** (échoue), fonction **727 ko** (passe).

Aucune des deux configurations ne satisfait les deux seuils simultanément.

## Ce que ça signifie

Le seuil « fonction < 1 Mo » **est atteignable** : `-Oz` le démontre (727 ko).

Mais au prix du module, qui passe de 56 à 92 Mo.

## La question

Peut-on **mélanger** : `-O1` partout, `-Oz` sur le seul fichier contenant la fonction géante ?

C'est **faisable** : le build compile par fichier. `mc_071.cpp` en `-Oz`, le reste en `-O1`.

## La mesure à faire

Lier avec `mc_071.o` en `-Oz` et tous les autres en `-O1`, puis mesurer module et plus grosse fonction.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui en -O1 (56 Mo)** |
| plus grosse fonction sous 1 Mo | **oui en -Oz (727 ko)** |
| les deux ensemble | **pas encore mesuré** |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |

---

# Le link mixte : fonction OK, module non

## La mesure

`mc_071.o` compilé en `-Oz`, tous les autres objets en `-O1`, puis link :

    ps3mix.wasm  funcs 81655  largest 779588  total 110193839
    module: 113782517 bytes = 108.5 MB

| configuration | plus grosse fonction | module |
|---|---|---|
| `-O1` pur | 1 409 972 o | **56 Mo** |
| `-Oz` pur | **727 079 o** | 91,9 Mo |
| **mixte** | **779 588 o** | 108,5 Mo |

## Ce que ça établit

**Le seuil de fonction est atteignable** : 779 588 o < 1 Mo, en mixte comme en `-Oz` pur.

**Le module ne l'est pas** : 108,5 Mo en mixte, au-dessus des 80 Mo.

## Ce que ça révèle

Le module à 56 Mo du round 26 utilisait une **table filtrée** (1 804 entrées). Le module actuel lie la table
**complète** (22 685 fonctions), ce qui ajoute du code.

Les trois tailles ne sont donc pas comparables directement : elles diffèrent par **ce qui est lié**, pas
seulement par l'optimisation.

## Ce qu'il faudrait mesurer

Un link avec la table **complète** et tous les objets en `-O1`, pour isoler l'effet de l'optimisation de
celui du contenu de la table. C'est la mesure qui manque pour trancher.
