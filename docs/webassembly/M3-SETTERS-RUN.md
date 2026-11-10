# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| Sept fonctions écrivent l'offset 1796 | round 129 | oui |
| Hypothèse : aucune n'est appelée → le drapeau reste 0 | round 129 | **FAUSSE — corrigée ici** |
| **Quatre setters sont appelés** | mesure | **oui** |

# Les setters sont appelés

## La mesure

    [setter] pc=0x01217760 lr=0x2d6374
    [setter] pc=0x01004f74 lr=0x223514
    [setter] pc=0x01302b98 lr=0x2040e0
    [setter] pc=0x004e2064 lr=0x2d63fc

Quatre des sept fonctions sont **atteintes**. Et deux d'entre elles (`lr=0x2d6374`, `lr=0x2d63fc`) sont
appelées depuis la **région 0x2d6xxx**, proche de la boucle.

## Ce que ça infirme

L'hypothèse du round 129 — « aucun setter n'est appelé, donc le drapeau reste à 0 » — est **fausse**.

Les setters **tournent**. Le drapeau devrait donc changer.

## La contradiction

| fait | valeur |
|---|---|
| setters appelés | **4** |
| basculements du drapeau en 200 échantillons | **0** |

Les deux ne s'accordent pas. Trois explications possibles :

1. Les setters écrivent un **autre** offset 1796, sur une base différente.
2. Ils écrivent la **même valeur** (0), donc « écrire » ne change rien.
3. Ils écrivent mais **après** mon échantillonnage (les gardes s'arrêtent à 4 par fonction).

## Ce que je dois mesurer

Enregistrer **les valeurs écrites**, pas seulement les appels. C'est la seule façon de distinguer « écrit 0 »
de « écrit autre chose ».

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |