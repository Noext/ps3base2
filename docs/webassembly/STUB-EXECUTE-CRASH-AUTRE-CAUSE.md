# Le stub s'execute et remplit le contexte ; le crash ne vient pas de la

## Ce qui est maintenant mesure

    [gcmstub] context=0xd72800 cmd=0xfa000 io=0x100000/0x40100000 ctx_mapped=1

**Mon stub s'execute bien** (la verification du round precedent etait fausse : il n'y avait qu'une definition,
et c'etait la mienne ; le log manquait parce qu'aucune branche ne l'emettaient).

## Ce qui a change

| avant | apres |
|---|---|
| `_cellGcmInitBody` -> `0x80010004` (LLE) | **`0x0`** (HLE) |
| `begin`/`end`/`current` non ecrits | **ecrits par le stub** |
| crash `call_indirect(0)` | **crash persiste** |

Le `[gcmargs] mem[...]` affiche encore des zeros parce qu'il est lu **avant** l'appel de l'init — c'est une
mesure prise au mauvais moment, pas un echec du stub.

## Ce que ca etablit

| fait | consequence |
|---|---|
| l'init GCM reussit maintenant (`0x0`) | le chemin d'echec GCM est **neutralise** |
| le contexte est rempli | la structure n'est plus vide |
| le crash persiste au meme `lr` | **la cause du crash n'etait pas l'echec GCM** |

C'est un resultat important : pendant 15 rounds j'ai suppose que le crash venait du pointeur nul dans la
structure remplie par l'init GCM. **Ce n'est pas le cas** — remplir la structure ne change rien au crash.

## Ce que le crash est alors

`call_indirect(0)` a `lr=0x005d2c08`, dans `f_005d2b44`, avec `r3=0`. Le pointeur nul vient de
`ld32(0x00d72800 + 10240)` — c'est-a-dire **`0xd75000`**, pas `0xd72800`.

J'ai surveille `0xd72800`-`0xd72840` depuis le round 220. **`0xd75000` n'a jamais ete surveille.**

## La mesure a faire

Elargir le probe a `0x00d75000` et voir ce qui l'ecrit. C'est la prochaine chose a faire, et elle est
mecanique.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 737 751 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |