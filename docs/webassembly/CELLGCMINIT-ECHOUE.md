# `_cellGcmInitBody` echoue avec `0x80010004`

## La mesure

Un probe sur l'import 119 :

    [imp119] _cellGcmInitBody called lr=0x005d27f8 r3=0x6001f740
             ret=0xffffffff80010004

Le retour, en 32 bits : **`0x80010004`**.

## Ce que ce code est

`0x80010004` est `CELL_GCM_ERROR_FAILURE` — l'echec generique de cellGcmSys. Ce n'est pas `CELL_OK`.

| fait | valeur |
|---|---|
| `_cellGcmInitBody` appele | **oui** |
| `lr` de l'appel | `0x005d27f8` — dans `mc_058`, 44 octets avant l'appel a `f_005d2b44` |
| valeur de retour | **`0x80010004`** |
| libgcm_sys | **LLE** (`libgcm_sys.sprx` -> `0x33000000`), 1381 fonctions liftées |

## Pourquoi c'est decisif

L'`lr` `0x005d27f8` est dans la **meme fonction** que `f_005d2b44` (`mc_058`, dont le call au crash est a
`lr=0x005d2c08`). Le code qui echoue et le code qui lit le pointeur nul sont **dans la meme routine**.

Autrement dit : le jeu initialise GCM, **GCM echoue**, et le jeu continue quand meme jusqu'a lire la structure
que l'initialisation aurait du remplir.

## Ce que ca explique

| fait observe | explication |
|---|---|
| `0x00d72800` jamais ecrit | l'initialisation qui devait l'ecrire a echoue |
| la structure voisine `0x00d729e8` est ecrite | un autre chemin l'ecrit, independant |
| le jeu continue apres l'echec | il ignore le code de retour |

## Ce qu'il faudrait

Trouver **pourquoi** le code LLE de `_cellGcmInitBody` rend `CELL_GCM_ERROR_FAILURE`. Comme c'est du LLE (le
vrai code du firmware), l'echec vient d'une dependance non satisfaite :

- un `sys_memory_allocate` refuse,
- un mappage memoire absent,
- un appel interne a un autre module qui echoue.

Le chemin est dans `libgcm_sys_000.cpp`, et l'appel est a `lr=0x005d27f8`.

## Les hypotheses ecartees par ce round

| hypothese | statut |
|---|---|
| `_cellGcmInitBody` reussit | **faux** — `0x80010004` |
| c'est mon stub HLE qui repond | **faux** — c'est le LLE |
| la structure est ecrite par un DMA RSX | **non demontre** — mais l'echec d'init suffit a l'expliquer |

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |