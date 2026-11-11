# Le filtre capture la mauvaise fonction

## Ce que la source dit

`f_002da36c_body` contient **9 branches**, toutes identifiees :

| # | `bo` | `bi` | cible |
|---|---|---|---|
| 1 | 4 | 1 | `L_2da478` |
| 2 | 12 | 2 | `L_2da458` |
| 3 | 4 | 2 | `L_2da458` |
| 4 | 12 | 0 | `L_2da428` |
| 5 | 12 | 0 | `L_2da41c` |
| **6** | **4** | **1** | **`L_2da4c0`** — saute `Configure` |
| 7 | 12 | 2 | `L_2da4f8` |
| 8 | 12 | 2 | `L_2da534` |
| 9 | 12 | 2 | `L_2da5bc` |

## Ce que la trace dit

    bo=12 bi=2    bo=12 bi=26    bo=12 bi=29    bo=12 bi=30
    bo=4  bi=28   bo=4  bi=30

**Aucun `bi` de 0 ou 1.** Ce sont des conditions d'autres fonctions.

## La cause

`g_bt_lr` est pose dans le dispatcher a partir de `ctx.lr` : c'est le **retour de l'appel en cours**, donc
l'adresse de l'**appelant** de la fonction dispatchee. Quand `f_002da36c` appelle `f_005d2b44`, le `lr` vaut
`0x2da54c` — dans la plage du filtre — mais les branches enregistrees appartiennent a `f_005d2b44`, pas a
`f_002da36c`.

Le filtre est donc **structurellement incapable de distinguer** « branches de la fonction A » de « branches de
la fonction B appelee par A dont le `lr` tombe dans la plage de A ».

## Ce qu'il faudrait

Le **pointeur d'instruction courant**, pas le `lr`. Or `ctx.cia` n'est ecrit qu'avant les syscalls (35 fois dans
`mc_028`), pas a chaque branchement : le code lifté ne le met pas a jour.

Deux voies, aucune essayee :

1. Faire ecrire `ctx.cia` par chaque bloc de base au lift — demande de regenerer `out/ppu`.
2. Passer l'adresse du bloc a `bc_cond` par un parametre supplementaire — meme contrainte, le lift doit changer.

Les deux exigent une modification du **generateur**, pas du runtime.

## Ce que ce round etablit

| fait | valeur |
|---|---|
| branches de `f_002da36c` | **9**, listees |
| filtre par `lr` | **capture d'autres fonctions** |
| distinction possible sans changer le lift | **non** |

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 44,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |