# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le registre de fichier montre `0x208=0x121`, `0x1A00=0x280000` | `M3-PARTIAL-SURFACE.md` (round 104) | oui |
| `method()` ne voit pas les écritures groupées | `M3-INSTRUMENT-BLIND-SPOT.md` (round 103) | **oui — confirmé** |
| `cellGcmSetSurface` n'est **pas importé** par le jeu | `out/ppu/table.cpp` : 0 | oui |

# Confirmation de l'angle mort

## La mesure

J'ai instrumenté `method()` pour enregistrer l'ordre des écritures dans `0x200-0x220` et `0x1A00-0x1C00` :

    if ((reg >= 0x200 && reg <= 0x220) || (reg >= 0x1A00 && reg < 0x1C00)) {
        if (k < 14) LOG_INFO("surfseq", "#%d reg 0x%03x = 0x%08x", k, reg, arg);
    }

Résultat : **aucune sortie**, alors que le registre de fichier contient effectivement `0x208=0x121` et
`0x1A00=0x280000`.

## Ce que ça confirme

**Toutes** les écritures de surface et de texture du wasm passent par le **chemin groupé**, qui écrit
directement dans `regs[]` sans appeler `method()`.

C'est la confirmation définitive de l'angle mort du round 103 : `method()` ne voit **aucune** écriture de
surface, pas même celles qui ont lieu.

## Ce que ça implique pour tout instrument futur

Tout compteur placé dans `method()` est **structurellement aveugle** aux registres absents de `special[]`,
c'est-à-dire à la majorité des registres RSX.

Les seuls instruments fiables pour ces registres sont :
1. **lire `regs[]`** (l'état final),
2. instrumenter le **chemin groupé** lui-même (`lv2/rsx.cpp:586-590`).

## Ce que ça change pour la suite

Le jeu utilise massivement les paquets groupés. Toute mesure de registres doit passer par le chemin groupé.

C'est une contrainte de méthode importante, et je l'avais manquée pendant une douzaine de rounds.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |

Le jalon 3 reste ouvert. Ce round **confirme définitivement** que toute écriture de surface/texture passe
par le chemin groupé, et que `method()` ne peut pas les voir.
