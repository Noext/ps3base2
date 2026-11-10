# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| 911 PNG décodés wasm, 0 liés | `M3-DECODED-NOT-BOUND.md` (round 88) | oui |
| `cellSaveDataUserListAutoLoad` **est implémenté** | `runtime/src/hle/cellSaveData.cpp:514` | **oui — mais 0 appel wasm** |
| Le natif crée `thread_getSavesInfo` après les options | `/tmp/cmp.log` t=82,6 s | oui |

# La lenteur n'est pas la cause

## La mesure qui l'élimine

| | natif | wasm |
|---|---|---|
| décodage PNG (début → fin) | t=0,6 → **85,0 s** | t=1,2 → **10,4 s** |
| options chargées | t=82,6 s | **t=10,5 s** |

**Le wasm est 8× plus rapide** que le natif sur ces phases. Il décode 911 PNG en 9 secondes là où le
natif en met 84.

Donc la lenteur que j'avais notée au round 88 comme un possible facteur **n'en est pas un** : le wasm
arrive plus vite à chaque étape, et s'arrête quand même.

## Ce qui manque après les options

Le natif, juste après `UserAutoLoad` :

    _sys_ppu_thread_create 'thread_getSavesInfo'
    savedata: UserListAutoLoad version 0x1 user 0
    savedata: UserListAutoLoad: 0 savedata match prefix 'BLES01976--'
    ppu: thread 'thread_getSavesInfo' exit 0x0
    texloc: off 0x40b2c00 fmt 0x00018129 ...

Le wasm n'a **aucun** de ces événements :

| | natif | wasm |
|---|---|---|
| `thread_getSavesInfo` créé | oui | **non** |
| `UserListAutoLoad` | 4 | **0** |
| `texloc` | 40 | 0 |

## Ce que ça change

Le HLE `cellSaveDataUserListAutoLoad` **existe** (`cellSaveData.cpp:514`) et n'est jamais invoqué.
Comme aux rounds 75-77 pour les event flags : ce n'est pas un HLE défaillant, c'est un **appel qui
n'arrive pas**.

Et l'énumération des sauvegardes est précisément ce qui, chez le natif, précède la création des
threads d'affichage (`FullscreenProgress`, `Server`) et le `sys_vm_memory_map`.

## La chaîne, mise à jour

    le jeu charge ses options (t=10,5 s, plus vite que le natif)
      → il n'enchaîne PAS sur l'énumération des sauvegardes
        → thread_getSavesInfo jamais créé
          → UserListAutoLoad jamais appelé
            → les threads d'affichage ne sont pas créés
              → sys_vm_memory_map jamais atteint
                → pas de monde, pas de menu

## Ce que je n'ai pas établi

Pourquoi le jeu n'enchaîne pas sur l'énumération des sauvegardes après avoir chargé ses options. C'est
le nouveau point d'entrée de la chaîne.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |

Le jalon 3 reste ouvert. Ce round **élimine la lenteur** (le wasm est 8× plus rapide) et **déplace le
point d'entrée de la chaîne** sur l'énumération des sauvegardes, absente côté wasm alors que son HLE
existe.
