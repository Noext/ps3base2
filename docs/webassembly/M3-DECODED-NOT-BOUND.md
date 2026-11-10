# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le critère mécanique réel du monde est `sys_vm_memory_map ->` | `tools/drive_world.py:59-62` | oui |
| Natif 2 appels, wasm 0 | `/tmp/cmp.log`, `/tmp/def.log` | oui |
| Il survient à t=120,55 s, après 3 allocations et la création de `FullscreenProgress`/`Server` | `/tmp/cmp.log` | oui |

# La contradiction mesurée

## Les nombres

| | natif | wasm |
|---|---|---|
| PNG décodés (`pngdec`) | 941 | **911** |
| `cellPngDec` appels | 106 | **103** |
| **`texloc` (liaisons)** | **40** | **0** |
| ouvertures `MediaPS3.arc` | 49 | 27 |

**Le wasm décode 911 PNG — presque autant que le natif — et n'en lie aucun.**

## Ce que ça implique

Le chemin `cellPngDec` fonctionne : les images sont décodées, les pixels écrits en mémoire invitée.
Ce qui manque est l'étape **après** : prendre ces pixels et les présenter comme une texture au RSX.

Cette étape vit dans `rsx_renderer.cpp` (le `texloc` du round 79). Le stub ne la fait pas.

Donc : **le jeu décode ses assets correctement, et rien ne les affiche.** C'est cohérent avec le
`texmask=0x0` du round 85 — le guest n'écrit pas de registre de texture parce qu'il attend que le
décodage aboutisse à une liaison, ce qui ne se produit jamais côté wasm.

## La chronologie

| | natif | wasm |
|---|---|---|
| ouverture d'archive n°25 | t=11,3 s | t=17,2 s |
| ouverture n°27 | t=11,9 s | t=18,7 s |
| pause de 65 s | oui | **non atteinte** |
| `sys_vm_memory_map` | t=120,5 s | **jamais** |

Le wasm est plus **lent** sur cette phase (6× sur les premières ouvertures) et ne franchit pas la
pause que le natif traverse.

## Ce que ça ne dit pas

Je n'ai pas établi si la lenteur est la cause ou la conséquence. Deux lectures :

1. Le wasm est lent parce qu'il fait autre chose (traduction, scheduling) et n'atteint pas le point.
2. Le wasm est lent **et** bloqué, les deux étant des symptômes d'un même problème.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | **non** (0 contre 2) |

Le jalon 3 reste ouvert. Ce round **mesure une contradiction nette** : 911 PNG décodés, zéro liaison.
Le décodage fonctionne, la présentation au RSX n'existe pas.
