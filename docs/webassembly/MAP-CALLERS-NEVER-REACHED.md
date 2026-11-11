# Le chemin du monde n'est jamais emprunte, meme chez ses appelants

## Ce que j'ai cherche

Le round 177 a etabli que f_0021c42c -- la seule fonction qui emet le syscall 300 -- n'est jamais executee.
La question suivante : qui l'appelle, et cet appelant est-il atteint ?

## Les six appelants

| fonction appelante | fichier |
|---|---|
| f_002d9dec | mc_028.cpp |
| f_002da6f0 | mc_028.cpp |
| f_002f0750 | mc_029.cpp |
| f_002f0800 | mc_029.cpp |
| f_003316d4 | mc_032.cpp |
| f_00288e24 | mc_025.cpp |

## La mesure

| garde posee sur | occurrences |
|---|---|
| les six adresses de retour de l'appel | 0 |
| les six fonctions appelantes | 0 |
| la fonction emettrice f_0021c42c | 0 |
| la ligne de menu | 1 |

Aucune des trois couches n'est atteinte. Le jeu atteint le menu mais n'emprunte jamais le chemin qui mene au
mappage memoire, pas meme a son premier maillon.

## La chaine etablie

    menu atteint (17 threads d'interface)
      -> aucun des 6 appelants de f_0021c42c n'est execute
        -> f_0021c42c n'est jamais execute
          -> syscall 300 jamais emis
            -> handler jamais entre
              -> aucune zone mappee
                -> pas de monde

Le blocage est au-dessus des six appelants. Le point d'entree utile n'est plus 0x0021c42c mais la fonction
qui devrait appeler l'un des six.

## Un recoupement independant

f_002da6f0 figure parmi les sept fonctions que le round 129 avait trouvees comme ecrivant l'offset 1796. Deux
recherches differentes convergent sur la meme region.

## Ce que je n'ai pas etabli

Pourquoi aucun des six n'est appele. Six fonctions non atteintes simultanement suggere un point de
bifurcation unique en amont, pas six chemins independants.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 5 criteres sur 6 |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |