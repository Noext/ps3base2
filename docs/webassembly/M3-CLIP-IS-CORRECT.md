# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le guest écrit `0x200 = 0` au début | `M3-ZERO-SURFACE.md` (round 107) | **incomplet — corrigé ici** |
| `regs[reg >> 2] = arg` est le seul point d'écriture groupé | `lv2/rsx.cpp:587` | oui — instrumenté |
| `clip_w() = R(0x200) >> 16` | `rsx_renderer.cpp:1181` | oui |

# Les dimensions SONT correctes

## La mesure

J'ai compté **chaque** écriture de `0x200`/`0x204` avec sa valeur et son maximum :

    clipwrite: reg 0x200 = 0x05000000 (#350208, max 0x05000000)
    clipwrite: reg 0x204 = 0x01e00000 (#350208, max 0x02d00000)
    clipwrite: reg 0x200 = 0x05000000 (#353280, max 0x05000000)

## La traduction

    0x05000000 >> 16 = 1280     ← clip_w
    0x01e00000 >> 16 = 480      ← clip_h

**Le guest écrit 1280×480, 353 280 fois**, avec les valeurs exactes que le natif utilise.

## Ce que ça corrige

Le round 107 concluait : « le jeu écrit zéro dans les dimensions, c'est une surface vide délibérée ».

**C'est faux.** Le jeu écrit `0` **une fois au début**, puis **353 280 fois les vraies dimensions**.

Mon dump du round 104 lisait le registre **avant** la configuration réelle — c'est-à-dire à un instant où
elle valait encore zéro.

## Ce que ça invalide

| round | conclusion | statut |
|---|---|---|
| 104 | « le guest ne configure pas les dimensions » | **faux** |
| 105 | « la dimension n'est écrite nulle part » | **faux** |
| 106 | « method() ne voit aucune écriture » | vrai, mais sans conséquence : les écritures existent |
| 107 | « surface 0×0 délibérée » | **faux** |

**Quatre rounds de conclusions tombent** devant une seule mesure correctement placée.

## La leçon de méthode

Le dump du round 104 était pris à `(i % 40) == 39` — tous les 40 tours de la boucle d'attente. Mais la
configuration de surface se produit **à chaque frame**, donc après le premier dump.

Lire un registre **une fois** ne dit rien d'un registre **réécrit en boucle**. Il fallait compter les
écritures, pas échantillonner l'état.

## Ce que ça rétablit

Le guest configure **complètement** sa surface :

| registre | valeur | sens |
|---|---|---|
| `0x200` | `0x05000000` | clip 1280 |
| `0x204` | `0x01e00000` | clip 480 |
| `0x208` | `0x121` | format couleur |
| `0x20C` | `0x40` | pitch |
| `0x1A00+` | `0x280000` | textures |

**Le renderer devrait donc créer des surfaces.** Et il ne le fait pas — parce qu'il **n'existe pas** dans le
build wasm (`PS3BASE_NO_RENDERER`).

## La conclusion inversée

Ce n'est pas le guest qui manque de configuration. **C'est nous qui n'avons pas de renderer pour la
consommer.**

C'était l'évidence depuis le début : le jalon 3 demande un stub qui compte les commandes, et un stub qui
compte ne crée pas de surfaces. Mais j'ai passé une douzaine de rounds à chercher la faute côté guest.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |

Le jalon 3 reste ouvert. Ce round **corrige quatre rounds** : le guest configure complètement sa surface
(1280×480, 353 280 fois). L'absence de surface vient de l'absence de renderer, pas d'une omission du guest.
