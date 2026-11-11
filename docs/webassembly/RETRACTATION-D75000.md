# Retractation : le pointeur nul EST a `0xd72800`, et il n'est plus nul

## L'erreur du round 238

J'avais ecrit : « le pointeur nul vient de `ld32(0xd72800 + 10240)` — c'est-a-dire `0xd75000` ».

**C'est faux.** Le code lifté est :

    L_2da534:
        ctx.gpr[29] = 0 + 14090240;                    // = 0x00d70000, PAS 0xd72800
        ctx.gpr[30] = 0 + 12451840;                    // = 0x00be0000
        ctx.gpr[3]  = ld32(ctx.gpr[29] + 10240);       // ld32(0xd72800)
        ctx.gpr[5]  = ld32(ctx.gpr[30] + 1808);        // ld32(0xbe0710)
        f_005d2b44(ctx, base);

`0xd70000 + 10240 = 0xd72800`. **C'est bien `0xd72800`**, l'adresse que je surveillais depuis le round 220.

J'avais pris la constante `10240` pour une adresse de base alors qu'elle est un **offset**.

## Ce que la mesure de ce round montre

    [d7store] ea=0x00d72800 size=4 v=0x00001040   -> valeur 0x40100000
    [d7store] ea=0x00d72804 size=4 v=0x00a01f40   -> valeur 0x401fa000
    [d7store] ea=0x00d72808 size=4 v=0x00001040   -> valeur 0x40100000

**Le stub remplit correctement la structure** :

| champ | valeur ecrite |
|---|---|
| `begin` | `0x40100000` |
| `end` | `0x401fa000` (= `0x40100000 + 0xfa000`) |
| `current` | `0x40100000` |

## Ce que ca implique

| fait | consequence |
|---|---|
| `0xd72800` contient maintenant `0x40100000` | **le pointeur n'est plus nul** |
| le crash est toujours la | il vient d'**autre chose** |

Le pointeur nul qui provoque `call_indirect(0)` n'est donc pas celui-la. Il faut lire `gpr[3]` **au moment du
crash**, dans `f_005d2b44`, plutot que de deduire son origine.

## Ce que ce round a corrige

Deux erreurs de ma part :

1. l'arithmetique du round 238 (`0xd75000` au lieu de `0xd72800`),
2. la conclusion qui en decoulait (« j'observais le mauvais offset »).

## La mesure a faire

Lire `r3` a l'entree de `f_005d2b44` et voir d'ou il vient. Le probe `[imp-entry]` existe peut-etre deja.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 737 751 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |