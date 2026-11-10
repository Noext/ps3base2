# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| Cinq fonctions jamais appelées dans quatre modules | round 120 | oui |
| Le wasm cesse d'allouer après t=100 s | round 120 | oui |
| **Natif : `sys_event_flag_wait` × 219 207. Wasm : 0** | mesure sur les deux logs | **oui — l'écart le plus net** |

# Le régime d'attente est différent

## Les compteurs

| syscall | natif | wasm |
|---|---|---|
| **`sys_event_flag_wait` (85)** | **219 207** | **0** |
| `_sys_lwmutex_lock` (97) | 113 465 | 1 180 |
| `_sys_lwmutex_unlock` (98) | 104 058 | 190 |
| `sys_timer_usleep` (141) | — | **177 663** |
| `sys_time_get_current_time` (145) | — | 56 837 |

## Ce que ça dit

Le natif passe son temps **à attendre sur des drapeaux d'événements** : 219 207 attentes, 113 465 verrous de
mutex léger.

Le wasm **dort** : 177 663 `usleep`, et **zéro** attente sur drapeau.

## Ce n'est pas la même attente

| | natif | wasm |
|---|---|---|
| mécanisme | event flags + lwmutex | `usleep` |
| threads actifs | coordonnés par drapeaux | endormis en boucle |
| conduite | producteur/consommateur | sondage temporel |

Le natif a un **système de synchronisation** qui fonctionne. Le wasm n'en a pas : ses threads dorment et se
réveillent sans se coordonner.

## Ce que ça explique enfin

Les **cinq** fonctions jamais appelées des rounds 112-120 incluent `cellSpursEventFlagWait` (219 207
natives). Ce n'est pas une coïncidence : **c'est le même fait**.

Le jeu n'emprunte pas ses chemins de synchronisation, donc ses threads ne se coordonnent pas, donc il ne
progresse pas au-delà de l'interface.

## Ce que ça ne dit pas

Si l'absence d'event flags est la **cause** ou un **symptôme** du blocage amont.

Mais c'est la première mesure qui donne un **mécanisme** : pas « il manque X », mais « l'attente ne se fait
pas comme il faut ».

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |