# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Aucune des deux voies de synchronisation n'est invoquée | `M3-NO-SYNC-AT-ALL.md` (round 75) | oui |
| Le natif fait son premier wait à t=4,55 s, `lr=0x0014f974` dans `f_0014f8e4` | `/tmp/cmp.log` | oui |
| `f_0014f8e4` lit `0xC00000 - 3156` et passe `gpr[3]=5` | `out/ppu/mc_012.cpp` | oui |

# La fonction est atteinte, mais par un autre appelant

## La mesure

J'ai gardé l'entrée de `f_0014f8e4` :

    [syncfn] #0 entry lr=0x3a19e4 r3=0x0

**Une seule entrée**, avec `r3=0x0`.

## Ce que ça dit

| | natif | wasm |
|---|---|---|
| `f_0014f8e4` atteinte | oui, depuis `(4J) Chunk update` | **oui, une fois** |
| appelant (`lr`) | `0x0014f974` (interne) | **`0x003a19e4`** |
| `r3` à l'entrée | valeur du flag | **`0x0`** |

La fonction **est** atteinte côté wasm — mon round 75 disait « jamais invoquée », ce qui était vrai
pour les appels HLE mais pas pour la fonction elle-même.

Mais :
- elle est atteinte **une seule fois** (le natif l'appelle en boucle),
- depuis un **autre appelant** (`f_003a19bc`, un poseur de flags, pas `Chunk update`),
- avec `r3 = 0` au lieu d'une valeur de flag.

## Ce que ça élimine et ce que ça laisse

**Éliminé** : « la fonction n'est jamais atteinte ». Elle l'est.

**Reste** : le thread `(4J) Chunk update` — celui qui appelle en boucle chez le natif — **n'emprunte
pas ce chemin** côté wasm. C'est le thread 9 parmi les 13 créés.

Donc le problème n'est pas la fonction, c'est **le thread qui devrait l'appeler en boucle**.

## Où chercher

`(4J) Chunk update` existe côté wasm (7 occurrences dans les logs) mais ne boucle pas sur
`f_0014f8e4`. Il faut voir à quel pc il se trouve et ce qu'il attend.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **précise** le round 75 : la fonction de synchronisation est
atteinte, mais par un autre appelant et une seule fois. Le thread qui devrait boucler dessus ne le
fait pas.
