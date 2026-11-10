# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `build_dispatch()` itère sur `image::function_count` et lit `image::functions[i]` | `runtime/src/ppu_runtime.cpp:128-131` | **oui** — c'est le bug trouvé ce round |
| `runtime/src/main.cpp:217` fait la même boucle | `runtime/src/main.cpp:217` | oui — même dépendance au compte |
| Le générateur émet `function_count` et `functions[]` ensemble | `out/ppu/table.cpp:22707`, `:20` | oui — les deux doivent rester cohérents |

# Le bug de mon filtre, trouvé et corrigé

## Le symptôme

Le module filtré (56 Mo) démarrait puis mourait sur :

    RuntimeError: table index is out of bounds

Aucun flip, deux marqueurs de boot. Le build ne rendait plus.

## La cause

Mon script de filtrage supprimait les entrées de `functions[]` mais **laissait
`function_count` à 22 685** :

    const uint32_t function_count = 22685;   /* inchangé */
    const FuncEntry functions[] = { ... 1804 entrées ... };

Or `build_dispatch()` fait :

    for (uint32_t i = 0; i < image::function_count; i++) {
        uint32_t a = image::functions[i].addr;

**1804 entrées lues comme 22 685** — lecture hors bornes, d'où le trap.

Le compte et le tableau sont une seule donnée ; les filtrer séparément est un défaut.

## Le correctif

Réécrire `function_count` avec le nombre réel d'entrées conservées :

    const uint32_t function_count = 1804;

Même module, même filtre, une ligne de plus.

# Le résultat

| | avant correctif | après |
|---|---|---|
| module | 56 Mo | **56 Mo** |
| frames rendues | **0** (crash) | **11 161** |
| `out of bounds` | oui | **non** |
| audio actif | — | **oui**, port ouvert, blocs pompés |
| `unknown indirect` | — | aucun |

**Le module est sous le seuil (56 Mo contre 80) et couvre trois parcours** : le boot, l'appui
START, et l'initialisation audio.

# Ce que ce round corrige dans les rounds précédents

Rounds 40 à 48 présentaient les 55-56 Mo comme un gain fonctionnel. **Le round 42 avait déjà
montré que le filtre cassait sur un parcours non profilé** ; ce round montre qu'il cassait
aussi **structurellement**, dès le premier appel de dispatch, par un compte incohérent.

Autrement dit : les builds filtrés des rounds 40-48 n'ont probablement jamais rendu un seul
frame. Je ne l'avais pas vérifié à chaque fois — je vérifiais les flips sur le build **non
filtré** et je supposais que le filtré se comportait pareil.

C'est la leçon : **un filtre qui change une structure de données doit être testé sur le chemin
qu'il sert, pas seulement sur la taille qu'il produit.**

# Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo, vérifié à l'exécution** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | non |

Le jalon 3 reste ouvert. Les 56 Mo sont maintenant **prouvés par l'exécution** : 11 161 frames,
audio actif, trois parcours couverts, aucun crash.
