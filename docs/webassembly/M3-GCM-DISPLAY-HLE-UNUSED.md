# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le guest n'écrit jamais de registre de surface (`0x0200-0x0214`) | `M3-WRITES-PROFILE.md` (round 92) | oui |
| `localSize` corrigé à 249 Mo, aucun effet | `M3-LOCALSIZE-NOT-THE-CAUSE.md` (round 94) | oui |
| Le jeu importe `cellGcmSetDisplayBuffer`, `GetDisplayInfo`, `GetDisplayBufferByFlipIndex` | `out/ppu/table.cpp` | **oui** |

# Ce que le jeu importe pour configurer son écran

## Les imports

    cellGcmSetDisplayBuffer
    cellGcmGetDisplayInfo
    cellGcmGetDisplayBufferByFlipIndex

**Et pas `cellGcmSetSurface`** — le jeu ne configure pas sa surface par HLE.

## Ce que fait notre HLE

| fonction | implémentée |
|---|---|
| `cellGcmGetTiledPitchSize` | oui |
| `cellGcmInitDefaultFifoMode` | oui |
| `cellGcmSetDebugOutputLevel` | oui |
| `cellGcmGetConfiguration` | oui |
| `_cellGcmInitBody` | oui |
| **`cellGcmSetDisplayBuffer`** | **non — absente de `runtime/`** |
| **`cellGcmGetDisplayInfo`** | **non** |
| **`cellGcmGetDisplayBufferByFlipIndex`** | **non** |

## Ce qui est mesuré

| | natif | wasm |
|---|---|---|
| `unimplemented cellGcm*` dans les logs | **0** | **0** |
| appels `cellGcmSetDisplayBuffer` visibles | 0 | 0 |

**Aucune des deux parties n'atteint ces appels.** Ni le natif, ni le wasm. Ils sont importés mais jamais
invoqués dans ces runs.

## Ce que ça signifie

Le jeu configure son écran par **commandes FIFO** (registres `0x0200-0x0214`), pas par ces HLE. Et le
wasm n'écrit aucun de ces registres.

Donc les HLE `cellGcmSetDisplayBuffer` manquants **ne sont pas la cause** : personne ne les appelle.

## Ce que ça consolide

Le fait central reste : **le guest n'écrit jamais les registres de surface**. Ce n'est pas un HLE
manquant, ce n'est pas une valeur de configuration fausse, ce n'est pas une ressource absente.

C'est le **chemin invité** qui n'atteint pas le code de configuration de surface.

## Ce que je n'ai pas encore fait

Je n'ai pas cherché **où** dans le code lifté se trouve l'écriture des registres `0x0200-0x0214`, ni
vérifié si la fonction qui la contient est atteinte. C'est la mesure qui manque, et elle est faisable :
guetter les fonctions qui écrivent ces registres et voir si elles sont appelées.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |

Le jalon 3 reste ouvert. Ce round **élimine les HLE GCM d'affichage** comme cause : importés mais jamais
appelés des deux côtés.
