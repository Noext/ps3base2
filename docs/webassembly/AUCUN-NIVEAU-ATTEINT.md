# Le chemin du mappage memoire n'est emprunte a aucun niveau

## Ce que j'ai mesure

Quatre niveaux de remontee, chacun avec les **bonnes** adresses (verifiees comme entrees de la table) et
**une garde par fonction, sans englobante**.

| niveau | fonctions gardees | atteintes |
|---|---|---|
| emetteur | `f_0021c470` | **0** |
| appelants directs | `f_00523e00`, `f_005240d8` | **0** |
| appelants de niveau 3 | `f_00524034`, `f_00185b4c`, `f_00524488`, `f_00559844` | **0** |
| la ligne de menu | -- | **1** |

**Aucune fonction du chemin n'est executee, a aucun niveau.** Le jeu atteint le menu et n'entre jamais dans
cette branche.

## Ce que ca exclut maintenant

| hypothese | statut |
|---|---|
| le syscall est emis mais le handler ne le recoit pas | **ecartee** — le syscall n'est jamais emis |
| la fonction emettrice est atteinte mais echoue | **ecartee** — elle n'est jamais atteinte |
| un seul appelant manque | **ecartee** — quatre niveaux, tous absents |

## Ce que ca suggere fortement

**Le chemin n'est pas emprunte du tout.** Ce n'est pas une defaillance a un niveau : c'est une branche
entiere du jeu qui n'est jamais prise.

Cela change la nature du diagnostic : la question n'est plus « pourquoi le mappage echoue » mais **« pourquoi
cette branche n'est jamais prise »**. Et une branche non prise depend d'un etat amont, pas d'une fonction.

## Ce que je dois mesurer ensuite

Un appelant de `f_00185b4c` (56 ko, 61 branches — une entree de haut niveau). Si lui non plus n'est pas
atteint, la remontee continue ; s'il est atteint, le point de bifurcation est **a l'interieur** et c'est une
condition a lire dans son code.

## Ce que j'ai corrige dans ma methode

| erreur passee | correction appliquee |
|---|---|
| garder une fonction et ses callees | **une garde par fonction, jamais l'englobante** |
| chercher par motif textuel sans verifier | **chaque adresse verifiee comme entree de table** |
| conclure sur une fonction non lue | **lire le corps avant de le garder** |

Les quatre adresses du niveau 3 sont toutes confirmees dans la table, et `f_00185b4c` a ete lu.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 5 criteres sur 6 |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |