# Le seuil de taille de fonction : pourquoi il ne se franchit pas simplement

## La mesure

Deux fonctions dépassent 1 Mo de source :

| fonction | fichier | octets |
|---|---|---|
| `f_0070d138` | `mc_071.cpp` | **2 265 876** |
| `f_001e1f68` | `mc_018.cpp` | 1 032 540 |

Distribution : 46 fonctions > 100 Ko, 13 > 200 Ko, 3 > 500 Ko, **2 > 1 Mo**.

## Pourquoi un découpage mécanique ne marche pas

J'ai analysé la structure de la plus grosse :

| | nombre |
|---|---|
| labels (`L_xxxx`) | **2 083** |
| labels ciblés par un `goto` | **2 082** |
| labels atteints seulement par fallthrough | **1** |
| appels directs à d'autres fonctions | 2 958 |

**2 082 des 2 083 labels sont des cibles de saut.** La fonction n'est pas une séquence de
blocs indépendants que l'on pourrait extraire en helpers : c'est un **réseau de contrôle
entièrement connecté**. Couper à un label donné casserait tous les `goto` qui y arrivent
depuis d'autres blocs.

Un découpage correct demanderait de transformer les sauts inter-blocs en **paramètres de
continuation** (une machine à états explicite, ou des appels avec un numéro de bloc
suivant). C'est un changement de la stratégie de lifting, pas un post-traitement du code
généré.

## Ce que la mesure dit de la contrainte

V8 compile le module filtré de **58 Mo en 92 ms** et exécute la grosse fonction
correctement (round 4 : `BIGFUNC-RAN`). Le seuil de 1 Mo est donc une **règle de prudence**
du prompt, pas une limite technique observée.

C'est important à dire explicitement : la contrainte est respectée dans son intention
(« V8 refuse d'optimiser les grosses fonctions ») et le fait mesuré est que V8 **ne la
refuse pas** ici — il compile et exécute.

## Les routes

1. **Machine à états explicite** dans le lifting : chaque bloc devient une fonction
   `block_N(ctx, next_label)`, le dispatch se fait par un `switch`. Élimine complètement la
   taille, au prix d'un ralentissement sur le chemin chaud et d'un changement du générateur.
2. **Accepter le dépassement et le documenter**, en s'appuyant sur la mesure
   (`BIGFUNC-RAN`, 92 ms de compilation, exécution correcte).
3. **Découper seulement les deux fonctions > 1 Mo** par la route 1, laissant les 46 autres
   sous le seuil. Limite le travail à 2 fonctions sur 22 685.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 55 Mo** |
| plus grosse fonction sous 1 Mo | **non, 1,4 Mo** |
| ligne de log du menu | **non** |

Je ne franchis pas ce seuil ce round, et je ne vais pas prétendre le contraire. Ce que
j'apporte est la raison structurelle : **2 082 labels sur 2 083 sont des cibles de saut**,
donc le découpage est un changement du générateur de code, pas un nettoyage.

La route 3 (traiter les 2 fonctions de plus de 1 Mo par une machine à états) est la plus
proportionnée, et c'est ce que le prochain round devrait tenter.
