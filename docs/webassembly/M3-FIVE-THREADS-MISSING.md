# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| 17 threads sur 22 chez le wasm | mesure sur `/tmp/big.log` vs `/tmp/cmp.log` | **oui — la mesure de ce round** |
| Le wasm n'écrit pas de sauvegarde (round 112) | `M3-NO-SAVE-WRITE.md` | oui |
| `FullscreenProgress` manquait déjà au round 77 | `M3-ASSETS-IDENTICAL.md` | oui |

# Les cinq threads manquants

## La mesure

| | wasm | natif |
|---|---|---|
| threads distincts | **17** | **22** |
| communs | 17 | 17 |
| **manquants au wasm** | — | **5** |

    (4J) FullscreenProgress
    (4J) Post processing
    (4J) Server
    thread_autosave
    thread_options_save

## Ce que chacun révèle

| thread | rôle | cohérent avec |
|---|---|---|
| `thread_options_save` | écrit les options | round 112 : `UserAutoSave` = 0 |
| `thread_autosave` | écriture périodique | round 112 : `savedata: write` = 0 |
| `(4J) Server` | thread réseau du monde | round 117 : `sceNp` offline |
| `(4J) FullscreenProgress` | écran de chargement | round 77 |
| `(4J) Post processing` | effets | après le monde |

## La cohérence

Les **cinq** threads manquants se répartissent en deux familles :

1. **écriture de sauvegarde** (`autosave`, `options_save`) — cohérent avec l'absence d'écriture mesurée ;
2. **le monde et son affichage** (`Server`, `FullscreenProgress`, `Post processing`) — cohérent avec l'absence
   de `sys_vm_memory_map`.

**Aucun thread manquant n'est un thread de menu.** Le wasm a donc créé **tous** les threads d'interface.

## Ce que ça établit

Le wasm est allé **jusqu'à l'interface complète** (17 threads, dont tous les threads de menu et d'audio).
Ce qui manque est **en aval** : l'écriture des sauvegardes, puis le monde.

## Ce que ça ne dit pas

Que ces cinq threads soient la cause. Ce sont les **étapes** manquantes, pas nécessairement le verrou.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |