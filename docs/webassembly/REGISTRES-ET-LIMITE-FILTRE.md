# Les registres sont visibles ; la boucle avance ; le filtre reste faux

## La mesure

    [regs] n=0 bo=4  bi=30 t=1 g5=4294967295 g6=0       g8=14104744    g9=72057594037927936
    [regs] n=4 bo=4  bi=28 t=0 g5=4        g6=1280    g8=855683992   g9=0
    [regs] n=7 bo=4  bi=28 t=0 g5=4        g6=1280    g8=855683992   g9=512

## Ce que les registres disent

| registre | evolution | lecture |
|---|---|---|
| `g9` | `0` -> `512` | **progresse** |
| `g5` | `4294967295` -> `4` | change |
| `g6` | `0` -> `1280` | change |
| `g8` | `855683992` = `0x3300b578` | **une adresse**, stable |

`g9` avance de 512 a chaque iteration : **la boucle ne peut pas etre infinie par ce compteur**.

## Mais le filtre reste faux

Les `bi` observes sont **26, 28, 29, 30**. La fonction `f_002da36c` n'a que des `bi` de **0, 1 et 2** (verifie au
round 216). Donc les branches enregistrees appartiennent toujours a des **appeles**, pas a la fonction filtree.

C'est la limite etablie au round 215, et ce round ne l'a pas levee : `g_bt_lr` est le `lr` de l'appelant du
**dispatcher**, donc toute fonction appelee depuis la plage filtree passe le filtre.

## Ce que ce round apporte quand meme

L'instrument lit desormais **les registres au moment de la decision**, pas seulement `bo`/`bi`. C'est ce qui
permet de dire que `g9` progresse — un fait qu'aucune mesure precedente n'avait etabli.

## Ce qui manque, et qui est la vraie cle

L'adresse du bloc. Sans elle :

- on ne peut pas attribuer une branche a une fonction ;
- on ne peut pas comparer avec le natif, qui n'a pas ce probleme puisqu'il execute le vrai binaire.

Le lift ecrit `ctx.cia` **35 fois** dans `mc_028`, et seulement avant des syscalls. Faire ecrire `ctx.cia` a
chaque bloc demande de modifier `recomp/ppu/codegen.py`, puis de regenerer `out/ppu` — une operation de build
que je n'ai pas faite dans ce round.

## Les erreurs de ce round

| tentative | resultat |
|---|---|
| `g_bt_ctx` declare dans le sink, defini dans le runtime | **trois erreurs de linkage** (C vs C++) |
| `extern "C"` dans un corps de fonction | erreur de compilation |
| lire `g[5]` suppose que `gpr` est a l'offset 0 | **correct** — verifie dans `context.h:30` |

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 44,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |