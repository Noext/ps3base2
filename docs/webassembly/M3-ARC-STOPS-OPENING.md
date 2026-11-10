# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| Le wasm décroche entre t=120 et t=170 | round 135 | oui |
| `MediaPS3.arc` ouvert à t=120,235 juste avant les allocations | `/tmp/cmp.log` | **oui** |
| **natif : 16 ouvertures après t=100. wasm : 0** | mesure | **oui — l'écart** |

# Le fichier d'archive n'est plus ouvert après t=100

## La mesure

| | natif | wasm |
|---|---|---|
| `MediaPS3.arc` au total | 49 | 34 |
| **après t=100 s** | **16** | **0** |

## La séquence native

    120,235 s  fs: open MediaPS3.arc flags 00 -> 4
    120,248 s  sys_memory_allocate(0x100000) -> 0x35a00000
    120,378 s  sys_memory_allocate(0x100000) -> 0x35b00000
    120,536 s  sys_memory_allocate(0x100000) -> 0x35c00000
    120,551 s  _sys_ppu_thread_create '(4J) FullscreenProgress'
    120,551 s  _sys_ppu_thread_create '(4J) Server'
    120,554 s  sys_vm_memory_map -> 0x100c0000

## Ce que ça établit

Le natif **ouvre une archive média** puis, dans les 300 ms, alloue et crée le monde.

Le wasm **n'ouvre plus l'archive** après t=100 s — donc n'entre pas dans cette séquence.

## Ce que ça ne dit pas

Pourquoi le wasm cesse de l'ouvrir. Il l'a ouverte **34 fois** avant.

## L'hypothèse

Le jeu lit l'archive par **requêtes asynchrones**. Si une requête reste en attente, le jeu n'en émet plus.

C'est cohérent avec le déficit de synchronisation mesuré au round 122 (12 attentes contre 219 207).

## La mesure suivante

Compter les lectures (`fs: read`) sur `MediaPS3.arc` des deux côtés : si le wasm en a moins, la lecture
asynchrone est le suspect.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |