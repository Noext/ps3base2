# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `(4J) Tile update` créé avec `arg=0x335f3ea0` | `/tmp/cmp.log` (natif) et `/tmp/f4.log` (wasm) | **identique des deux côtés** |
| Statut final natif : `blocked in 85 (r3 0x9800bb00)` | `/tmp/cmp.log:9126.597975` | **oui — et c'est décisif** |
| Le syscall 85 est `sys_event_flag_wait` | `runtime/src/lv2/sync.cpp:481` | oui |

# La rétractation du round 54

Quatre rounds (51-54) ont construit une chaîne causale autour du flag `0x9800BB00` :
créé à t=7,52 s, attendu à t=7,55 s, jamais posé, poseur jamais appelé. Chaque mesure était
juste, et j'en ai tiré une conclusion fausse : **que ce blocage était la cause de l'absence de
menu.**

La vérification manquante, que je n'avais pas faite : **le run natif valide a exactement le même
blocage** :

    I 9126.597975 status: ppu 0x1000006 '(4J) Tile update' blocked in 85
                 (r3 0x9800bb00 r31 0x0) syscalls +93

Même thread, même syscall (85 = `sys_event_flag_wait`), même flag `0x9800bb00`, et après
9 126 secondes il est **toujours bloqué**. Et ce run **atteint le monde**.

Le thread reçoit aussi le même argument des deux côtés (`0x335f3ea0`).

## Ce que ça signifie

`(4J) Tile update` bloqué sur `0x9800BB00` est **l'état normal du jeu**, pas un défaut. C'est
un thread qui attend un travail de tuilage qui n'arrive pas dans cet état de jeu — et le jeu
fonctionne sans lui.

Donc :
- Le blocage est **réel** et **correctement observé**.
- Il n'est **pas** la cause de l'absence de menu.
- Les rounds 51-54 ont diagnostiqué un symptôme partagé avec un run qui réussit.

## La leçon, la même que plusieurs fois déjà

Quand je mesure un symptôme, **je dois le mesurer aussi dans le run de référence qui réussit**.
Je l'ai fait pour le réseau (round 50, ce qui a éliminé la piste) mais pas pour ce flag. Si je
l'avais fait au round 51, j'aurais arrêté la piste immédiatement.

Le coût de l'omission : quatre rounds de diagnostic sur un comportement normal.

## Ce qui reste vrai et utile de ces quatre rounds

1. Le probe par identifiant (`PS3BASE_FLAG_ID_PROBE`) est un instrument qui marche et qui
   restera utile : il énumère les waits et les poses sans dépendre de `getenv`.
2. Les 9 sites de pose sont énumérés et localisés ; les 3 de `libsre` sont prouvés non atteints,
   les 3 poses réelles rattachées à `mc_036`.
3. Le flag `0x9800BB00` est créé par `main_thread` et attendu sans timeout — c'est une
   propriété du jeu, désormais documentée.

## Où chercher maintenant

La bonne question redevient : **qu'est-ce que le natif fait après t≈86 s que le wasm ne fait
pas ?** Le natif atteint le monde ; le wasm rend 11 221 frames et s'arrête.

Les mesures déjà disponibles pour la prochaine étape :
- `draw` natif 59-65 k/s contre 3 k/s wasm (round 36) — mais avec 240 k draws/soumis mesurés au
  round 45, donc la différence est dans l'exécution, pas dans la soumission.
- 16 threads vivants côté wasm, `main_thread` running (round 50).
- Aucun import non implémenté qui ne soit aussi absent côté natif (round 50).

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | oui, 56 Mo, vérifié à l'exécution |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | non |

Le jalon 3 reste ouvert. Ce round **rétracte** la chaîne causale des rounds 51-54 : le blocage
sur `0x9800BB00` est partagé avec un run natif qui atteint le monde, donc il n'explique pas
l'absence de menu.
