# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le park forcé donne 2 425× d'instructions SPU | `M3-PARK-FIX.md` (round 65) | oui |
| Il réduit les échecs `putllc` de 3,43 M à 1,03 M | `M3-SPINNING-NOT-WORKING.md` (round 66) | oui |
| `getenv` est inerte en wasm, donc le flag ne peut pas être activé à l'exécution | mesuré rounds 20-24 | oui |
| Aucun script de build wasm dans le dépôt | `tools/*.sh` | oui — le build est manuel |

# Le correctif était conditionné à un flag que personne ne mettrait

Le round 65 avait obtenu un gain mesuré en compilant avec `-DPS3BASE_SPU_PARK_FORCED=1`. Ce round
rend le comportement **par défaut pour la cible wasm**, sans option :

    #if defined(__wasm__) || defined(__EMSCRIPTEN__) || \
        (defined(PS3BASE_SPU_PARK_FORCED) && PS3BASE_SPU_PARK_FORCED)
        return true;
    #else
        static const bool on = env_long("GOWA_SPU_PARK", 0) != 0;
        return on;
    #endif

## Pourquoi c'était nécessaire

Il n'existe **aucun script de build wasm dans le dépôt** : le module est produit par une ligne
`emcc` manuelle. Un gain conditionné à un `-D` que ce script ne contient pas est un gain qui
n'existera pas dans les faits.

C'est la différence entre « j'ai mesuré un gain » et « le build a ce gain ».

## La vérification

Reconstruction **sans aucun `-D` de park** :

| | sans park | avec park (défaut) |
|---|---|---|
| `insns` SPU max | 3 072 | **7 446 528** |
| `putllc` échoués | 3 434 000 | **1 034 000** |

Le gain du round 65 est maintenant obtenu **sans option**. Le natif, lui, garde son comportement
d'origine (`PS3BASE_SPU_PARK`), et `spurs` passe, build natif à 0 erreur.

## Ce que ça règle, et ce que ça ne règle pas

**Règle** : le correctif est réel et permanent pour la cible visée, pas une expérience documentée.

**Ne règle pas** : le menu. Les frames n'ont pas bougé (11 161 → 11 281). Le travail SPU progresse,
le rendu non.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **consolide** un gain mesuré en le rendant effectif par défaut,
ce qui est la condition pour qu'il compte.
