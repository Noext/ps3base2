# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| Le code du syscall 300 est atteint (`pc=0x01004f74`) | round 124 | oui |
| Le handler **logue dès son entrée** | `runtime/src/lv2/memory.cpp:42` | **oui — donc 0 log = jamais entré** |
| **0 ligne `sys_vm_memory_map` dans le run wasm** | `/tmp/big.log` | **oui — le fait décisif** |

# Le handler n'est jamais entré

## La logique de l'instrument

Le handler du syscall 300 commence par :

    LOG_INFO("vm", "sys_vm_memory_map vsize 0x%llx psize 0x%llx flags ...");

avant **toute** validation. Si le wasm l'appelait, même pour refuser, il y aurait une ligne.

## La mesure

| | natif | wasm |
|---|---|---|
| lignes `sys_vm_memory_map` | **2** (entrée + retour) | **0** |
| gardes `mapcode` (code atteint) | — | **6** |

## Ce que ça établit

Le guest **exécute** la fonction qui contient le syscall 300, mais **n'émet pas** le syscall.

C'est donc une **bifurcation en amont de l'appel** : le code prend un chemin qui ne mène pas au `sc 300`.

## Ce que ça élimine définitivement

| hypothèse | statut |
|---|---|
| « le handler refuse le map » | **éliminée** — jamais appelé |
| « le guest n'atteint pas le code » | **éliminée** (round 124) |
| « le pool de threads limite » | **éliminée** (round 123) |
| « les fibres manquent » | **éliminée** (round 123) |

## Ce qui reste

La fonction atteinte (`f_01004f74`) est **partagée** : c'est un helper générique (les gardes montrent le même
`pc` pour tous). Le vrai appelant est `lr=0x5e9124`.

C'est donc **`0x5e9124`** qui décide d'appeler ou non le map. C'est là qu'il faut regarder.

## La prochaine mesure

Désassembler ou inspecter `f_005e9124` pour voir la condition qui garde l'appel au `sc 300`.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) — handler **jamais entré** |