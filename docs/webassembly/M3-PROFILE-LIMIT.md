# La limite du filtre par profil, mesurée

## Ce que j'ai essayé

Le titre interroge l'input **513 fois** pendant le boot (sonde sur `cellPadGetData`), avec
un pad toujours neutre. J'ai injecté un appui START à un offset fixe pour distinguer
« le jeu attend un appui » de « le jeu est bloqué ».

## Ce qui s'est passé

| | sans START | avec START |
|---|---|---|
| frames rendues | **6 781** | **241** |
| résultat | boot stable | **`Aborted()`** |

Le crash :

    F ppu: unknown indirect target 0x00657d34 (lr=0x000e0324, thread 0x1000000)

## La cause

`0x657d34` **est** dans la table complète :

    15921:    {0x657d34, f_00657d34},

et **absent** de la table filtrée que j'ai produite au round 40 :

    grep -c '0x00657d34' /tmp/table_filtered.cpp  →  0

**Mon élimination de code mort a supprimé une fonction que l'appui START atteint.** Le
profil de couverture avait été pris sur un parcours sans input ; le START ouvre un chemin
que le profil n'avait jamais vu.

## Ce que ça démontre

Le filtre par profil est **valide pour le parcours profilé et invalide pour tout autre
parcours**. Ce n'est pas un défaut de mise en œuvre : c'est la propriété de la méthode. Un
profil ne couvre que ce qu'il a exécuté, et 7,8 % de couverture signifie que 92 % du code
attend des chemins qui n'ont pas encore été pris.

Deux conséquences concrètes :

1. **Les 55 Mo du round 40 sont un module qui fonctionne pour le parcours profilé**, pas un
   jeu complet. Il faut le dire ainsi.
2. **Le filtre doit être régénéré** dès que le parcours change — et un parcours qui atteint
   le menu emprunte nécessairement des chemins que le parcours de boot n'emprunte pas.

## Ce que ça révèle du vrai blocage

Le fait que le START **change le comportement** (241 frames au lieu de 6 781, puis un chemin
de code différent) est une information neuve et importante : **le guest n'est pas simplement
en train de tourner à vide.** Il réagit à l'input, ce qui veut dire qu'il est dans un état
où l'input est traité — un écran, pas une boucle de chargement.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | oui, 55 Mo — **mais pour le parcours profilé seulement** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | non |

J'avais présenté les 55 Mo comme un gain net au round 40. **C'est un gain conditionnel au
parcours**, et ce round le démontre par un crash reproductible. Je corrige la présentation.
