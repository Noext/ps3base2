# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| 3 threads du monde manquent au wasm | round 118 | oui |
| `sys_vm_memory_map` natif à **t=120,5 s** | `/tmp/cmp.log:2868` | **oui — bien plus tôt que je ne croyais** |
| `FullscreenProgress` + `Server` créés à **t=120,55 s**, juste avant le map | `/tmp/cmp.log` | **oui — la séquence** |
| Le wasm crée **0** de ces deux threads | `/tmp/big.log` | **oui — le fait décisif** |

# La séquence exacte du monde

## Chez le natif

    120,378 s  sys_memory_allocate(0x100000) -> 0x35b00000
    120,536 s  sys_memory_allocate(0x100000) -> 0x35c00000
    120,551 s  _sys_ppu_thread_create '(4J) FullscreenProgress'
    120,551 s  _sys_ppu_thread_create '(4J) Server'
    120,554 s  sys_vm_memory_map vsize 0x4000000 -> 0x100c0000

**Trois événements en 200 ms**, à t=120,5 s. Le map dépend de la création des deux threads.

## Chez le wasm

| | natif | wasm |
|---|---|---|
| `FullscreenProgress` | **t=120,5 s** | **jamais** |
| `Server` | **t=120,5 s** | **jamais** |
| `sys_memory_allocate` après t=100 s | oui | **0** |
| `sys_vm_memory_map` | oui | **0** |

## Ce que ça établit

Le wasm **s'arrête d'allouer de la mémoire** après t=100 s. Il ne crée plus de thread. Il ne mappe plus rien.

C'est un **arrêt net** de la progression mémoire, pas une lenteur.

## Ce que ça corrige

Je croyais que `sys_vm_memory_map` était une étape tardive (après des centaines de secondes). En réalité
elle se produit à **t=120,5 s**, et mon run long l'a **largement dépassée** sans l'atteindre.

## Ce que le wasm fait à la place

À t=118-120 s, le wasm écrit `clipwrite` (registres de surface) et fait du `putllc` (SPU). Il **tourne**,
mais dans une boucle de rendu, pas dans une progression d'initialisation.

## La question précise

Qu'est-ce qui, entre t=100 et t=120 s, devrait provoquer les deux `sys_memory_allocate` puis les deux
threads ?

Le natif les fait. Le wasm ne les fait pas.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |