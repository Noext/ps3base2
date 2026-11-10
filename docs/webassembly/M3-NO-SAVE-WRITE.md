# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| Natif : 240 `Save thread`, 34 `UserAutoSave` ; wasm : 0 et 0 | round 111 | oui |
| `cellSaveDataUserAutoSave` **est implémenté** | `runtime/src/hle/cellSaveData.cpp:668` | **oui** |
| Le jeu **importe** `cellSaveDataUserAutoSave` | `out/ppu/table.cpp` : 1 | **oui — mais ne l'appelle pas** |

# Le chemin d'écriture n'est jamais emprunté

## La mesure

| | natif | wasm |
|---|---|---|
| `cellSaveDataUserAutoSave` importé | — | **oui** |
| `UserAutoSave` appelé | **34** | **0** |
| `Save thread` créé | **240** | **0** |
| `savedata: write` | **78** | **0** |

## Ce que le wasm fait

    UserAutoLoad  'BLES01976-OPTIONS' (existing)  -> 0    (lecture, réussie)

## Ce que le wasm ne fait pas

    UserAutoSave  ...                                      (jamais appelé)

## La signature, déjà vue

C'est la **quatrième** fois dans ce diagnostic qu'une fonction est **importée, implémentée, et jamais
appelée** :

| fonction | importée | implémentée | appelée (wasm) | appelée (natif) |
|---|---|---|---|---|
| `cellSpursEventFlagWait` | oui | oui | 0 | 219 207 |
| `cellGcmSetDisplayBuffer` | oui | non | 0 | 0 |
| `cellSaveDataUserListAutoLoad` | oui | oui | 0 | 4 |
| **`cellSaveDataUserAutoSave`** | oui | oui | **0** | **34** |

## Ce que ça signifie

Le jeu **n'entre pas** dans son chemin d'écriture de sauvegarde. Ce n'est pas un défaut de HLE : la fonction
existe et accepte l'appel.

Et l'écriture de sauvegarde **précède** la bascule de rendu chez le natif (t=487,7 s → t=511 s).

## La chaîne, mise à jour

    le jeu lit ses options (les deux runs)
      → le natif écrit une sauvegarde (t=487,7), le wasm non
        → le natif crée 240 threads de sauvegarde, le wasm 0
          → le natif bascule en mode jeu (t=511), le wasm reste à 3k/s

## Ce que je n'ai pas établi

Que l'écriture **cause** la bascule. Elle la précède de 23 s.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |