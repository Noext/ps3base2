# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| Le pool de threads n'est pas la contrainte | round 123 | oui |
| Le jeu importe `cellFiberPpuSelf`, `cellFiberPpuWaitSignal` | `out/ppu/table.cpp` | **oui** |
| **Aucune implémentation de fibre** dans `runtime/src/hle/` | grep | **oui** |
| Le natif log `unimplemented cellFiber::cellFiberPpuSelf` à **t=120,63 s** | `/tmp/cmp.log` | **oui — 80 ms après le map** |

# Les fibres sont absentes des deux côtés, et le natif les atteint au moment du monde

## La séquence native

    120,551 s  _sys_ppu_thread_create '(4J) Server'
    120,554 s  sys_vm_memory_map -> 0x100c0000
    120,634 s  unimplemented cellFiber::cellFiberPpuSelf   <-- 80 ms après

## L'état de notre code

| | état |
|---|---|
| `cellFiberPpuSelf` implémenté | **non** (`runtime/src/` : 0 occurrence) |
| appelé par le wasm | **0** |
| appelé par le natif | **1** (non implémenté) |

## Ce que ça signifie

Le natif, grâce à son renderer et à son map réussi, **atteint** un chemin qui utilise les fibres — et le log
comme non implémenté.

Le wasm n'atteint pas ce chemin (pas de map), donc n'a jamais l'occasion d'appeler les fibres.

## Ce que ça n'explique pas

Les fibres ne sont **pas** la cause du blocage : le natif lui-même ne les a pas implémentées et atteint
pourtant le monde.

Elles sont un **symptôme d'avancement**, pas un verrou.

## Ce que ça donne

Un **marqueur fiable** : `cellFiberPpuSelf` est appelé ~80 ms après `sys_vm_memory_map`. Si un jour le wasm
l'appelle, c'est que le map a réussi.

## Ce que ça élimine

L'hypothèse « les fibres manquent au wasm » est **éliminée** : elles manquent **aussi** au natif, qui
progresse.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |