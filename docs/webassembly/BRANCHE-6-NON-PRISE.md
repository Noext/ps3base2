# La branche qui saute Configure n'est pas prise

## Ce que la source montre

La sixieme branche de `f_002da36c` :

    ctx.gpr[3] = ld16(sp + 130);            // hauteur = 0x2d0
    ctx.cr_set(ctx, 0, int32(gpr[3]), 576);
    if (ppu::bc_cond(ctx, 4, 1)) goto L_2da4c0;    // teste EQ

`bi=1` designe **EQ** dans `cr0`. Avec `h = 0x2d0`, EQ = 0, donc la branche **n'est pas prise** et le code
continue.

## Ce qui suit, et qui ne contient aucun retour

Entre cette branche et l'appel a l'import 142 (`cellVideoOutConfigure`) :

| mesure | valeur |
|---|---|
| taille du segment | 3 985 octets |
| `return;` | **0** |
| `goto` | 5, dont `L_2da4c0` (la cible de la branche), `L_2da4f8`, `L_2da534`, `L_2da5bc`, `L_2da590` |

**Aucun retour.** Le flux qui ne prend pas la branche doit donc atteindre `Configure`.

## Ce que la trace dit pourtant

Le `[videoimp]` du round 211 montre que l'import 142 **n'est jamais atteint**. Et la trace par branchement ne
montre **aucun `bi=1`**, ce qui est coherent avec « la branche n'est pas prise ».

## Les deux faits sont compatibles, et c'est ca qui est interessant

| fait | consequence |
|---|---|
| branche 6 non prise | le code continue vers `Configure` |
| import 142 jamais atteint | le code **ne continue pas** |

Donc **le flux n'arrive jamais a la branche 6**. Il entre dans la fonction, execute la premiere moitie, et
quitte par un chemin que ni la trace (filtre par `lr`) ni les probes d'import ne voient.

## Ce que la trace a effectivement capture

`bo=12 bi=2` apparait dans la trace **et** dans la source (branche 2 et 3). Les autres (`bi=26/28/29/30`) sont
d'autres fonctions, comme etabli au round 215.

Donc la trace voit au moins **une** branche de la bonne fonction : elle est bien appelee. Mais elle n'atteint
pas la sixieme.

## La question qui reste

Entre l'entree de `f_002da36c` et la branche 6, quel `goto` sort de la sequence lineaire ? Les branches 1 a 5
menent a `L_2da478`, `L_2da458`, `L_2da428`, `L_2da41c` — toutes **avant** la 6. Une de ces branches prise
saute par-dessus la 6.

La prochaine mesure : lire les quatre labels pour voir lequel saute effectivement la branche 6.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 44,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |