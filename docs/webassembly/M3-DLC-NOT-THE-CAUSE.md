# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| La chaîne causale est fermée : allocations → FullscreenProgress → bit 0x1 → Chunk update bloqué | `M3-CHAIN-CLOSED.md` (round 77) | oui |
| Le wasm s'arrête à l'allocation 59, adresse `0x34f00000`, t=18,9 s | `/tmp/def.log` | oui |
| Le natif atteint `0x34f00000` à t=11,9 s puis `0x35000000` à t=76,9 s | `/tmp/cmp.log` | **oui — un saut de 65 s** |

# La divergence n'est pas dans le DLC

## L'hypothèse testée

Le natif monte des DLC de textures (`City Texture Pack`, `Plastic Texture Pack`, etc.). J'ai supposé
que le wasm n'y avait pas accès et s'arrêtait là.

## La mesure

| | natif | wasm |
|---|---|---|
| ouvertures `USRDIR/DLC` | **5** | **5** |
| `TexturePack.pck` ouvert | oui | **oui** |
| `Skins1_Sony.pck` ouvert | oui | oui |
| répertoire monté | — | **identique** |

**Le DLC existe des deux côtés et est ouvert des deux côtés.** Ce n'est pas la cause.

## Ce que la comparaison révèle

La divergence est **temporelle**, pas fonctionnelle :

    natif : allocation 56 à 0x34f00000  t=11,9 s
            ... 65 secondes ...
            allocation 57 à 0x35000000  t=76,9 s

    wasm  : allocation 59 à 0x34f00000  t=18,9 s
            (rien ensuite)

Le natif **fait une pause de 65 secondes** entre deux allocations, puis continue. Le wasm, lui,
s'arrête après la même adresse et ne reprend pas.

## Ce que ça signifie

Ce n'est pas une ressource manquante : c'est **le jeu qui ne reprend pas** après cette série
d'allocations. Pendant ces 65 secondes, le natif exécute du travail (rendu, audio, flagtrace — des
millions de lignes) mais **ne demande pas de nouvelle mémoire**.

Le wasm, lui, ne reprend jamais.

## Ce que ça ne dit pas

Je n'ai **pas** établi ce qui permet au natif de reprendre à t=76,9 s. Candidats :

- Un thread qui progresse pendant ces 65 s et déclenche l'allocation suivante.
- Un rendez-vous applicatif qui se débloque.

La chaîne du round 77 dit que `(4J) FullscreenProgress` manque. Or ce thread est créé **après**
l'allocation 57. Donc : le wasm n'atteint pas l'allocation 57, donc ne crée pas `FullscreenProgress`,
donc ne pose pas le bit, donc `Chunk update` bloque. **La chaîne tient.**

Mais le point d'entrée — pourquoi l'allocation 57 n'arrive pas — reste ouvert.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **élimine le DLC** comme cause (5 ouvertures des deux côtés, même
répertoire) et **précise la nature de la divergence** : temporelle, avec une pause de 65 s côté natif
que le wasm ne franchit pas.
