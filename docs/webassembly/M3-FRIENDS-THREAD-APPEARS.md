# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| 16 appuis → 11k/s de draws | round 115 | oui |
| 4 appuis → 8k/s | round 114 | oui |
| `(4J) GetFriendsThreadProc` manquait au wasm | round 57 | **oui — il apparaît maintenant** |

# 600 appuis : plus de threads, mais pas plus de draws

## La mesure

| appuis | draws | threads |
|---|---|---|
| 0 | 3k/s | 17 |
| 4 | 8k/s | 17 |
| 16 | **11k/s** | 17 |
| **600** | **9k/s** | **29** |

## Ce qui change vraiment

Les **threads passent de 17 à 29**. Et un thread apparaît qui n'existait **jamais** avant :

    (4J) GetFriendsThreadProc  ×9

C'est précisément l'un des 8 threads que le round 57 listait comme **manquants** au wasm.

## Ce que ça signifie

Le jeu, avec 600 appuis, atteint une **phase de son parcours** où il crée des threads de réseau social
(`GetFriendsThreadProc`). C'est un écran de menu qui interroge les amis PSN.

**Le jeu progresse donc bien dans son parcours** — jusqu'à un écran qu'il ne dépassait pas.

## Ce qui n'augmente pas

Les draws **baissent** de 11k/s à 9k/s. Ce n'est pas une régression : c'est un **écran différent**. Un menu
dessine moins qu'un écran de chargement.

C'est cohérent avec un jeu qui a quitté le chargement pour un menu.

## Ce que ça ne prouve pas

Que le menu soit **affiché**. Le stub RSX ne rend rien. Mais le jeu **agit** comme s'il y était.

## La piste

`GetFriendsThreadProc` interroge `sceNp` (réseau PSN). Le round 63 avait mesuré que `sceNp` est offline.
Ce thread se termine probablement en erreur — mais il est **créé**, ce qui prouve l'avancement.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |