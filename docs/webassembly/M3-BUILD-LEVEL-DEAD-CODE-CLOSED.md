# L'élimination au niveau du build ne marcherait pas non plus

## L'hypothèse testée

Les rounds 157 ont montré que ni la table filtrée, ni `-ffunction-sections`, ni `--gc-sections` ne réduisent
le code. La piste restante : **ne compiler que les fichiers contenant des fonctions utiles**.

C'est faisable : le profil donne les adresses, la table donne les symboles, les sources donnent les
fichiers.

## La mesure

| | valeur |
|---|---|
| adresses du profil | 1 890 |
| résolues en symboles | **1 890** (100 %) |
| symboles localisés dans un fichier | **1 785** |
| **fichiers nécessaires** | **80 sur 88** |
| octets de source à compiler | **111 561 571** sur 123 684 646 |
| **part** | **90,2 %** |

## Ce que ça établit

**Les 1 890 fonctions exécutées sont dispersées dans 80 fichiers sur 88.** Ne compiler que le nécessaire
économiserait **10 %** de la source, pas 90 %.

Le découpage en fichiers ne correspond pas au découpage en fonctions : un même `mc_*.cpp` contient des
centaines de fonctions, dont quelques-unes utiles.

## Ce que ça clôt définitivement

| piste | verdict |
|---|---|
| filtre de table | réduit la table, pas le code |
| `-ffunction-sections` | aucun effet |
| `--gc-sections` | aucun effet |
| **ne compiler que les fichiers utiles** | **10 % de gain seulement** |

## La conclusion

**L'élimination de code mort n'est pas atteignable par ces moyens.** Le code inutilisé est entrelacé avec
le code utile au niveau des fichiers.

Pour l'atteindre, il faudrait découper **par fonction** dans les sources générées — c'est-à-dire un fichier
par fonction, ce qui est un changement de `codegen.py`.

## Ce que ça signifie pour le jalon 3

Le module fait **49,7 Mo**, sous le seuil de 80 Mo. **C'est suffisant.** L'élimination de code mort n'est
pas nécessaire pour passer le critère.

La seule raison de la poursuivre serait de réduire la plus grosse fonction — et nous avons établi qu'elle
n'est pas découpable localement.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui — 49,7 Mo, sans élimination de code** |
| plus grosse fonction sous 1 Mo | non — 1 358 012 o |
| ligne de log du menu | **oui, corroborée** |
| élimination de code mort | **non viable** — 4 pistes fermées |