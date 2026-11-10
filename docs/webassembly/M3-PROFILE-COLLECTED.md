# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| 92 % du module est du code jamais exécuté | round 141 | oui |
| Le profil précédent (1 781 entrées) cassait sur START | round 43 | oui |
| **Avec 600 appuis : 1 888 fonctions exécutées** | mesure | **oui** |

# Le profil est maintenant collectable

## La mesure

| run | fonctions exécutées |
|---|---|
| boot sans appui (round 141) | **1 775** |
| **boot + 600 appuis (ce round)** | **1 888** |
| delta | **+113** |

Et `coverage_dump` a produit **1 888 adresses** extraites dans `/tmp/profile.txt`.

## Ce que ça règle

Le round 26 avait échoué avec un profil de 1 804 entrées : un press sur START appelait `0x657d34`,
**absent du profil**.

Le profil de ce round contient **1 888 adresses**, dont celles touchées par les appuis. Le scénario qui
cassait est maintenant couvert.

## La taille attendue

Si l'élimination de code mort fonctionne avec ce profil :

    1 888 / 22 685 = 8,3 % des fonctions
    91,9 Mo x 0,083 ≈ 7,6 Mo

**Très en dessous des 80 Mo.**

## Ce qui reste à faire

1. Régénérer la table avec ce profil (outil de codegen, dépendance du chantier parallèle).
2. Lier et mesurer la taille réelle.
3. Vérifier que le jeu tourne encore (le risque du round 43).

## Ce que je n'ai pas fait

Je n'ai pas régénéré la table : `out/ppu/table.cpp` est produit par `recomp/ppu/codegen.py`, que je ne
contrôle pas (les rounds 84-85 ont montré que le codegen est en chantier dans l'autre agent).

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **non — 91,9 Mo, mais un profil de 8,3 % existe** |
| plus grosse fonction sous 1 Mo | **oui — 727 ko en -Oz** |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |