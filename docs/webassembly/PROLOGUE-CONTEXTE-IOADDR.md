# Le contexte du corps est `ioAddr`, pas `0xd72800` — et il n'est pas ecrit non plus

## Ce que le prologue montre

    ctx.gpr[27] = ctx.gpr[4];   // cmdSize
    ctx.gpr[26] = ctx.gpr[5];   // ioSize
    ctx.gpr[30] = ctx.gpr[6];   // ioAddr = 0x40100000
    ctx.gpr[31] = ctx.gpr[7];   // le drapeau pose par le saut = 1

Puis `f_330005ec` est appele, et c'est lui qui ecrit `[r30+0]` et `[r30+4]`.

**Donc les ecritures attendues visent `0x40100000`**, pas `0xd72800`. Le `context = 0xd72800` sert a autre
chose.

## Ce que la mesure dit

En surveillant **les deux** plages :

    [d7store] ea=0x00d7280c -> 0x00bcf278
    [d7store] ea=0x00d72810 -> 0x00bcf3a0
    [d7store] ea=0x00d72814 -> 0x00bcf3a8

**Aucune ecriture a `0x40100000`.** Les trois seules ecritures sont sur la structure du jeu.

## Ce que ca etablit

| fait | valeur |
|---|---|
| le prologue place `ioAddr` dans `r30` | **oui** |
| `f_330005ec` ecrit `[r30+0]` dans son propre corps | **oui, en source** |
| une ecriture a `0x40100000` est observee | **non** |

Donc **`f_330005ec` ne s'execute pas jusqu'a cette ecriture**, ou il recoit un `r30` different du `gpr[30]` du
corps (il a ses propres arguments, que je n'ai pas verifies).

## Ce que ce round n'a pas fait

Je n'ai pas verifie ce que `f_330005ec` recoit. C'est une mesure d'une ligne — le probe existe, il suffit de
lire `r30` a l'entree de la fonction — mais une garde sur entree casse le boot, et je n'ai pas trouve d'autre
moyen.

## Un fait qui reste solide

Le corps GCM ne remplit pas la structure a `0xd72800`, et rien d'autre ne le fait. Les trois champs ecrits le
sont par du code du jeu, pas par l'init.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 737 751 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |