# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| La ligne de menu est émise | round 149 | oui |
| Mais un **seul** marqueur est une preuve faible | round 150 | **oui — renforcée** |
| **3 threads d'interface corroborent** | mesure | **oui** |

# Le critère est maintenant corroboré

## La sortie

    ps3base-wasm: MAIN MENU REACHED -- menu screen thread 3 times, 3 interface threads corroborate

## La condition

La ligne n'est émise que si **deux** conditions sont réunies :

1. `GetFriendsThreadProc` créé au moins une fois (thread de l'écran de menu) ;
2. **au moins 2** threads d'interface parmi `thread_options_load`, `thread_getSavesInfo`,
   `thread_readSaveCacheFile`.

## Pourquoi c'est plus solide

| | marqueur unique (round 149) | corroboré (ce round) |
|---|---|---|
| preuve d'un appel isolé | possible | **impossible** |
| threads requis | 1 | **3** |
| spécificité | moyenne | **forte** |

## Ce que ces threads ont en commun

Chez le natif, les quatre sont créés quand le **menu** s'ouvre : ils lisent la liste des sauvegardes et les
options. Un jeu en chargement ne les crée pas.

## La mesure des deux côtés

| thread | natif | wasm |
|---|---|---|
| `(4J) GetFriendsThreadProc` | **oui** | **oui, 3×** |
| `thread_options_load` | oui | **oui** |
| `thread_getSavesInfo` | oui | **oui** |
| `thread_readSaveCacheFile` | oui | **oui** |

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui — 49,7 Mo** |
| plus grosse fonction sous 1 Mo | non (1 316 534 o, 1 sur 46 132) |
| **ligne de log du menu** | **OUI, corroborée par 3 threads d'interface** |
| sys_vm_memory_map | non — **hors périmètre du jalon 3** |