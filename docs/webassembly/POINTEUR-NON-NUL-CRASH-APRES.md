# Le pointeur n'est plus nul : le crash vient de `current + 16` contre `end`

## Ce que le probe d'entree montre maintenant

    [impl-entry] addr=0x005d2b44 in_map=1
    [entry544] lr=0x002da54c r3=0x40100000 r4=0xff

**`r3 = 0x40100000`** — le pointeur n'est **plus nul**. Le stub HLE a rempli la structure, et la valeur arrive
bien jusqu'a `f_005d2b44`.

C'est le premier round ou le pointeur n'est pas zero a cet endroit.

## Ce que la fonction fait de ce pointeur

    gpr[31] = gpr[3]                                  // le contexte
    gpr[12] = ld32(gpr[31] + 8)                       // current = 0x40100000
    gpr[0]  = ld32(gpr[31] + 4)                       // end     = 0x401fa000
    gpr[9]  = gpr[12] + 16                            // current + 16
    cr_setu(ctx, 7, gpr[9], gpr[0])                   // compare current+16 a end
    if (bc_cond_at(0x5d2b7c, 12, 29)) goto L_5d2bf0;   // si current+16 > end -> L_5d2bf0

Avec `current = 0x40100000` et `end = 0x401fa000`, `current + 16 = 0x40100010` est **inferieur** a `end`, donc
la branche n'est pas prise et le code continue normalement dans la zone de commandes.

## Ce que ca change

| fait | round 238 | round 239 |
|---|---|---|
| `r3` a l'entree de `f_005d2b44` | `0` | **`0x40100000`** |
| structure du contexte | vide | **remplie par le stub** |
| crash | `call_indirect(0)` | **persiste** |

Le stub a donc **fait son travail** : la valeur n'est plus nulle. Le crash a une autre origine, **apres** ce
point.

## Ce qui reste a mesurer

Le `call_indirect(0)` a `lr=0x005d2c08`. Entre `0x5d2b44` et `0x5d2c08`, le code lit d'autres champs de la
structure. Celui qui vaut zero est a trouver — et cette fois je sais que ce n'est pas `+8`.

Le candidat le plus probable est un champ de **callback** lu plus loin, ou une entree de la table de commandes
a `0x40100000` qui n'est pas initialisee.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 737 751 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |