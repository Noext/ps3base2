# Cinq niveaux de remontee, tous absents : la branche n'est jamais prise

## La chaine complete, mesuree

| niveau | fonction | atteinte |
|---|---|---|
| 1 | `f_0021c470` (emet `ctx.gpr[11] = 300`) | **0** |
| 2 | `f_00523e00`, `f_005240d8` | **0** |
| 3 | `f_00524034`, `f_00185b4c`, `f_00524488`, `f_00559844` | **0** |
| 4 | `f_00186ce0`, `f_00524888` | **0** |
| -- | la ligne de menu | **1** |

Chaque adresse est verifiee comme entree de table, chaque garde porte sur une seule fonction.

## Le fait remarquable

Deux fonctions de niveau 3 (`f_00524488`, `f_00559844`) ont **zero appelant statique**. Et aucun thread du
run n'utilise ces adresses comme point d'entree :

    entry 0x00bbe520  0x00bbe5f8  0x00bbe600  0x00bbe6d0  ...

**Toutes les entrees de thread sont dans la region `0x00bb????`.** Aucune n'est dans `0x0018????` ou
`0x0052????`.

## Ce que ca etablit

Le chemin du mappage memoire **n'est pas un chemin de demarrage**. Il est atteignable seulement depuis du code
qui n'est jamais execute dans un boot jusqu'au menu.

**C'est un fait de structure du jeu, pas un defaut de l'emulateur.**

## La lecture

Le jeu, dans son parcours jusqu'au menu, n'a aucune raison d'appeler `sys_vm_memory_map`. Cette fonction sert
a creer une zone memoire pour le monde — donc apres le menu, quand le joueur lance une partie.

**Et le `sys_vm_memory_map` du natif survient a t=120 s**, apres que le jeu a recu des appuis.

## Ce que ca change pour le diagnostic

| lecture precedente | lecture actuelle |
|---|---|
| le wasm n'atteint pas le mappage par defaut | le mappage n'est **pas dans le parcours du menu** |
| il manque quelque chose au wasm | il manque un **evenement declencheur** au wasm |

Le round 114 avait mesure qu'avec 600 appuis le jeu cree `GetFriendsThreadProc` et progresse. Le mappage
demande peut-etre **un appui de plus** : celui qui lance une partie.

## Ce qu'il faudrait tester

Une sequence qui **selectionne une entree de menu** (pas seulement « appuyer ») : le jeu doit passer du menu a
l'ecran de creation de monde. Les rounds 115-116 avaient utilise `down` puis `cross` en aveugle.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 5 criteres sur 6 |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |