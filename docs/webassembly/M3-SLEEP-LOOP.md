# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `f_005d2b44` fait 0 appel, c'est du calcul | `M3-NOT-A-RENDERER.md` (round 73) | oui |
| `f_005d2868` fait 1 import, 1 syscall, 2 appels | `out/ppu/mc_058.cpp` | **oui — mesuré** |
| Son unique syscall est 141 (`sys_timer_usleep`) | `out/ppu/mc_058.cpp` | oui |

# Le contenu réel de la boucle, fonction par fonction

## `f_005d2868` : un paceur, pas un renderer

| propriété | valeur |
|---|---|
| lignes | 44 |
| imports HLE | 1 |
| appels de fonction | 2 (`f_005d28e8`, `f_005d2810`) |
| **syscall** | **141 = `sys_timer_usleep`** |

Cette fonction **dort**. C'est le paceur de trame du jeu, pas un appel de rendu.

## `f_002d9220` : un autre dormeur

35 lignes, **un seul syscall, 141**. Dort aussi.

## `f_002dc1e4` : l'accesseur

14 lignes, lit `0xBE070C`, dort 300 µs.

## Ce que la boucle est vraiment

    f_002dbff8(...)                       lecture
    f_005d2b44(ctx, base)   r4=65, r5=1   calcul sur la mémoire (0 appel)
    f_005d2868(ctx, base)   r4=0          usleep (paceur)
    f_002d9220(ctx, base)   r3=65, r4=1   usleep
    -> f_002dc1e4                          lecture + usleep 300us

**Trois des quatre fonctions de la boucle dorment.** Aucune ne rend quoi que ce soit.

Ce n'est pas une boucle de trame : c'est une **boucle d'endormissement**. Le jeu se réveille, lit un
octet, calcule quelque chose, dort trois fois de suite, recommence.

## Pourquoi 56 fps alors

Les 56 fps du round 69 viennent des **flips du FIFO**, produits par d'autres threads via
`cellGcmSetFlip`. Cette boucle-ci n'y contribue pas.

Ce que la boucle consomme : `usleep` 79 540 fois pour 401 741 syscalls — environ **20 % du trafic
syscall** est ce sommeil.

## Ce que ça signifie pour le diagnostic

Le jeu a **un thread qui attend en boucle** en lisant une condition qui ne change pas. C'est un état
d'attente applicatif, exactement ce que les rounds 69-70 avaient postulé.

Chaque tour dure au minimum 300 µs + un usleep de `f_005d2868` + un de `f_002d9220`. C'est une
attente **lente et polie**, pas un spin.

## Ce que je n'ai pas établi

Quelle condition cette boucle attend, et qui devrait la satisfaire. L'accesseur lit `0xBE070C` qui
vaut 1, donc la condition testée n'est pas cet octet seul.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **caractérise la boucle** : trois de ses quatre fonctions dorment,
aucune ne rend. C'est une attente applicative, pas une boucle de trame — et les 56 fps viennent
d'ailleurs.
