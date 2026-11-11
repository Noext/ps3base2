# La vraie fonction emettrice, et pourquoi elle n'est jamais atteinte

## Une erreur de cible corrigee

Le round 180 concluait que le syscall 300 est emis quatre fois. La mesure etait juste, mais la fonction visee
etait fausse.

| ce que je gardais | ce que c'est |
|---|---|
| f_0021c42c | 1 159 octets qui appellent f_0021db7c et f_0021dc88, aucun syscall |
| **f_0021c470** | **la fonction qui contient ctx.gpr[11] = 0 + 300** |

Mon script de recherche avait pris f_0021c42c parce qu'il cherchait le premier void f_ avant le motif, et
f_0021c470 se trouve plus loin dans le fichier.

## La mesure corrigee

| garde sur | occurrences |
|---|---|
| f_0021c42c (mauvaise cible) | 4 |
| **f_0021c470 (vraie emettrice)** | **0** |
| f_00523e00 (appelant A) | **0** |
| f_005240d8 (appelant B) | **0** |
| ligne de menu | 1 |

Le vrai emetteur n'est jamais atteint, et aucun de ses deux appelants non plus.

## Ce que ca renverse

| round | conclusion | statut |
|---|---|---|
| 180 | le syscall 300 est emis quatre fois | mesure valide, attribution fausse |
| 177/178 | f_0021c42c jamais atteinte | mesurait la mauvaise fonction |

## La chaine correcte

    menu atteint
      -> ni f_00523e00 ni f_005240d8 n'est execute
        -> f_0021c470 n'est jamais execute
          -> ctx.gpr[11] = 300 n'est jamais execute
            -> syscall 300 jamais emis
              -> handler jamais entre
                -> pas de monde

## La question qui reste

Pourquoi f_00523e00 et f_005240d8 ne sont pas atteints. Ce sont les deux seuls appelants du vrai emetteur.

## Ce que j'ai appris sur ma methode

Trois mesures successives sur la mauvaise fonction, parce que j'ai cherche par motif textuel dans 170 fichiers
sans verifier que le motif appartenait a la fonction nommee. La verification qui manquait : lire le corps
avant de garder.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 5 criteres sur 6 |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |