# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| Fibres et pool éliminés comme causes | round 123 | oui |
| Le code lifté émet les syscalls via `ppu::syscall(ctx, base)`, base **calculée** | `out/ppu/liblv2_000.cpp` | **oui — pourquoi ma 1ʳᵉ recherche a échoué** |
| 5 fonctions référencent la constante 300 autour d'un syscall | `out/ppu/*.cpp` | oui |
| **Le wasm les atteint** (`pc=0x01004f74`, `lr=0x5e9124`) | mesure | **oui — mais le map ne se produit pas** |

# Le code du map est atteint, le syscall ne part pas

## La mesure

J'ai posé des gardes sur les cinq fonctions qui référencent 300 :

    [mapcode] pc=0x01004f74 lr=0x5e9124   (× 6)

**Le code est exécuté.** Il est appelé depuis `0x5e9124`.

## Ce que ça change

Mon hypothèse implicite (« le wasm n'atteint pas le code du map ») est **fausse**.

Le wasm **exécute** la fonction. Il n'en **émet** pas le syscall.

## Ce que ça implique

Trois lectures possibles :

1. Le code atteint est un **chemin différent** qui ne prend pas la branche du syscall 300.
2. Le syscall est émis mais notre **handler** le refuse (`CELL_ENOMEM` ou autre).
3. Le code atteint calcule `base` différemment et appelle un **autre** numéro.

## Ce que ça élimine

« Le guest n'arrive jamais à la fonction de map » est **éliminé**.

## La mesure suivante

Instrumenter le **handler du syscall 300** (`sys_vm_memory_map` dans `memory.cpp:34`) pour compter les
appels et leur code de retour. Si le handler est appelé et refuse, le problème est là ; s'il n'est jamais
appelé, le guest ne prend pas la branche.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) — mais **le code est atteint** |