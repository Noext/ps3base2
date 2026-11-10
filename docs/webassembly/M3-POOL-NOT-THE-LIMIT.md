# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| Le wasm crée 28-29 threads de guest | round 118 | oui |
| Mon link utilise `-sPTHREAD_POOL_SIZE=4` | recette de link | **oui — suspect structurel** |
| Le wasm fait 12 attentes sur drapeau, le natif 219 207 | round 122 | oui |

# Le pool de threads n'est pas la contrainte

## L'hypothèse

Un pool de 4 threads pour 29 threads de guest pourrait limiter la création, et donc la synchronisation.

## Le test

Recompilation avec :

    -sPTHREAD_POOL_SIZE=32 -sPTHREAD_POOL_SIZE_STRICT=0

## Le résultat

| métrique | pool=4 | pool=32 |
|---|---|---|
| threads créés | 29 | **28** |
| attentes drapeau | 12 | **13** |
| draws | 9k/s | **9k/s** |
| `sys_vm_memory_map` | 0 | **0** |

**Aucun changement.** Emscripten crée des threads à la demande au-delà du pool (`PTHREAD_POOL_SIZE_STRICT=0`),
donc le pool de 4 n'était pas une limite.

## Ce que ça élimine

L'hypothèse « le pool limite la progression » est **éliminée**.

## Ce que ça confirme

Les 29 threads sont **réellement créés** et fonctionnent. Le problème n'est pas la capacité de threading.

## Ce que ça laisse

Le déficit de synchronisation (12 attentes contre 219 207) vient du **comportement du guest**, pas de
l'infrastructure de threads.

## Ce qui reste à tester

Deux pistes structurelles non encore vérifiées :

1. **`-sPROXY_TO_PTHREAD`** — le thread principal est-il proxyfié ? Cela change la façon dont `main` se
   comporte vis-à-vis des threads.
2. **Les fibres** (`libfiber.sprx`). Le jeu charge `libfiber` ; nos fibres fonctionnent-elles ?

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |