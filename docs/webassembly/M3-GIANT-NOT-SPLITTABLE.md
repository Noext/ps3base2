# La fonction géante n'est pas découpable

## La mesure

J'ai construit le graphe de blocs de `f_0070d138_body` :

| | valeur |
|---|---|
| blocs (labels + code) | **2 084** |
| `goto` résolus | **2 509** |
| `goto` non résolus | **0** |
| positions de coupe **bloquées** par un saut traversant | **2 061 / 2 083** |
| positions de coupe **légales** | **22** |

## Ce que les 22 coupes produisent

| segment | octets de source |
|---|---|
| **1** | **2 014 747** |
| 2 | 75 917 |
| 3 | 33 837 |
| 4 | 23 902 |
| 5 | 12 552 |

## Ce que ça établit

**Le plus gros segment fait 2 014 747 octets — 89 % de la fonction.**

Les 22 coupes sont **toutes au début** : les labels légaux sont aux blocs 0, 2, 3, 8, 13, 18, 23, 28, 33, 38…
c'est-à-dire dans les 40 premiers blocs sur 2 084.

Le corps principal est un **bloc monolithique** : de chaque label, on peut sauter presque n'importe où
ailleurs, donc aucune coupe n'est légale au milieu.

## La conclusion

**Découper `f_0070d138` est impossible sans transformer les sauts en appels avec état.**

C'est un travail de machine à états dans `recomp/ppu/codegen.py` — pas une transformation locale.

## Ce qui est maintenant établi avec certitude

| fait | méthode |
|---|---|
| plus grosse fonction | **`f_0070d138`, 1 358 012 o** | section code + noms décalés de 90 |
| sa source | `out/ppu/mc_071.cpp`, 2 266 097 o | lecture directe |
| structure | 2 084 blocs, 2 509 sauts, **fortement imbriqué** | graphe construit |
| découpable ? | **non** — 89 % dans un seul segment | mesure |

## Ce que le prompt demande

> si une fonction dépasse ~1 Mo de code, **s'arrêter et rapporter** avant de continuer.

Je rapporte, avec la fonction nommée, sa source, sa structure, et la démonstration que le découpage n'est
pas une option locale.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui — 49,7 Mo** |
| plus grosse fonction sous 1 Mo | **non — et non découpable localement** |
| ligne de log du menu | **oui, corroborée** |

---

# Ce n'est pas un dispatcher non plus

J'ai vérifié l'hypothèse « c'est une table de sauts déguisée, donc transformable ».

| marqueur | valeur |
|---|---|
| appels à des fonctions liftées | **2 958** |
| fonctions distinctes appelées | **15** |
| tail calls (call; return;) | **0** |
| marqueurs de dispatch indirect | **0** |
| affectations de registres | **70 288** |
| stockages mémoire | **3 873** |

## Ce que ça signifie

Ce n'est pas un aiguillage : c'est **une seule fonction du jeu** qui fait beaucoup de travail.

**Il n'y a pas de structure à exploiter.** Elle est monolithique parce que le jeu l'a écrite ainsi.

## Ce que ça clôt

| piste | verdict |
|---|---|
| découpage mécanique aux labels | non — 89 % dans un segment |
| transformable en dispatcher | non — 0 marqueur |
| inlining | non — fno-inline ne change rien |
| fusion du linker | non — taille dans la section code |

**Quatre voies explorées, quatre fermées par la mesure.**