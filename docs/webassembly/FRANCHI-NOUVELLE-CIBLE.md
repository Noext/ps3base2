# Le chemin a franchi la fonction liftée : nouvelle cible 0x000c2ba0

## Ce qui a change

Le round 187 a lifte 0x000f656c. Le module reconstruit avec cette fonction ne bloque plus dessus.

    AVANT : unknown indirect target 0x000f656c
    APRES : unknown indirect target 0x000c2ba0  (lr=0x000e0828, thread 0x1000000)

L'adresse bloquante est differente. Le chemin a progresse au-dela de la premiere fonction manquante.

## La validation du mecanisme

| etape | resultat |
|---|---|
| lift de 0xf656c | 1 548 octets, entree de table presente |
| liaison dans le module | mc passe de 0x8aa674 a 0x8af1ec (+19 448 octets) |
| execution | franchit 0xf656c, bloque sur 0xc2ba0 |

C'est le comportement attendu d'une boucle de decouverte : chaque iteration resout une adresse et en revele une
autre.

## Ce que la boucle doit faire maintenant

Ajouter 0x000c2ba0 a out-mc/gen/indirect_seeds.txt (ajouter, pas remplacer), relancer setup.py --steps ppu,
recompiler et relier. tools/discover_loop.sh automatise exactement cela.

## Les progres intermediaires mesures

| etape | threads crees |
|---|---|
| module avec la fonction liftée mais sans LLE | 0 |
| + modules LLE dans le disque | 8 |
| + resolution des doublons de liaison | 12 |

Le jeu passe de « ne demarre pas » a 12 threads crees, l'ordre de grandeur d'un boot qui avance.

## Ce que ca etablit

La chaine lift -> lier -> executer fonctionne et progresse. Ce n'est plus « le wasm n'atteint pas le monde » :
c'est « il reste des fonctions a lifter, et la liste s'obtient en executant ».

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 5 criteres sur 6 ; la boucle de decouverte progresse, une adresse par iteration |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |