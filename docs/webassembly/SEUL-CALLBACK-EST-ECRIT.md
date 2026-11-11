# Seul `callback` est ecrit ; les trois autres champs jamais

## La mesure

Probe sur toutes les ecritures dans `0x00d72800`-`0x00d72810` pendant tout le boot :

    [d7store] ea=0x00d7280c size=4 v=0x78f2bc00

**Une seule ecriture.** `0xd7280c` est le champ `callback` (`0x00bcf278` en little-endian).

| champ | offset | ecrit ? |
|---|---|---|
| `begin` | +0 | **non** |
| `end` | +4 | **non** |
| `current` | +8 | **non** |
| `callback` | +12 | **oui**, une fois |

## Ce que ca etablit

Parmi les deux possibilites du round 229, c'est la **premiere** : le code lifté ecrit `callback` et **rien
d'autre**. Les trois champs ne sont ecrits par **aucun code** — ni PPU lifte, ni chemin du runtime.

## Ce que la reference fait

`refs/rpcs3/rpcs3/Emu/Cell/Modules/cellGcmSys.cpp:416` : le corps recoit `context` et **construit lui-meme** le
FIFO de commandes a partir de `cmdSize`, en remplissant `begin`, `end` et `current`. Le jeu, lui, ne fournit
que `callback`.

**Donc c'est le corps qui doit ecrire ces trois champs**, et il ne le fait pas.

## Ce que ca implique

| fait | consequence |
|---|---|
| le jeu ne fournit que `callback` | c'est conforme a la reference |
| le corps doit remplir `begin`/`end`/`current` | il ne le fait pas |
| aucune ecriture a ces offsets de tout le boot | **mesure** |

Le corps `f_33000cc0` **ne remplit pas la structure**. Soit il echoue avant, soit une de ses branches prend le
chemin d'erreur a cause d'un etat que notre runtime ne fournit pas.

## La piste la plus directe

Le corps lit `current` a un moment pour decider s'il peut allouer. Si `current == 0` a l'entree, il peut
conclure a une erreur **avant** d'ecrire quoi que ce soit. C'est coherent avec tout ce que j'ai mesure.

Mais la reference **ne lit pas `current` en entree** : elle l'ecrit. La difference est peut-etre dans le corps
du firmware, que je dois lire plutot que supposer.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 737 751 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |