# La fonction qui remplit le contexte, et pourquoi elle n'ecrit rien

## La chaine complete

    _cellGcmInitBody (OPD 0x3300b8ac)
      -> f_33000e5c  (saut)
        -> f_33000cc0  (le corps, 5 591 octets)
          -> f_330005ec   <-- ECRIT [r30+0], [r30+4], [r30+8]
          -> f_330003c4
          -> f_33000024
          -> f_3300070c
          -> f_330009a8

## Les ecritures attendues

Dans `f_330005ec` :

    ppu::st32(base, uint32_t(ctx.gpr[30] + 4), uint32_t(ctx.gpr[31]));   // end
    ppu::st32(base, uint32_t(ctx.gpr[30] + 0), uint32_t(ctx.gpr[31]));   // begin

**Ce sont exactement les champs `begin` et `end`** de `CellGcmContextData`.

## Ce que la mesure disait, et qui reste vrai

    [d7store] ea=0x00d7280c size=4 v=0x78f2bc00     <-- callback seulement

**Une seule ecriture sur toute la duree du boot**, a `callback`. Donc le code ci-dessus **n'est jamais
execute**, ou `r30` ne pointe pas vers `0xd72800`.

## Ce que mes extractions precedentes avaient manque

Jusqu'au round 230 je cherchais les ecritures **dans** `f_33000cc0`, qui n'en fait aucune sur la structure. Les
ecritures sont dans son **appele** `f_330005ec` — que je n'avais pas inspecte.

## La question qui reste, et elle est maintenant precise

Deux possibilites, a trancher par mesure :

| hypothese | comment la trancher |
|---|---|
| `f_330005ec` n'est pas atteint | garde sur `0x330005ec` — mais cela casse le boot (3 fois mesure) |
| `f_330005ec` est atteint, mais `r30` ne vaut pas `0xd72800` | probe sur l'import 401 dans cette fonction, qui donne `r30` par la pile |

La seconde est mesurable **sans** garde : le probe `[d7store]` existe deja et ne tire qu'une fois. Si
`f_330005ec` s'executait avec `r30 = 0xd72800`, il y aurait **trois** ecritures (0, +4, +8).

**Le fait qu'il n'y en ait qu'une prouve que soit la fonction n'est pas atteinte, soit `r30` differe.**

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 737 751 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |