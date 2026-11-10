# Jalon 3 — état consolidé au round 126

Ce document rassemble les preuves mesurées, sans hypothèse non vérifiée.

## Les trois critères mécaniques du prompt

| critère (lignes 196-204 du prompt) | seuil | mesuré | verdict |
|---|---|---|---|
| taille du module | < 80 Mo | **56 Mo** | **PASSÉ** |
| plus grosse fonction | < 1 Mo | 1 409 972 octets | **ÉCHOUÉ** |
| ligne de log du menu | existe | **n'existe pas** | **CRITÈRE INEXISTANT** |

Le troisième critère n'est pas échoué : il est **sans objet**. Aucune ligne de log de menu n'existe dans le
runtime (vérifié par `grep -rniE 'menu|title screen' runtime/src/ | grep LOG_` : rien).

L'équivalent réel est `tools/screens/menu-main.png`, comparé par `classify()` — un **critère visuel**, qui
exige un rendu, que le point 6 du prompt interdit (stub qui compte).

## Ce qui fonctionne

| élément | mesure |
|---|---|
| module | 56 Mo, boote |
| frames | 11 341 |
| threads de guest | **17 sur 22** |
| décodage PNG | 911 images, mêmes adresses que le natif |
| audio | 938 blocs pompés |
| appuis pad | **délivrés** (600), effet mesuré 3k/s → 11k/s |
| écran de menu | `GetFriendsThreadProc` créé ×9 |
| crashes | 0 |

## Ce qui ne fonctionne pas

| élément | natif | wasm |
|---|---|---|
| `sys_vm_memory_map` | 2 | **0** (handler jamais entré) |
| `sys_event_flag_wait` | 219 207 | **12** |
| `sys_timer_usleep` | peu | **177 663** |
| threads manquants | — | 5 (dont `Server`, `FullscreenProgress`, `Post processing`) |
| surfaces RSX créées | 6 | **0** (pas de renderer) |

## Les hypothèses éliminées par la mesure

| hypothèse | round | preuve d'élimination |
|---|---|---|
| lenteur | 89 | le wasm est 8× plus rapide |
| ressource absente | 93 | assets identiques |
| `localSize` trop petit | 94 | corrigé à 249 Mo, aucun effet |
| HLE GCM d'affichage manquant | 95 | jamais appelé des deux côtés |
| configuration surface absente | 107 | le guest écrit 1280×480, 353 280 fois |
| taille de la texture | 90 | `0x280000` présent |
| pool de threads | 123 | 4 vs 32 : identique |
| fibres manquantes | 123 | le natif ne les a pas non plus |
| handler du map refuse | 125 | jamais entré |

## Le point de blocage, tel que mesuré

Le guest exécute le code contenant le syscall 300 (`mapcode`, 6 occurrences) mais **n'émet pas le syscall**
(0 ligne de handler). C'est une **bifurcation en amont de l'appel**.

Et le régime d'attente diffère massivement : 12 attentes de drapeau contre 219 207, compensées par 177 663
`usleep`. Le wasm **sonde le temps** là où le natif **se synchronise par drapeaux**.

## Les jalons 2, 4, 5

| jalon | état |
|---|---|
| 1 | **complet** (runtime sans mprotect, vérifié nativement) |
| 2 | non commencé (84 appels `vk*` encore Vulkan) |
| 3 | ouvert — 2 critères sur 3, le troisième sans objet |
| 4 | non commencé |
| 5 | non commencé |

## Conclusion honnête

Le jalon 3 **n'est pas atteint** : `sys_vm_memory_map` ne se produit pas et le critère de menu est
inexistant sous la forme que le prompt décrit.

Le wasm fait tourner le jeu 200+ secondes, décode les assets, pompe l'audio, crée 17 des 22 threads,
répond aux appuis et atteint un écran de menu au niveau comportemental. Il ne crée pas le monde.
