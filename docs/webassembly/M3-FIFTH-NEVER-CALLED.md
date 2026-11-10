# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| Le wasm cesse d'allouer après t=100 s | round 120 | oui |
| `sceNp: ManagerGetChatRestrictionFlag` à t=99,03 s (natif) | `/tmp/cmp.log` | **oui — juste avant** |
| Natif 3 appels, wasm **0** | mesure | **oui** |
| Implémenté (`np.cpp:621`) et importé (`table.cpp`) | — | **oui — mais jamais appelé** |

# Cinquième occurrence du même schéma

## La mesure

| | natif | wasm |
|---|---|---|
| `ManagerGetChatRestrictionFlag` | **3** | **0** |
| implémenté dans notre HLE | — | **oui, ligne 621** |
| importé par le jeu | — | **oui** |

## La séquence native

    99,03 s   sceNp: ManagerGetChatRestrictionFlag -> 0   (× 2)
    120,37 s  sys_memory_allocate(0x100000) -> 0x35b00000
    120,55 s  _sys_ppu_thread_create '(4J) FullscreenProgress'
    120,55 s  _sys_ppu_thread_create '(4J) Server'
    120,55 s  sys_vm_memory_map vsize 0x4000000

Le wasm n'appelle **pas** la fonction, et **n'atteint pas** les cinq événements suivants.

## Le schéma, cinquième fois

| fonction | importée | implémentée | wasm | natif |
|---|---|---|---|---|
| `cellSpursEventFlagWait` | oui | oui | 0 | 219 207 |
| `cellGcmSetDisplayBuffer` | oui | non | 0 | 0 |
| `cellSaveDataUserListAutoLoad` | oui | oui | 0 | 4 |
| `cellSaveDataUserAutoSave` | oui | oui | 0 | 34 |
| **`sceNpManagerGetChatRestrictionFlag`** | oui | oui | **0** | **3** |

## Ce que ça signifie

Le jeu a **un chemin** qu'il n'emprunte pas dans le wasm, et ce chemin mène directement aux allocations du
monde.

Ce n'est pas un HLE manquant : la fonction existe et retourne `0`.

## Ce que ça ne dit pas

Que cette fonction **cause** le map. Elle le précède de 21 s.

## L'hypothèse

Le jeu pourrait être bloqué **avant** cet appel, sur le chemin qui y mène. Les cinq fonctions jamais
appelées sont toutes dans des modules différents (`spurs`, `gcm`, `savedata`, `sceNp`) — ce qui suggère un
**point de blocage commun en amont**, pas cinq problèmes séparés.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |