# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `flag_trace` est filtré par `flag_watched(id)` | `runtime/src/lv2/sync.cpp:79-88` | **oui** — mon contraste du round 50 était un artefact |
| La sélection passe par `PS3BASE_FLAG_TRACE_ALL` / `_IDS` (`getenv`) | `runtime/src/lv2/sync.cpp:80,84` | oui — inerte en wasm |
| `getenv` est inerte sans `-sENV`, et `-sENV` casse ce lien | mesuré aux rounds 20-24 | oui |

# La rétractation du round 50

Le round 50 annonçait « 3 321 122 lignes `flagtrace` côté natif contre 4 côté wasm » et en
tirait un contraste de modèle de synchronisation. **C'était un artefact.**

`flag_trace` commence par :

    if (!flag_watched(id)) return;

et `flag_watched` lit `PS3BASE_FLAG_TRACE_ALL` via `std::getenv`. **`getenv` est inerte dans le
build wasm** — donc la trace ne pouvait rien enregistrer, quelles que soient les activations.
Le natif, lui, tournait avec le flag d'environnement activé.

J'ai remplacé la sélection par une constante de compilation et remesuré.

# La mesure corrigée, et ce qu'elle donne

Avec `-DPS3BASE_FLAG_TRACE_ALL=1`, le wasm enregistre **8 lignes**, pas 3,3 millions. Le
contraste est donc **réel**, mais il n'était pas établi par la mesure du round 50.

Et ces 8 lignes suffisent à nommer le blocage :

    flagtrace: wait-in  thread=(4J) Tile update(0x1000006) lr=0x0025d0a4 bits=0x7 mode=0x11
    flagtrace: timeout  value=0x0 (0 means wait forever)
    flagtrace: state    subsystem[5] value=0x01
    flagtrace: set      from=main_thread lr=0x00145d14 bits=0x2 pattern 0x0 -> 0x2
    flagtrace: wait-in  thread=(4J) Chunk update(0x1000009) lr=0x0014f974 bits=0x3 mode=0x11
    flagtrace: set      from=(4J) OpenStreamThreadProc lr=0x003a19e4 bits=0x1 pattern 0x0 -> 0x1

| fait | valeur |
|---|---|
| `(4J) Tile update` attend | **`bits=0x7`**, mode `0x11` = AND, attente de tous les bits |
| timeout | **`0x0` = attente infinie** |
| bits jamais posés | `0x2`, `0x1` posés par d'autres ; **`0x4` n'est jamais posé** |

**Le thread attend les trois bits à la fois ; le bit `0x4` n'arrive jamais.** C'est un blocage
par condition insatisfiable, pas une lenteur.

## Ce que ça explique

`(4J) Tile update` est l'un des threads du système de rendu par tuiles. S'il attend un bit que
personne ne pose, il ne participe pas — et le pipeline de rendu reste partiel, ce qui est
cohérent avec les 93 % de flips vides mesurés au round 36 et avec l'absence de contenu affiché.

## Ce que je n'ai pas établi

Je n'ai pas identifié **qui devrait poser `0x4`**, ni si ce poseur existe et échoue, ou s'il
n'est jamais atteint. C'est le prochain pas : chercher le site qui écrit ce bit dans le code
lifté, et vérifier s'il est atteint.

# Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | oui, 56 Mo, vérifié à l'exécution |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | non |

Le jalon 3 reste ouvert. Ce round **rétracte** une conclusion du round 50 (elle reposait sur un
instrument muet) et la remplace par une mesure valide qui **nomme un blocage précis** :
`(4J) Tile update` attend `0x7` en mode AND avec un timeout infini, et le bit `0x4` n'est jamais
posé.
