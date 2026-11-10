# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `EV_LR` se lève quand la réservation est perdue | `runtime/src/spu/thread.cpp:238-243` | **oui** — mesuré : 19 pertes sur 19 195 vérifications |
| Aucune occurrence de `0x30028500` dans le code lifté | `out/ppu/`, `out/spu/` | oui — l'adresse est calculée, pas immédiate |
| `cellSpursAddWorkload` n'est pas implémenté | `runtime/src/hle/cellSpurs.cpp` : 0 | oui — mais **jamais appelé** non plus |
| `libspurs_jq` chargé à t=11,4 s (wasm) / t=4,5 s (natif) | `/tmp/f4.log`, `/tmp/cmp.log` | identique des deux côtés |

# Trois vérifications, trois éliminations

## 1. Le travail SPU n'est pas soumis — mais ce n'est pas la cause

`cellSpursAddWorkload` **n'est pas implémenté**, et **n'est pas appelé** au runtime (0 occurrence
dans les logs). Le jeu ne soumet donc aucun workload.

Mais `libspurs_jq` se charge de la même façon des deux côtés, et le natif n'appelle pas non plus
ces fonctions dans ses logs. Je ne peux donc pas attribuer la différence à ce manque.

## 2. `_cellSpursLFQueuePopBody` retourne `CELL_OK` sans rien remplir

    HLE_FUNC(cellSpurs, _cellSpursLFQueuePopBody) { ret(ctx, CELL_OK_); }

Le jeu importe 5 symboles `_cellSpursLFQueue` mais **ne les appelle jamais** dans ce run
(0 occurrence). RPCS3 les marque également non implémentés (`//s32 _cellSpursLFQueuePopBody()`).
Défaut réel, mais inactif ici.

## 3. La réservation attendue EST invalidée

C'était la dernière porte. `EV_LR` se lève sur une **perte** de réservation :

    if ((event_mask & EV_LR) && raddr && !(events & EV_LR)) {
        if (rsv::lost(raddr, rtime, rdata)) { set_events(EV_LR); raddr = 0; }
    }

Mesure :

    reservation lost checks=19195 hits=19

**19 195 vérifications, 19 pertes.** L'événement se produit donc réellement. Le SPU ne peut pas
être bloqué par l'absence d'écrivain sur `0x30028500` — la réservation est invalidée 19 fois.

# Ce que ces trois mesures laissent

Toutes les portes que j'ai testées **s'ouvrent au moins une fois** :

| porte | verdict |
|---|---|
| `ie = 0` | partagé avec le natif (round 62) |
| décrémenteur | tire à t=32,8 s, corrigé (round 61) |
| `events 0x20` | apparaît bien |
| réservation perdue (`EV_LR`) | **19 fois** |
| AOT | trouve les blocs |
| travail soumis | non soumis — mais identique côté natif |

Et pourtant les SPU font 12 288 instructions contre 287 milliards.

Cela signifie que ma recherche de « la porte fermée » est **mal posée**. Le SPU ne s'arrête pas sur
une porte : il exécute ~1 000 instructions, sauvegarde son contexte, et le **thread hôte cesse de
le faire tourner**.

# L'hypothèse qui reste

Le module SPURS est un programme **coopératif** : il boucle, teste s'il y a du travail, et se met
en attente. Chez le natif, ce noyau tourne pendant que les PPU chargent, pendant 287 milliards
d'instructions. Chez le wasm, il tourne ~1 000 instructions puis le **thread hôte** s'endort.

La différence pourrait être dans le **réveil** : `cv.wait_for(lk, 100 µs)` dans `block()`. Si le
thread hôte est réveillé puis re-teste un prédicat faux, il retourne en attente sans exécuter
d'instruction SPU — et `insns` ne bouge pas.

C'est testable : compter les réveils de `cv.wait_for` et les entrées/sorties de `block()`.

# Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **élimine trois hypothèses de plus** (workload, LFQueue,
réservation) et reformule le problème : ce n'est pas une porte fermée, c'est un thread hôte qui
cesse de faire tourner un noyau coopératif.
