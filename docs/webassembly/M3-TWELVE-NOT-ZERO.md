# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| « wasm : 0 `sys_event_flag_wait` » | round 121 | **FAUX — corrigé ici : 12** |
| Natif : 219 207 `sys_event_flag_wait` | `/tmp/cmp.log` | oui |
| Le rapport final ne montre que le **top 4** des syscalls | `wasm_entry.cpp` | **oui — la cause de l'erreur** |

# Correction : 12, pas 0

## Ce que j'avais lu

    top syscalls: 141(sys_timer_usleep)=177663 145(sys_time_get_current_time)=56837
                  97(_sys_lwmumeX_lock)=1180 98(_sys_lwmuteX_unlock)=190

J'en avais conclu « zéro `sys_event_flag_wait` ». **C'est faux** : le rapport n'affiche que les **quatre**
entrées les plus fréquentes, et `sys_event_flag_wait` (12 appels) est en dessous.

## La mesure correcte

J'ai recompilé avec `PS3BASE_FLAG_ID_PROBE=1`, qui compte chaque attente :

    flagid: 12 appels

| | natif | wasm |
|---|---|---|
| `sys_event_flag_wait` | **219 207** | **12** |

## Ce que ça change

L'écart reste **énorme** (18 267×), mais la formulation « zéro » était fausse et je la retire.

Le wasm **fait** des attentes sur drapeau — juste une douzaine, au lieu de 219 207.

## Ce que ça ne change pas

Le fond du constat du round 121 tient : **le régime d'attente diffère massivement**.

| | natif | wasm |
|---|---|---|
| event flags | 219 207 | 12 |
| lwmutex lock | 113 465 | 1 180 |
| `usleep` | (peu) | **177 663** |

Le wasm dort beaucoup et synchronise peu. Le natif synchronise beaucoup et dort peu.

## La leçon de méthode

**Un rapport tronqué au top-N n'est pas un compteur.** J'ai lu une absence dans une liste partielle.

C'est la même erreur qu'au round 103 (compteur aveugle au chemin groupé) : j'ai tiré une conclusion
d'absence d'un instrument qui ne couvrait pas tout.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |