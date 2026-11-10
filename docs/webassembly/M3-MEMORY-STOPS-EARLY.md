# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| Le wasm cesse d'allouer après t=100 s | round 120 | oui |
| 5 threads manquants (dont `Server`, `FullscreenProgress`) | round 118 | oui |
| **wasm : 65 allocations, 91 Mo. natif : 119 allocations, 152 Mo** | mesure | **oui** |

# Le wasm s'arrête de demander, sans échouer

## La mesure

| | natif | wasm |
|---|---|---|
| `sys_memory_allocate` | **119** | **65** |
| mémoire utilisée | 0x91a0000 (152 Mo) | **0x57a0000 (91 Mo)** |
| erreurs `ENOMEM` | 0 | **0** |
| dernière allocation | t=169,7 s | **t=38,4 s** |

## Ce que ça établit

Le wasm **a de la place** (91 Mo sur un budget bien plus grand) et **cesse de demander**.

Ce n'est pas un refus : aucune erreur. C'est le **guest** qui n'émet plus l'appel.

## Où ça s'arrête

Le wasm s'arrête à **t=38 s**, bien avant les événements de t=120 s.

Le natif, au même point (t=38 s), avait déjà alloué davantage et continuait.

## Ce que ça implique

La progression mémoire est **bloquée par le parcours du guest**, pas par le budget.

Et l'arrêt à t=38 s précède de 80 s les transitions d'état mesurées au round 133.

## Ce que ça ne dit pas

Pourquoi le guest n'alloue plus. Cela pourrait être normal (il a fini son chargement) ou anormal (il attend).

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |