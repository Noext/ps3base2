# Une chaîne de build reproductible

## Le problème

Tous mes builds de ce jalon vivaient dans `/tmp`, avec des objets nommés à la main. **Rien n'était
reproductible** : ni les drapeaux, ni l'ordre, ni la composition des objets.

Un module qui passe les seuils mais qu'on ne sait pas reconstruire n'est pas un livrable.

## Ce que j'ai ajouté

| fichier | rôle |
|---|---|
| `tools/build_wasm.sh` | compile et lie le module, drapeaux commentés par leur raison mesurée |
| `tools/filter_table.py` | filtre la table par profil de couverture |
| `tools/wasm_sizes.py` | rend le nombre de fonctions et la plus grosse |

## Les drapeaux, chacun justifié

| drapeau | raison |
|---|---|
| `-msimd128 -msse4.1 -mfma` | `__m128`/`__m128i` et 22 intrinsics SSE ne compilent pas sans |
| `-fwasm-exceptions` | le runtime utilise les exceptions C++ |
| `-matomics -mbulk-memory` | requis par `--shared-memory` ; **chaque** objet en a besoin |
| `-pthread` | un thread hôte par thread invité |
| `-Oz` | mesuré meilleur que `-O1` sur **les deux** critères (92 vs 110 Mo, 727 vs 1 445 ko) |
| `-nostdlib++` + `libc++-mt-wasmexcept.a` | `libc++.a` n'a pas les exceptions |

## Les deux outils, vérifiés

    table filter: 1890 of 22685 entries kept, 0 profile addresses have no entry
    ps3m10.wasm: 46150 functions, largest 1316534 bytes  ** largest function exceeds 1 MB **

## Ce que le filtre documente

92 % des fonctions liées ne sont jamais exécutées (1 775 sur 22 685). Le profil doit être **complet** : un
seul appel indirect manquant avorte le guest avec `unknown indirect target 0xADDR`.

La boucle de fermeture est documentée dans l'en-tête du script : lier, lancer, ajouter l'adresse rapportée,
recommencer.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui — 49,7 Mo** |
| plus grosse fonction sous 1 Mo | non (1 316 534 o, 1 sur 46 150) |
| ligne de log du menu | **oui, corroborée** |
| **build reproductible** | **oui — script + deux outils** |