# Le crash est le `callback` du contexte, jamais initialise

## Le site exact

    L_5d2bf0:
        ctx.gpr[9] = ld32(ctx.gpr[31] + 12);        // callback = 0x00bcf278
        ctx.gpr[0] = ld32(ctx.gpr[9] + 0);          // <- lit 0
        ctx.ctr    = ctx.gpr[0];
        ctx.gpr[2] = ld32(ctx.gpr[9] + 4);
        ctx.lr = 0x5d2c08; call_indirect(ctx, base, uint32_t(ctx.ctr));

`r31` est le contexte (`0x00d72800`), `+12` est `callback`, et son **premier mot** est utilise comme cible
d'appel indirect. Il vaut **zero**.

## Pourquoi on arrive la

    gpr[9] = ld32(r31 + 8) + 16        // current + 16
    gpr[0] = ld32(r31 + 4)             // end
    cr_setu(7, gpr[9], gpr[0])
    if (bc_cond_at(0x5d2b7c, 12, 29)) goto L_5d2bf0;

`bi=29` = bit 3 du champ 7, teste avec `bo=12` (GT). La branche est prise quand `current + 16 > end`.

Mon stub ecrit `current = 0x40100000` et `end = 0x401fa000`, donc `current + 16 < end` et la branche ne devrait
**pas** etre prise.

## La contradiction, et ce qu'elle signifie

| fait | valeur |
|---|---|
| le crash est a `lr=0x5d2c08`, **dans** `L_5d2bf0` | mesure |
| le stub ecrit `current = 0x40100000`, `end = 0x401fa000` | `[d7store]` |
| `current + 16 > end` est faux | arithmetique |

Donc **soit la branche est prise pour une autre raison, soit `r31` n'est pas `0xd72800`** dans cet appel.

`f_005d2b44` est appele **plusieurs fois** (deux occurrences dans `L_2da534`). La premiere peut avoir un
contexte different de celui que j'ai rempli.

## Ce que l'absence d'ecriture a `0x00bcf278` etablit

    aucune ecriture dans 0x00bcf270-0x00bcf2b0

**Le jeu ne remplit jamais ce callback.** Sur console, c'est `_cellGcmInitBody` qui l'installe — la reference
montre `gcm_cfg.current_config` et un callback de FIFO.

## La correction evidente

Le stub HLE doit aussi ecrire `callback` a `+12` : un pointeur vers une petite structure dont `+0` est une
fonction de vidage de commandes. C'est exactement ce que le prompt demande — « un stub qui compte les
commandes RSX ».

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 737 751 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |