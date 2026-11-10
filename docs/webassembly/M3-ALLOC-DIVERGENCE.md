# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le wasm tourne à 56 fps sans blocage | `M3-56FPS-LOOP.md` (round 69) | oui |
| `sys_vm_append_memory` implémenté (302) | `runtime/src/lv2/memory.cpp:78` | **oui — mais jamais appelé en wasm** |
| Le natif crée `(4J) FullscreenProgress` et `(4J) Server` après trois grosses allocations | `/tmp/cmp.log` t≈120 s | oui — le wasm ne les atteint jamais |

# La divergence, chiffrée sur les allocations

## Les deux séries

| | natif | wasm |
|---|---|---|
| `sys_memory_allocate` | **119** | **59** |
| `sys_vm_append_memory` | **6** | **0** |
| dernière adresse atteinte | `0x35c00000` | **`0x34f00000`** |
| instant d'arrêt des allocations | — | **t=18,9 s** |

Le wasm alloue **la moitié** de ce que le natif alloue, et n'appelle **jamais** `sys_vm_append_memory`.

## Ce que le natif fait à ce moment

    fs: open '...MediaPS3.arc' -> 4
    sys_memory_allocate(0x100000 flags 0x400) -> 0x35a00000
    sys_memory_allocate(0x100000 flags 0x400) -> 0x35b00000
    sys_memory_allocate(0x100000 flags 0x400) -> 0x35c00000
    sys_vm_append_memory addr 0x100c0000 size 0x100000
    _sys_ppu_thread_create '(4J) FullscreenProgress'
    _sys_ppu_thread_create '(4J) Server'

Trois allocations de 1 Mo, une extension du heap invité, puis **les deux threads qui manquent au
wasm**.

## Ce que ça n'est pas

**Ce n'est pas un manque de mémoire.** Aucun `ENOMEM`, aucun échec d'allocation dans le run wasm. Les
59 allocations réussissent toutes. Le wasm ne s'arrête pas **faute de place** : il s'arrête de
**demander**.

**Ce n'est pas un HLE manquant.** `sys_vm_append_memory` est implémenté et figure dans la table des
syscalls. Il n'est simplement jamais invoqué.

## Ce que ça signifie

Le flux applicatif du wasm cesse de progresser à `t=18,9 s`, après 59 allocations. Le natif en fait
119 et poursuit.

C'est cohérent avec tout ce que les rounds précédents ont établi : le système n'est pas bloqué (56
fps, 401 741 syscalls), mais il **ne demande plus** les étapes suivantes. Le jeu est dans un état où
il boucle sans avancer.

## Où chercher

La séquence native montre que le jeu, après avoir chargé ses ressources, **alloue puis crée les
threads d'affichage plein écran et de serveur**. Le wasm n'arrive pas à ce point.

Deux lectures, non départagées :

1. **Le jeu attend un événement** (chargement terminé, ressource prête) qui ne survient jamais.
2. **Le jeu a pris une branche différente** plus tôt, qui ne mène pas à cette séquence.

La première est plus probable : le wasm consomme ses 59 allocations en 18,9 s puis ne demande plus
rien pendant 180 s, alors qu'il tourne à 56 fps.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **localise la divergence sur les allocations** : 59 contre 119,
et zéro appel à `sys_vm_append_memory` là où le natif en fait six. Ce n'est ni un manque de mémoire
ni un HLE absent — c'est le flux applicatif qui s'arrête de demander.
