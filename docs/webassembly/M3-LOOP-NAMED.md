# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `f_002dc1e4` est un accesseur, l'octet vaut 1 | `M3-BYTE-ALREADY-SET.md` (round 71) | oui |
| `pc=0x002dc20c` est `sys_timer_usleep` dans cet accesseur | `out/ppu/mc_028.cpp` | oui |
| L'appelant unique est `f_002dc29c` | `out/ppu/mc_028.cpp` | **oui — mesuré par garde** |

# La chaîne d'appel, remontée par la mesure

## Le garde sur l'entrée de l'accesseur

En gardant l'entrée de `f_002dc1e4`, on voit le `lr` de chaque appel :

    [acc] #0 entry lr=0x2dc33c r1=0x6001f900
    [acc] #1 entry lr=0x2dc33c r1=0x6001f900
    ... (répété, même lr, même r1)

**L'appelant est toujours `0x2dc33c`**, dans `f_002dc29c`, et `r1` ne change pas. C'est donc une
boucle qui appelle l'accesseur sans rien changer entre les tours.

## Le corps de cette boucle

    42 lignes avant l'appel :
    f_002dbff8(...)                       /* quelque chose qui lit */
    f_005d2b44(ctx, base)   r4=65, r5=1   /* le renderer d'origine */
    f_005d2868(ctx, base)   r4=0          /* autre appel de rendu */
    f_002d9220(ctx, base)   r3=65, r4=1   /* -> syscall 141 (usleep) */
    -> f_002dc1e4                          /* l'accesseur, puis usleep 300us */

Chaque tour : un appel de rendu, un usleep, une lecture d'octet.

## La chaîne complète

| fonction | lignes | boucle ? |
|---|---|---|
| `f_002dc1e4` (accesseur) | 14 | non |
| `f_002dc29c` | 103 | **appelle en boucle** (mesuré) |
| `f_002d63e4` | 21 | non — linéaire |
| au-dessus | — | non déterminé |

## Ce que ça établit

Le bouclage est **dans `f_002dc29c`**, pas dans ses appelés. Et chaque tour contient :

- un appel à `f_005d2b44` avec `r4=65`, `r5=1`
- un appel à `f_005d2868`
- un `usleep`
- une lecture d'octet

C'est une **boucle de trame** : rendu, attente, test. Le jeu présente des frames et teste une
condition à chaque tour.

## Ce que ça éclaire

Cohérent avec le round 69 : 56 fps, boucle serrée de présentation. La boucle est
`f_002dc29c`, et elle contient bien du travail de rendu (`f_005d2b44`, `f_005d2868`).

Le jeu **n'est pas bloqué** : il boucle sur une trame qui inclut le rendu. Ce qu'il attend, c'est
la condition testée par l'accesseur — et cette condition dépend des octets voisins à `0xBE0708`
et `0xBE0709`, mesurés à 48 et 16.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **identifie la boucle** : `f_002dc29c`, un cycle rendu → usleep →
test, mesuré par le `lr` constant de l'appelant.
