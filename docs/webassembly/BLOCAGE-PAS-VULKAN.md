# Le blocage n'est pas Vulkan : il persiste avec le stub

## Les trois essais

| essai | resultat |
|---|---|
| boot normal | **exit 124**, une ligne |
| **cache GLCache efface** | **exit 124**, une ligne — le cache n'est pas en cause |
| **`PS3BASE_NO_RENDERER=1`** (stub, pas de Vulkan) | **exit 124**, une ligne |

Le troisieme est decisif : **sans Vulkan, le jeu bloque autant.**

## La trace avec le stub

Avec le stub, la trace est **differente** :

| trace | derniere operation |
|---|---|
| avec Vulkan | 31 `openat(/dev/nvidiactl)` consecutifs |
| **avec le stub** | **lecture du fd 25, puis `futex`, puis sortie** |

**Aucun `openat(/dev/nvidiactl)` avec le stub.** Le pilote n'est pas interroge quand le renderer est absent.

## Ce que ca corrige

Le round 174 concluait : « le pilote NVIDIA ne repond pas ». **C'etait vrai avec Vulkan, mais ce n'est pas
la cause premiere** : le jeu bloque aussi sans Vulkan.

C'est la **deuxieme attribution retiree** sur ce meme blocage (round 173 : l'autre agent ; round 174 : le
pilote).

## Ce que le fd 25 revele

Le fd 25 est encore un fichier `GLCache` meme avec le stub — le **pilote charge un shader avant que le choix
du renderer soit fait**. Ce n'est donc pas notre renderer qui declenche le chargement.

## Ce qui reste

Le blocage est **dans notre code**, apres la lecture initiale, sur un `futex`. Mais je n'ai pas identifie
quel thread ni quelle attente : 86 `futex` dans la trace, sans indication de l'appelant.

## La mesure qui manquerait

Une pile d'appels **du binaire** au moment du blocage. Mon `pgrep` du round 174 attrapait le shell, pas le jeu ;
il faut cibler le processus par son nom exact et l'attacher avant qu'il ne meure.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7, mesures bloquees |
| 3 | 5 criteres sur 6 |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps bloques |

---

# La pile d'appels : tentative et echec

| methode | resultat |
|---|---|
| pgrep -x puis gdb -p | a attrape le shell, pas le jeu |
| lancement en arriere-plan puis attachement | sortie vide |
| gdb -batch -ex run --args | sortie vide |
| gdb -x avec script | sortie vide |

Pourquoi : ptrace_scope vaut 1 (attachement limite), et le runtime appelle setsid en interne, donc le
processus se detache et le pid connu du shell n'est plus le sien.

Ce que le round 175 a etabli : le blocage n'est pas Vulkan (il persiste avec le stub et sans cache).
Ce qu'il n'a pas etabli : quel thread ni quelle attente.

Ce qu'il faudrait : ptrace_scope=0, ou lancer sans setsid, ou une trace futex resymbolisee.