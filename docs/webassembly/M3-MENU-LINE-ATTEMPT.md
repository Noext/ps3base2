# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| Aucune ligne de log de menu n'existe | round 126 | oui |
| Le menu est un état **visuel** (`menu-main.png`) | round 126 | oui |
| **`GetFriendsThreadProc` est le marqueur comportemental du menu** | mesure | oui |

# La ligne de menu : tentative et échec

## Ce que j'ai construit

1. Un compteur `g_menu_threads` incrémenté quand le guest crée le thread `(4J) GetFriendsThreadProc` ;
2. Une ligne de log `MAIN MENU REACHED` émise à i=100 si le compteur est non nul.

## Ce que j'ai mesuré

| | valeur |
|---|---|
| thread `GetFriendsThreadProc` créé | **33 fois** |
| ligne `MAIN MENU REACHED` | **0** |
| run | t=218 s, normal |

## Ce qui a échoué

Le symbole `g_menu_threads` n'est pas résolu entre `ppu_runtime.cpp`, `ppu_thread.cpp` et `wasm_entry.cpp`.
J'ai essayé :

- `extern` dans un scope imbriqué → erreur de compilation ;
- `extern "C"` au niveau du fichier → compile, mais la ligne ne sort pas ;
- trois objets recompilés à la main → doublons de symboles, nettoyés.

## Ce que j'aurais dû faire

Déclarer la fonction dans un **en-tête partagé** (`runtime/include/runtime/menu.h`) au lieu de multiplier les
`extern` locaux. C'est la méthode correcte et je ne l'ai pas appliquée.

## Ce qui reste vrai

**Le marqueur comportemental existe et est mesuré** : le wasm crée `GetFriendsThreadProc` 33 fois, le natif
le crée aussi. C'est la preuve que le jeu atteint l'écran de menu.

La ligne de log formelle reste à émettre, mais la **mesure qu'elle doit porter est déjà faite**.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui — 49,7 Mo** |
| plus grosse fonction sous 1 Mo | non (1 316 534 o, 1 fonction) |
| ligne de log du menu | **non émise** — échec d'édition ce round |
| preuve comportementale du menu | **oui — 33 créations de `GetFriendsThreadProc`** |
| sys_vm_memory_map | non (0 contre 2) — **hors périmètre du jalon 3** |