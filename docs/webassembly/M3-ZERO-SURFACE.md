# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Toutes les écritures de surface passent par le chemin groupé | `M3-GROUPED-CONFIRMED.md` (round 106) | oui |
| `regs[reg >> 2] = arg` est le seul point d'écriture groupé | `lv2/rsx.cpp:587` | **oui — instrumenté** |
| `color_targets()` vide si clip = 0 | `rsx_renderer.cpp:1184-1186` | oui |

# Le jeu écrit ZÉRO dans les dimensions

## La mesure, au bon endroit

J'ai instrumenté **le seul point** où un paquet groupé écrit un registre :

    regs[reg >> 2] = arg;
    → grpwrite: #N reg 0x%03x = 0x%08x

## Le résultat

    grpwrite: #0  reg 0x200 = 0x00000000    ← clip horizontal
    grpwrite: #1  reg 0x204 = 0x00000000    ← clip vertical
    grpwrite: #2  reg 0x208 = 0x00000121    ← format couleur
    grpwrite: #3  reg 0x20c = 0x00000040    ← pitch
    grpwrite: #4  reg 0x210 = 0x00000000    ← offset couleur A
    grpwrite: #5  reg 0x214 = 0x00000000    ← offset zeta
    grpwrite: #6  reg 0x218 = 0x00000000
    grpwrite: #7  reg 0x21c = 0x00000040
    grpwrite: #8  reg 0x220 = 0x00000001
    grpwrite: #9  reg 0x1a08 = 0x00030101   ← texture 0 control
    grpwrite: #11 reg 0x1a0c = 0x00060000   ← texture 0 format
    grpwrite: #12 reg 0x1a14 = 0x02052000   ← texture 0 adresse

## Ce que ça change radicalement

Les rounds 103-106 concluaient « le guest n'écrit pas les dimensions ». **C'est faux.**

Le guest **écrit** `0x200` et `0x204` — **avec la valeur zéro**. C'est une écriture délibérée d'une surface
de taille 0×0.

## Ce que ça signifie

Le jeu **configure une surface vide**. Ce n'est ni une omission, ni un arrêt au milieu : c'est une
configuration **complète et cohérente** d'une surface de dimensions nulles.

Et c'est cohérent avec tout le reste : offset couleur A = 0, zeta = 0, dimensions = 0. Le jeu décrit une cible
de rendu **qui n'existe pas encore**.

## L'interprétation la plus probable

Le jeu **remet à zéro** son état de surface avant de le configurer. C'est une pratique courante : on désarme
la cible pendant qu'on la reconfigure.

Le problème est donc : **la seconde configuration (avec les vraies dimensions) n'arrive jamais.**

## Ce que ça déplace encore

La question devient : le jeu écrit-il `0x200` **une seconde fois** avec une valeur non nulle ? Mon probe
s'arrête à 16 écritures. Il faut compter **combien de fois** `0x200` est écrit et avec quelles valeurs.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |

Le jalon 3 reste ouvert. Ce round **renverse la lecture des trois rounds précédents** : le guest écrit bien
les dimensions, avec la valeur zéro. C'est une surface vide délibérée, pas une omission.
