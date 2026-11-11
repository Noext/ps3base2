# L'attribution par adresse de bloc fonctionne

## Ce qui a change

Le generateur emet desormais l'adresse du bloc a chaque branchement :

    return f"if (ppu::bc_cond_at(ctx, 0x{pc:x}, {bo}, {bi})) {{ {act} }}"

1081 branches de `mc_028.cpp` portent leur adresse. Le filtre compare `pc` a la plage de la fonction, plus
`lr`.

## La preuve

| avant (filtre par `lr`) | apres (filtre par `pc`) |
|---|---|
| `bi` = 26, 28, 29, 30 — **d'autres fonctions** | `bi` = **0, 1, 2** — ceux de `f_002da36c` |

Les trois `bi` de la fonction sont exactement ceux releves dans sa source au round 216.

## La sequence complete, 42 decisions

    n=0  bo=12 bi=2  t=0
    n=1  bo=4  bi=1  t=0
    n=2  bo=12 bi=2  t=1
    ...
    n=37 bo=12 bi=0  t=0
    n=38 bo=12 bi=0  t=0
    n=39 bo=4  bi=1  t=0     <-- LA BRANCHE 6 EST ATTEINTE
    n=40 bo=12 bi=2  t=0
    n=41 bo=12 bi=2  t=0     <-- contexte change (g8 = 0x3300b578)

## Ce que la sequence etablie

**La branche 6 (`bo=4 bi=1`) est bien executee**, et `taken=0` — elle n'est pas prise, ce qui est **correct** :
la hauteur vaut 720, superieure a 576, donc GT=1 et EQ=0.

Le flux **continue donc vers `cellVideoOutConfigure`**. Or l'import 142 n'apparait pas dans les traces
d'import : seuls 144 et 166 sont la.

## La contradiction, et sa resolution probable

| fait | valeur |
|---|---|
| la branche 6 est atteinte et non prise | **mesure** |
| le code apres elle mene a l'import 142 | lecture de source |
| l'import 142 n'est jamais appele | **mesure** |

`n=41` montre un changement de contexte : `g8` passe de `1610741376` a `855683992` (`0x3300b578`, une adresse
d'image). Le compteur `n` est celui du sink, pas de la fonction : la trace melange encore un appel sortant.

**Donc la fonction quitte apres la branche 6 par un appel**, et c'est dans cet appel que le crash survient.

## Les obstacles traverses ce round

| obstacle | correction |
|---|---|
| `fw/4.91` absent du worktree | lien symbolique vers l'arbre principal |
| `ref/nid_db.json` absent | idem |
| `bc_cond_at` declare mais non defini | definition ajoutee dans `ops.h` |
| le sink imprimait `[regs]`, pas `[blk]` | cosmétique, sans effet |

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 44,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |