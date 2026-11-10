# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| Le wasm reste à 3k/s de draw, le natif bascule à 68k/s vers t=511 s | round 110 | oui |
| `(4J) Save thread 0/1/2` créés à t=481,9 s chez le natif | `/tmp/cmp.log` | **oui — avant la bascule** |
| Le wasm ne crée **aucun** `Save thread` | `/tmp/long2.log` | **oui — 0 contre 240** |

# Les threads de sauvegarde manquent

## La mesure

| | natif | wasm |
|---|---|---|
| `(4J) Save thread N` | **240** | **0** |
| lignes `savedata` | **160** | **7** |
| `savedata: write` | **78** | **0** |
| `savedata: UserAutoSave` | **34** | **0** |

## La chronologie

    t=481,9 s  _sys_ppu_thread_create '(4J) Save thread 0/1/2'   (natif)
    t=487,7 s  savedata: UserAutoSave                            (natif)
    t=511 s    draw passe de 4k/s à 68k/s                        (natif)

Les threads de sauvegarde **précèdent** la bascule de rendu.

## Ce que ça établit

Le wasm n'écrit **jamais** de sauvegarde, et ne crée **jamais** de thread de sauvegarde. Le HLE
`cellSaveData` existe (11 fonctions) mais le guest ne l'invoque pas en écriture.

## Ce que ça ne dit pas

Je n'ai pas établi que ces threads **causent** la bascule. Ils la précèdent de 30 s.

## Une hypothèse testable

Le jeu pourrait attendre qu'une sauvegarde soit écrite pour passer en mode jeu. Si c'est le cas, le wasm
resterait en chargement parce qu'il n'écrit jamais — et le HLE `cellSaveData` serait le suspect.

Mais au round 89 j'ai mesuré que `UserAutoLoad` ne bloquait rien. Il faut vérifier la **voie d'écriture**
cette fois, pas la lecture.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |