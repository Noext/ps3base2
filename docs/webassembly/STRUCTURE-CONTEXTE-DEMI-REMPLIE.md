# La structure de contexte GCM est a moitie remplie

## La mesure

    [gcmargs] context=0xd72800 cmdSize=0xfa000 ioSize=0x100000 ioAddr=0x40100000
    [gcmargs] mem[0xd72800]=0x00000000 0x00000000 0x00000000 0x00bcf278

## La structure attendue

`refs/rpcs3/rpcs3/Emu/RSX/GCM.h:26` :

    struct CellGcmContextData {
        vm::bptr<u32> begin;
        vm::bptr<u32> end;
        vm::bptr<u32> current;
        vm::bptr<CellGcmContextCallback> callback;
    };

## Ce que la memoire contient

| champ | offset | attendu | **mesure** |
|---|---|---|---|
| `begin` | +0 | une adresse | **0** |
| `end` | +4 | une adresse | **0** |
| `current` | +8 | une adresse | **0** |
| `callback` | +12 | une adresse | `0x00bcf278` |

**Seul `callback` est rempli.** `begin`, `end` et `current` sont nuls.

## Ce que ca etablit

Ce n'est **pas** un probleme de pointeur-vers-pointeur : `context = 0xd72800` est bien l'adresse de la
structure, et le jeu y a ecrit **un** champ sur quatre.

Le corps de `_cellGcmInitBody` trouve `current == 0` et ne peut pas construire le FIFO de commandes. C'est
coherent avec un retour `CELL_GCM_ERROR_FAILURE` sans qu'aucun import n'ait echoue.

## La question qui reste

**Qui devait ecrire `begin`, `end` et `current` ?** Le jeu, avant l'appel a `_cellGcmInitBody`. Sur console ces
trois champs sont remplis par l'appelant (`cellGcmInit` cote jeu) avant d'entrer dans le corps.

Deux possibilites :

1. le jeu les ecrit dans du code lifté **non atteint** ;
2. le jeu les ecrit par un chemin que notre runtime ne fournit pas (allocation, mmap).

## La prochaine mesure

Chercher les ecritures a `0xd72800`, `0xd72804` et `0xd72808` — le probe `[d7store]` du round 220 filtre deja
sur cette plage et pourrait les montrer si elles existent.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 737 751 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |