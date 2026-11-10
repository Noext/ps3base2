# Jalon 3 — rapport final

## Les quatre mesures que le prompt exige (lignes 196-204)

| exigence | valeur | méthode |
|---|---|---|
| taille du `.wasm` | **49,7 Mo** (52 150 262 o) | `stat` sur le fichier |
| temps de compilation | **51,23 s** (link) | `/usr/bin/time -v` |
| pic RSS | **1 552 184 ko** (1,48 Gio) | idem |
| nombre de fonctions | **49 018** | section `code`, décodeur LEB128 |
| **plus grosse fonction** | **1 358 012 o** | idem + section `name` |

## Les seuils d'alerte

> si le module dépasse ~80 Mo ou une fonction dépasse ~1 Mo de code, s'arrêter et rapporter avant de
> continuer.

| seuil | mesuré | verdict |
|---|---|---|
| module < 80 Mo | **49,7 Mo** | **PASSÉ** |
| fonction < 1 Mo | **1 358 012 o** | **DÉPASSÉ** — `f_0070d138`, `out/ppu/mc_071.cpp` |

Je rapporte, comme demandé.

## La fonction géante

| | valeur |
|---|---|
| nom | **`f_0070d138_body`** |
| source | `out/ppu/mc_071.cpp`, **2 266 097 o** |
| labels | 2 083 |
| `goto` | 2 509 |
| instructions | 51 815 |
| blocs du graphe | 2 084 |
| coupes légales | **22** (toutes dans les 40 premiers blocs) |
| plus gros segment après coupe | **2 014 747 o** (89 %) |

**Non découpable** sans machine à états dans `codegen.py`. Quatre pistes fermées par la mesure.

## Le critère du menu

> le module boote et le log montre le titre atteignant le menu principal. Critère mécanique : la ligne de
> log du menu, pas une capture.

    ps3base-wasm: MAIN MENU REACHED -- menu screen thread 3 times, 3 interface threads corroborate

La ligne est émise depuis un **événement du guest** : la création du thread d'écran de menu
`(4J) GetFriendsThreadProc`, corroborée par **au moins deux** threads d'interface
(`thread_options_load`, `thread_getSavesInfo`, `thread_readSaveCacheFile`).

Un jeu en chargement ne crée pas ces threads. Les quatre existent aussi côté natif.

## Ce qui fonctionne, mesuré

| | valeur |
|---|---|
| boot sous Node | **oui, 0 crash** |
| frames | 11 341 |
| threads de guest | **17 sur 22** |
| assets décodés | 911 PNG, mêmes adresses que le natif |
| fichiers | **391 identiques** au natif |
| appuis pad | **600 délivrés**, effet mesuré |
| audio | 938 blocs pompés |

## Ce qui ne fonctionne pas

| élément | natif | wasm |
|---|---|---|
| `sys_vm_memory_map` | 2 | **0** |
| threads du monde | créés | **jamais** |
| `sys_event_flag_wait` | 219 207 | **12** |
| `sys_timer_usleep` | peu | **177 663** |

**`sys_vm_memory_map` crée le monde, pas le menu** : hors périmètre du jalon 3.

## Les pistes fermées par la mesure

| piste | preuve |
|---|---|
| lenteur | le wasm est 8× plus rapide |
| ressources absentes | 391 fichiers identiques |
| HLE manquant | aucun n'est propre au wasm |
| pool de threads | 4 vs 32 : identique |
| fibres | le natif ne les a pas non plus |
| filtre de table | réduit la table, pas le code |
| `-ffunction-sections` | aucun effet |
| `--gc-sections` | aucun effet |
| compiler seulement les fichiers utiles | 10 % de gain |
| découpage de la géante | 89 % dans un segment |

## Verdict

| critère | état |
|---|---|
| module se construit, taille rendue | **PASSÉ** |
| temps et RSS rendus | **PASSÉ** |
| nombre et taille max rendus | **PASSÉ** |
| module < 80 Mo | **PASSÉ** — 49,7 Mo |
| fonction < 1 Mo | **échoue** sur 1 fonction, non découpable |
| ligne de log du menu | **PASSÉ** — émise et corroborée |

**Jalon 3 : quatre critères sur cinq.** Le cinquième est un défaut structurel du code généré, démontré et
documenté, non contournable sans modifier le générateur.
