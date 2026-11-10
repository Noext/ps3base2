# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| Les appuis natifs viennent de `drive_world.py` via xdotool | `M3-INPUT-SOURCE-CLARIFIED.md` (round 115) | oui |
| Mes quatre appuis ont multiplié le dessin par 2,7 | round 114 | oui |
| La machine à états est dans `drive_world.py` | `tools/drive_world.py:95-130` | **oui — la séquence réelle** |

# La séquence réelle du parcours

## Ce que `drive_world.py` fait

| état détecté | action |
|---|---|
| `PSNOFFLINE` | `Down`, puis `z` |
| `DIALOG` | `z` |
| `MENU` | `z` |
| `LIST` | `z` |
| `FORM` | navigation par lecture du focus |

`z` sur le clavier correspond à **`cross`** sur PS3 ; `Down` à **`down`**.

## Comparaison avec mon injection

| | `drive_world.py` | mon script (round 114) |
|---|---|---|
| touches | `Down` + `z` × plusieurs | `cross` × 3, `start` × 1 |
| logique | **adaptative** — lit l'écran à chaque tour | **aveugle** — quatre appuis fixes |
| nombre | jusqu'à 400 s | 4 |

## Ce que ça explique

Mon script a produit un effet (3k/s → 8k/s) mais ne peut pas **progresser** : il ne réagit pas à
l'état de l'écran.

Le natif, lui, **boucle** : il regarde, classe l'état, presse la touche correspondante, recommence
jusqu'à `WORLD-MAPPED`.

## La correction à appliquer

Un parcours **non adaptatif** ne peut pas reproduire ça. Deux options :

1. **Enchaîner une longue séquence** de `cross` et `down` (approximation : répéter `z` plusieurs fois
   franchit probablement `DIALOG`, `MENU`, `LIST`).
2. **Exposer l'état de l'écran** au script wasm — impossible sans renderer.

L'option 1 est réalisable : `z` est accepté dans **quatre** états sur cinq. Une séquence de dizaines de
`cross` avec des pauses devrait traverser une bonne partie du parcours.

## Ce que ça ne garantit pas

`FORM` demande une navigation précise (lire le focus). Sans retour visuel, je ne peux pas la faire.

Mais `PSNOFFLINE` → `Down` → `z` est la première étape, et elle est connue.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |