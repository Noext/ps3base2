# `r30` dans le callee vaut `0x3300c0c0`, pas `0x40100000`

## La mesure

Sans garde — via `bc_cond_at`, qui tire a l'interieur de la fonction :

    [regs] n=0 pc=0x33000624 bo=12 bi=30 t=1 r30=0x3300c0c0
    [regs] n=1 pc=0x330006b0 bo=4  bi=18 t=1 r30=0x3300c0c0
    [regs] n=2 pc=0x330006e0 bo=4  bi=18 t=0 r30=0x3300c0c0

**`r30 = 0x3300c0c0`**, constant sur les trois decisions.

## Ce que cette valeur est

`0x3300c0c0` est **exactement** la valeur que `_sys_memset` (import 401) rendait au round 223 :

    [gcmdep] import=401 ret=0x3300c0c0 lr=0x33000650

Un `_sys_memset` rend son premier argument. Donc `0x3300c0c0` est une **adresse de destination de memset**, pas
le contexte GCM.

## Ce que ca etablit

| fait | valeur |
|---|---|
| `r30` dans `f_330005ec` | **`0x3300c0c0`** |
| `gpr[30]` du corps (`= ioAddr`) | `0x40100000` |
| identiques | **non** |
| `f_330005ec` est le callee qui ecrit le contexte | **non** — il travaille sur `0x3300c0c0` |

`0x3300c0c0` est dans la zone de `libgcm_sys` lui-meme (`0x33000000`-`0x3300b7xx` pour le texte). C'est une
adresse de **tampon interne du firmware**, pas la structure du jeu.

## Ce que ca invalide

Mon raisonnement du round 236 — « le prologue place ioAddr dans r30, donc les ecritures visent 0x40100000 » —
**etait faux**. `f_330005ec` a ses propres arguments, et son `r30` n'a rien a voir avec le `gpr[30]` du corps.

## Ce qui reste vrai et mesure

| fait | source |
|---|---|
| la structure a `0xd72800` n'est remplie qu'a partir de `+0x0c` | probe `[d7store]` |
| aucune ecriture a `0x40100000` | probe `[d7store]` |
| `f_330005ec` travaille sur `0x3300c0c0` | **ce round** |

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 737 751 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |