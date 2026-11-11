# Le corps de l'init ne produit pas l'erreur la ou je l'attendais

## La trace du corps

L'instrument du round 218, filtre sur `0x33000cc0`-`0x33003000`, donne 48 decisions :

    n=0 bo=16 bi=0 t=1 g9=855687680   <-- boucle sur une table
    n=1 bo=16 bi=0 t=1 g9=855687681
    ...
    n=7 bo=16 bi=0 t=0 g9=855687687   <-- sortie apres 7 iterations

`g9` demarre a `0x3300e580` et avance d'un octet par iteration : le corps parcourt une **chaine de 8 octets**
(`855687687 - 855687680 = 7`).

## Les branches du corps

Six branches, toutes `bo=4 bi=30` :

| adresse | condition |
|---|---|
| `0x33000d0c` | EQ sur `cr7` |
| `0x33000d70` | EQ sur `cr7` |
| `0x33000da0` | EQ sur `cr7` |
| `0x33000db8` | EQ sur `cr7` |
| `0x33000e28` | EQ sur `cr7` |
| `0x33000e4c` | EQ sur `cr7` |

Chacune est precedee d'un `cr_set(ctx, 7, gpr[3], 0)` — donc d'un test de **valeur nulle** sur le retour d'un
appel.

## Ce que la fin du corps montre

    ctx.lr = 0x33000e44; ppu::call_import(ctx, base, 396);   // _sys_strncmp
    ctx.cr_set(ctx, 7, gpr[3], 0);
    if (bc_cond_at(0x33000e4c, 4, 30)) goto L_33000da4;
    gpr[0] = 1;
    goto L_33000da8;

Le corps **compare des chaines** et positionne `gpr[0]`. Il ne contient **aucune constante `0x8001`** et aucun
`return` portant un code d'erreur.

## Ce que ca etablit

| fait | valeur |
|---|---|
| constantes `0x8001xxxx` dans le corps | **0** |
| `return` avec un code d'erreur | **0** |
| ce que le corps fait | compare 8 octets, positionne `gpr[0]` |

**Le `0x80010004` ne sort pas du corps lui-meme.** Il est produit soit par le wrapper, soit par l'appelant
(`lr=0x005d27f8`) apres avoir consulte `gpr[3]`.

## Ce que ca change

Mon hypothese du round 223 — « le corps calcule l'erreur » — est **fausse**. Le corps rend une valeur que
l'appelant interprete comme un echec, ou l'appelant fabrique le code lui-meme.

## La prochaine mesure

Lire `r3` **juste apres le retour** de `_cellGcmInitBody` dans l'appelant (`mc_058`, `lr=0x005d27f8`), et
regarder ce que l'appelant en fait. C'est plus court que d'analyser le firmware.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |