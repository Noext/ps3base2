# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| La boucle est `f_002dc29c`, cycle rendu → usleep → test | `M3-LOOP-NAMED.md` (round 72) | **partiellement faux — voir ci-dessous** |
| `f_005d2b44` est appelé avec `r4=65`, `r5=1` à chaque tour | `out/ppu/mc_028.cpp` | oui |
| `f_005d2b44` fait 67 lignes | `out/ppu/mc_058.cpp` | oui |

# Correction : `f_005d2b44` n'est pas un renderer

Le round 72 a décrit le cycle comme « rendu → usleep → test », en supposant que
`f_005d2b44` et `f_005d2868` étaient des appels de rendu.

## La mesure

    imports        0
    real syscalls  0
    direct calls   0

`f_005d2b44` n'appelle **rien** : pas un import HLE, pas un syscall, pas même une autre fonction
liftée. C'est du **calcul pur** sur la mémoire invitée — `ld32`, `rotl`, `st32`, arithmétique.

Ce n'est donc pas un appel au renderer. C'est une fonction de manipulation de données, appelée avec
`r4=65` (0x41, peut-être un caractère ou un type) et `r5=1`.

## Ce que j'ai confondu

J'ai lu `f_005d2b44` comme « renderer » parce que ce nom était apparu dans des rounds antérieurs
comme site de crash null-`this`. Le nom de la fonction n'était pas une preuve de sa nature.

## Ce que la boucle fait donc réellement

    42 lignes avant l'appel :
    f_002dbff8(...)                       lecture
    f_005d2b44(ctx, base)   r4=65, r5=1   CALCUL sur la mémoire, pas de rendu
    f_005d2868(ctx, base)   r4=0          à vérifier
    f_002d9220(ctx, base)   r3=65, r4=1   -> usleep
    -> f_002dc1e4                          accesseur, usleep 300us

La boucle est toujours réelle et toujours celle de `f_002dc29c` — mais son contenu n'est pas
« rendu ». C'est un cycle de **calcul + attente + test**.

## Ce que ça change

Le round 69 mesurait 56 fps en s'appuyant sur les flips du FIFO. Ces flips viennent d'ailleurs —
de `cellGcmSetFlip` appelé par d'autres threads. La boucle de `f_002dc29c` n'est pas celle qui
produit les flips.

Donc : il y a **au moins deux boucles actives** — celle-ci (calcul/attente) et celle qui présente
les frames. Mon round 72 les avait fusionnées à tort.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **corrige** ma description du round 72 : `f_005d2b44` ne fait
aucun appel, c'est du calcul sur la mémoire. La boucle est réelle, sa nature ne l'était pas.
