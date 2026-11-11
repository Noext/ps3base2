# Les branches `bi=1` existent, mais pas pendant l'initialisation video

## La mesure

Compteurs globaux dans `bc_cond`, rapportes tous les multiples de dix :

    [btall] branches=1000   bi1=0
    [btall] branches=10000  bi1=55
    [btall] branches=50000  bi1=625

| seuil | branches | `bi=1` |
|---|---|---|
| 1 000 | 1 000 | **0** |
| 10 000 | 10 000 | 55 |
| 50 000 | 50 000 | 625 |

## Ce que ca etablit

Les branches testant `EQ` du champ 0 **existent** dans le programme (625 sur 50 000). Elles ne sont donc pas
absentes du code lifté.

Mais **aucune ne s'execute parmi les mille premiers branchements**, qui couvrent l'initialisation. La branche 6
de `f_002da36c` — celle qui teste `EQ` sur la hauteur — n'est donc pas atteinte **parce que le flux n'y arrive
pas**, pas parce qu'elle serait mal compilee.

## Ce que ca confirme

| fait | statut |
|---|---|
| la branche 6 existe dans le source | oui |
| elle teste `EQ` sur la hauteur (`0x2d0`) | oui, donc EQ=0 et elle n'est pas prise |
| elle n'apparait pas dans la trace | **coherent : le flux ne l'atteint pas** |
| aucune branche `bi=1` avant 1000 | **mesure** |

## La conclusion qui se dessine

Le flux entre dans `f_002da36c`, execute les branches 1 a 5 (`bi` de 0 et 2), et **ne parvient pas** a la
branche 6. Les quatre labels cibles des branches 1 a 5 (`L_2da478`, `L_2da458`, `L_2da428`, `L_2da41c`) sont
tous **avant** elle dans le source : une boucle qui ne sort pas.

C'est coherent avec un **compteur de boucle** qui ne progresse pas — un `gpr` que le lift n'incremente pas
comme le binaire l'attend, ou une condition qui reste vraie.

## Ce qu'il faudrait mesurer

Les valeurs des `gpr` utilises par les branches 1 a 5 a chaque iteration, pour voir si l'une d'elles reste
constante. C'est un probe sur `bc_cond` qui rapporte les quatre conditions avec leurs operandes.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 44,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |