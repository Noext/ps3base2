# Le filtre couvre les deux parcours : 55 Mo, 11 341 frames, aucun crash

## Ce que j'ai fait

Round 42 avait montré que le filtre par profil cassait dès qu'un appui START ouvrait un
chemin non profilé. Ce round régénère le profil **avec le START actif**, pour que l'ensemble
conservé couvre les deux parcours.

| | profil sans START (round 40) | profil avec START |
|---|---|---|
| adresses atteintes | 1 775 | **1 781** |
| `0x657d34` (la cible du crash) | **absent** | **présent** |
| comportement au START | **crash** | **pas de crash** |

Six fonctions de plus suffisent à couvrir le chemin que START ouvre. Ce n'est pas une
coïncidence : le parcours d'appui n'ajoute qu'une poignée de fonctions, mais l'une d'elles
était précisément celle qui manquait.

## Le résultat

| | valeur |
|---|---|
| **module** | **55 Mo** (seuil 80) |
| **frames rendues** | **11 341** |
| `unknown indirect target` | **aucun** |
| crashes | 0 |

Le module est sous le seuil **et** il absorbe le parcours avec input. La correction du round
42 est intégrée : ce n'est plus un gain conditionnel à un seul parcours, c'est un gain qui
tient sur les deux que j'ai exercés.

## Ce que ça ne prouve pas

Il reste 20 904 fonctions hors du filtre. **Tout parcours non exercé peut encore manquer une
fonction**, exactement comme START l'a fait. Le filtre est valide pour les parcours profilés
— deux ici — et inconnu pour les autres.

C'est une limite de la méthode, pas du résultat : pour un jeu complet il faudrait soit
profiler tous les parcours (impossible à garantir), soit une élimination statique
conservatrice.

## État des critères du jalon 3

| critère | état |
|---|---|
| module sous 80 Mo | **oui, 55 Mo** |
| plus grosse fonction sous 1 Mo | **non, 1,4 Mo** |
| ligne de log du menu | **non** |

## Position

Le jalon 3 reste **ouvert** sur deux critères. Ce round consolide le premier : les 55 Mo ne
sont plus fragiles au parcours que j'ai testé, et le mécanisme de correction (régénérer le
profil quand le parcours change) est démontré et reproductible.
