# La mesure d'une ligne : le HLE ecrit bien, la hauteur n'est pas nulle

## Le resultat

    [resheights] sp=0x6001f670 w=0x0500 h=0x02d0

**1280x720**, ecrits correctement par `cellVideoOutGetResolution`.

| verification | resultat |
|---|---|
| le buffer de sortie est mappe | oui |
| le HLE ecrit la resolution | **oui**, `0x500` x `0x2d0` |
| la hauteur testee par la branche | **`0x2d0`, pas zero** |

Donc mon hypothese du round 211 — « la hauteur nulle fait sauter Configure » — est **fausse**.

## Ce que la branche fait vraiment

    cr_set(ctx, 0, int32(h), 576);        // h = 0x2d0
    if (bc_cond(ctx, 4, 1)) goto L_2da4c0;

`bc_cond(4, 1)` : `bo=4` ne decremente pas ctr, `bo & 16` est nul donc la condition est testee —
`crbit(c, 1)` = **EQ** du champ 0. Avec `h = 0x2d0`, EQ = **0**, donc la branche **n'est pas prise**.

Le code continue donc normalement.

## Ce que la mesure suivante montre

J'ai instrumente les imports 138 (`cellGcmGetLabelAddress`), 142 (`cellVideoOutConfigure`) et 124
(`cellGcmSetFlipMode`) pour voir lequel est atteint apres `GetResolution`.

**Aucun n'est atteint.** La fonction quitte apres `GetResolution` sans passer par eux.

## Ce que ca etablit

| fait | valeur |
|---|---|
| imports atteints dans `f_002da36c` | **2 sur 6** : 144, 166 |
| imports jamais atteints | 138, 142, 124 |
| crash | `call_indirect(0)` a `lr=0x005d2c08` |

Le crash est dans la **meme fonction** (le call vers `f_005d2b44` y est, ligne unique), mais apres un saut que
mes probes ne couvrent pas.

## Ce que je ne conclus pas

Je ne sais pas encore **quel chemin** mene de `GetResolution` au crash en sautant 138, 142 et 124. Une
instrumentation par garde sur les labels intermediaires le dirait, mais je ne peux pas garder un label interne
(limite etablie au round 201 : `hook_flags` ne s'applique qu'aux entrees de fonction).

La mesure correcte serait une trace d'execution par bloc de base, qui n'existe pas encore dans ce runtime.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 44,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |