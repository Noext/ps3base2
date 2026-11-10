# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| La boucle wasm dort 3× par tour (`f_002dc29c`) | `M3-SLEEP-LOOP.md` (round 73) | oui |
| Le natif journalise ses histograms de syscalls par thread | `/tmp/cmp.log` `status: top syscalls` | **oui — c'est la comparaison qui manquait** |
| Le wasm expose le même histogram | `/tmp/def.log` | oui |

# Le contraste, chiffré

## Les deux histogrammes

| syscall | natif | wasm | rapport |
|---|---|---|---|
| **141 `sys_timer_usleep`** | **6 754 559** | 79 540 | **85× moins** |
| **85 `sys_event_flag_wait`** | **219 207** | **absent du top** | — |
| 145 `sys_time_get_current_time` | 184 258 | 18 747 | 9,8× moins |
| 97 `_sys_lwmutex_lock` | 113 465 | 369 | **307× moins** |
| 98 `_sys_lwmutex_unlock` | 104 058 | 52 | **2 000× moins** |

## Ce que ça renverse

Les rounds 71-73 décrivaient la boucle wasm comme une attente excessive. **Le natif dort 85× plus.**

Et plus important : le natif fait **219 207 `sys_event_flag_wait`**, absents du top wasm. Les
event flags sont le mécanisme par lequel les threads du jeu se synchronisent — c'est le point que le
round 51 avait approché puis rétracté (à tort de conclure, pas à tort de mesurer).

## Ce que ça signifie

Ce n'est pas « le wasm attend trop ». C'est :

1. Le natif **utilise massivement** les event flags (219 207) et les mutex (217 523 combinés).
2. Le wasm en utilise **une fraction** (369 + 52 mutex, et pas d'event flag dans le top).

Le wasm ne **participe pas** à la synchronisation du jeu. Il exécute une fraction des opérations de
coordination que le natif exécute.

C'est cohérent avec tout ce qui précède : le wasm fait 401 741 syscalls au total contre des millions
côté natif. Il ne fait pas le même travail, pas seulement moins vite.

## Réserve importante

Ces histogrammes sont **par thread** côté natif (plusieurs lignes `status:`), et le wasm n'affiche
qu'un agrégat. Un rapport exact demanderait de comparer thread par thread. Les ordres de grandeur
sont néanmoins sans ambiguïté : 6,75 M contre 79 k, et 219 k event flags contre zéro dans le top.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **renverse une lecture** : le wasm n'attend pas trop, il
**participe trop peu** à la synchronisation. Le natif dort 85× plus et coordonne 219 207 fois par
event flags.
