# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| Le mot `0xbe0704` bascule 0/1 | round 131 | **incomplet — précisé ici** |
| `f_002dc238` lit ce mot et boucle | `mc_028.cpp` | oui |
| **Le mot est stable à 1 (t=39,79) puis stable à 0 (t=119,159)** | mesure | **oui — ni compteur ni parité** |

# Le mot est un état, pas une horloge

## La mesure

    100 échantillons consécutifs par palier :
    t=39s   first=1 last=1 max=1 changes_in_100=0
    t=79s   first=1 last=1 max=1 changes_in_100=0
    t=119s  first=0 last=0 max=0 changes_in_100=0
    t=159s  first=0 last=0 max=0 changes_in_100=0

## Ce que ça établit

| hypothèse | statut |
|---|---|
| compteur qui monte | **éliminée** — `max=1`, jamais plus |
| parité qui flippe vite | **éliminée** — 0 changement sur 100 |
| **état binaire qui change rarement** | **confirmée** |

Le mot vaut **1 pendant au moins 80 s**, puis **0 pendant au moins 80 s**.

## Ce que ça signifie

C'est un **drapeau d'état**, pas une horloge. Et il a changé **une fois** entre t=79 et t=119.

Cette transition est un **événement réel du jeu**. La capturer dirait où le jeu en est.

## Ce que ça corrige du round 131

J'avais écrit « le drapeau est vivant, il bascule ». C'est **inexact** : il ne bascule pas rapidement, il
change d'état **lentement**. La conclusion « la boucle avance » reste plausible mais n'est plus démontrée par
ce seul fait.

## La mesure suivante

Détecter **l'instant exact** de la transition 1→0 et regarder ce qui se passe à ce moment dans les logs. Si
un thread est créé ou un syscall particulier émis, c'est le point de bascule du jeu.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |