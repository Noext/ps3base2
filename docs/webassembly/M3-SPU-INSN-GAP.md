# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le probe `spuput` est un `#if` non activé nativement | `runtime/src/spu/thread.cpp:1183` | oui — la mesure directe est bloquée |
| Le build natif ne lie pas (`hook_flags`), codegen parallèle | `runtime/src/hooks.cpp:960,1007` | oui — tentatives 2 et 3 échouées |
| Les compteurs `insns` par SPU sont journalisés dans `status: spu` | `/tmp/cmp.log`, `/tmp/f4.log` | **oui — c'est la mesure qui a tranché** |

# La mesure manquante, obtenue sans le probe

Le round 58 voulait le taux d'échec `putllc` natif. Le probe est bloqué par un chantier
parallèle (deux tentatives : répertoire séparé, puis stockage de substitution — le lien échoue
sur `ppu::hook_flags`, que le codegen de l'autre agent doit fournir).

Mais la quantité qui compte était déjà dans les logs : **le nombre d'instructions SPU exécutées**.

| | natif valide (`cmp.log`) | wasm (`f4.log`) |
|---|---|---|
| `insns` SPU | **287 669 291 008** | **3 072** |
| `insns` SPU (autre noyau) | **263 193 551 872** | **12 288** |
| lignes de log SPU | 27 | 2 824 |
| état final des noyaux | `pc 0x011ac ie 0` | `pc 0x011ac ie 0` |

## Le facteur

    287 669 291 008 / 3 072 ≈ 93 600 000

**Les SPU du wasm exécutent 93 millions de fois moins d'instructions que ceux du natif.**

## Ce que ça change tout

Les rounds 25 à 45 ont établi que les SPU « tournent » côté wasm : 5 noyaux vivants, `putllc`
comptés, blocs compilés. Tout cela reste vrai. Mais en **volume de travail**, les SPU du wasm sont
essentiellement inactifs : 3 072 instructions contre 287 milliards.

Et l'état final est **identique** des deux côtés (`pc 0x011ac`, `ie 0`, les noyaux SPURS parqués).
Le natif arrive à cet état **après 287 milliards d'instructions** ; le wasm y est arrivé après
3 072.

Autrement dit : **le wasm parque ses SPU presque immédiatement, le natif les fait travailler
pendant tout le boot puis les parque.**

## Ce que ça explique

`(4J) Tile update` attend un flag que personne ne pose (rounds 51-55). Le poseur devrait être un
SPU — et les SPU du wasm n'exécutent presque rien. La chaîne redevient cohérente :

    SPU inactifs (3 072 insns)
      → pas de travail de tuilage produit
        → pas de pose sur le flag attendu
          → (4J) Tile update bloqué
            → pas de pipeline de rendu complet

Le round 55 avait correctement rétracté le lien causal **parce que le natif a le même blocage**.
Mais le natif a ce blocage **après** avoir fait travailler ses SPU ; le wasm l'a **sans** les avoir
fait travailler. Ce n'est donc pas le même état, malgré des compteurs de threads identiques.

## Ce que ça ne dit pas

Je n'ai pas identifié **pourquoi** les SPU wasm s'arrêtent. Deux pistes déjà documentées :

1. **`putllc` 68 % d'échecs** (round 58) — les SPU se parent sur `EV_LR` et n'avancent pas.
2. **Ordonnancement des threads hôtes Emscripten** — les réservations dépendent du timing.

Le taux d'échec natif reste inconnu, mais il devient moins critique : avec 287 milliards
d'instructions exécutées, le natif surmonte largement ses échecs.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round n'a pas obtenu la mesure visée (bloquée), mais a obtenu **une
mesure meilleure** : le volume d'instructions SPU, qui chiffre l'écart à un facteur 93 millions
et rattache le blocage observé au round 51 à une cause plus profonde.
