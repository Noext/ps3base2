# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le probe `spuint` était limité aux **20 premiers appels** | `runtime/src/spu/thread.cpp:145` (avant) | **oui — mes valeurs du round 60 étaient un artefact** |
| `dec_start += 0x100000000ull` au tic du décrémenteur | `runtime/src/spu/thread.cpp:221` (avant) | **oui — bug réel, corrigé** |
| `collect_events()` compare `timebase_now() - dec_start > dec_value` | `runtime/src/spu/thread.cpp:219` | oui |

# Deux corrections, dont une seule change quelque chose

## Correction 1 : le probe échantillonnait au mauvais moment

Le round 60 concluait « `elapsed` 2,3 M, 500× sous le seuil, le timer ne satisfera jamais ».
**C'était un artefact** : le probe s'arrêtait après 20 appels, tous dans la première seconde.

En échantillonnant réparti sur le run :

    t=0,9 s   events 0x0   elapsed 3 997 280
    t=20,4 s  events 0x0   elapsed 1 564 394 800
    t=32,8 s  events 0x20  elapsed 18 446 628 219 523 183 536   ← débordement
    t=198 s   events 0x20  elapsed 18 441 385 734 518 476 192   ← débordement

**Le timer tire bien** : `events 0x20` (EV_TM) apparaît à t=32,8 s. Mon round 60 avait tort sur
« ne satisfera jamais ».

## Correction 2 : un vrai bug d'arithmétique du décrémenteur

`elapsed = 18 446 628 219 523 183 536` ≈ **2^64**. C'est une soustraction non signée **négative** :

    uint64_t elapsed = timebase_now() - dec_start;
    if (elapsed > dec_value) { set_events(EV_TM); dec_start += 0x100000000ull; }

`dec_start += 2^32` à chaque tic. Au bout de quelques tics, `dec_start` **dépasse**
`timebase_now()`, et `now - dec_start` devient un nombre gigantesque au lieu de négatif.

Correction : resynchroniser `dec_start = now` au lieu d'avancer de 2^32, et traiter explicitement
le cas `now < dec_start`.

**Vérifié** : `elapsed` est maintenant sain (88 M à 967 M), plus de débordement.

## Ce qui n'a PAS changé

| | avant correctif | après correctif | natif |
|---|---|---|---|
| `insns` SPU | 3 072 | **3 072** | **287 669 291 008** |
| frames | 11 161 | **11 341** | 74 581 |

**Le décrémenteur n'était pas la cause du parcage SPU.** Le corriger assainit l'arithmétique et
ajoute 180 frames, mais les SPU restent à 3 072 instructions.

Je ne prétends donc pas avoir débloqué quoi que ce soit : j'ai corrigé un bug réel qui n'était
pas le blocage.

## Ce que ça laisse

Le round 60 disait : « `ie = 0` ferme la sortie, et `0x30028500` n'a pas d'écrivain ». Le premier
point reste vrai et devient **le** point : avec `ie = 0`, `interrupt_pending()` retourne `false`
inconditionnellement, donc même un `events 0x20` correct ne réveille pas le SPU.

Le SPU est parqué à `pc 0x011ac` dans `block()`, et sa seule sortie est le prédicat qu'il teste.
Ce prédicat dépend de `0x30028500`, qui n'a pas d'écrivain dans le code lifté.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **corrige deux choses** : un artefact de mesure du round 60, et
un bug d'arithmétique réel du décrémenteur. Il ne débloque pas les SPU, et le dit.
