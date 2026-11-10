# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le probe `spuput` est un `#if PS3BASE_SPU_WAIT_PROBE` | `runtime/src/spu/thread.cpp:1183` | oui |
| Le natif ne loggue aucune ligne `spuput` | `/tmp/cmp.log` : 0 occurrence | oui — c'est la mesure manquante |
| `runtime/src/hle/cellGame.cpp` est modifié (chantier parallèle) | `git status` | oui — bloque le build natif partagé |

# La mesure tentée, et pourquoi elle a échoué

Le round 58 a livré 68 % d'échecs `putllc` côté wasm et **nommé la mesure manquante** : le même
taux côté natif. Ce round a tenté de la produire.

## La méthode

Configuration d'un répertoire de build **séparé**, pour ne pas toucher au build de l'autre
agent :

    cmake -S . -B /tmp/build-probe -DCMAKE_BUILD_TYPE=RelWithDebInfo \
          -DCMAKE_CXX_FLAGS="-DPS3BASE_SPU_WAIT_PROBE=1"

Configuration réussie, compilation lancée à `-j 8` (limite de la machine partagée).

## L'échec

    hooks.cpp:960: undefined reference to `ppu::hook_flags'
    hooks.cpp:1007: undefined reference to `ppu::hook_flag_image_count'

Le lien échoue. C'est la **même erreur préexistante** signalée au round 31 : `hook_flags` est
déclaré dans `runtime/include/ppu/ops.h` mais son stockage doit être généré dans
`out/ppu/table.cpp`, et le codegen qui l'émet (`recomp/ppu/codegen.py`, `tests/hook_flags.cpp`)
est **le chantier en cours de l'autre agent**.

Autrement dit : le build natif du dépôt ne lie pas actuellement, quel que soit le répertoire de
build, tant que ce codegen n'a pas atterri.

## Ce que je ne prétends pas

Je n'ai **pas** produit la mesure natif. Je n'ai donc rien appris de plus sur les 68 %, et je ne
comble pas le trou par une estimation.

## Les options pour la suite

1. **Attendre le codegen de l'autre agent** et refaire la mesure alors. La plus propre.
2. **Fournir un stockage de substitution** pour `hook_flags` dans le build de sonde uniquement —
   par exemple un fichier `-include` qui définit les deux symboles. Cela permettrait la mesure
   sans toucher à l'arbre partagé.
3. **Construire depuis un commit antérieur** au chantier en cours, dans un worktree séparé, avec
   le probe. Évite toute interférence.

L'option 2 est la moins coûteuse et la plus ciblée : deux symboles à définir pour un binaire de
mesure jetable.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo, vérifié à l'exécution** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **n'a pas produit de progrès** : la mesure visée est bloquée
par un chantier parallèle. Je le dis plutôt que de le maquiller, et je laisse la méthode et les
options écrites pour le round suivant.
