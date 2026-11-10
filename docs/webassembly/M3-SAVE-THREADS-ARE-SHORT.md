# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| 5 threads manquants au wasm : `autosave`, `options_save`, `Server`, `FullscreenProgress`, `Post processing` | round 118 | oui |
| `thread_autosave` créé à t=487,7 s, `exit 0x0` immédiat | `/tmp/cmp.log` | **oui — pas un blocage** |
| `thread_options_save` créé 145 fois, à partir de t=489,6 s | `/tmp/cmp.log` | oui |
| `0x00bbe770` est une **opd**, pas une fonction | `runtime/src/ppu_runtime.cpp:524-528` | **oui — écarté** |

# Ce que les threads de sauvegarde font réellement

## Les mesures

| | natif |
|---|---|
| `thread_autosave` créé | t=487,7 s, **`exit 0x0` immédiat** |
| `thread_options_save` créé | 145 fois, dès t=489,6 s |

`thread_autosave` **se termine aussitôt** : ce n'est pas un thread qui bloque ni qui attend.

`thread_options_save` est créé **en boucle** — une fois par écriture d'options.

## Ce que j'ai écarté

`0x00bbe770` (l'entry de `thread_options_save`) n'apparaît nulle part dans `out/ppu/`.

**Ce n'est pas une fonction manquante** : c'est une **opd** (descripteur de fonction), et
`ppu_runtime.cpp:524-528` lit le code réel à l'exécution (`code = vm::read32(opd)`).

Chercher cette adresse dans la table était une erreur de méthode de ma part.

## Ce que ça change

Les threads de sauvegarde ne sont **pas** un mécanisme d'attente. Ils se créent, écrivent, sortent.

Leur absence côté wasm signifie donc simplement : **le wasm n'écrit pas de sauvegarde**. Ce que le round 112
avait déjà mesuré (`UserAutoSave` = 0).

## Ce qui reste

Sur les cinq threads manquants, deux sont des **écritures** (déjà expliquées) et trois concernent
**le monde** (`Server`, `FullscreenProgress`, `Post processing`).

Ce sont ces trois qui comptent pour le jalon 3 : ils apparaissent quand `sys_vm_memory_map` réussit.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |