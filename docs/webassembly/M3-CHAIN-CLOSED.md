# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `(4J) Chunk update` est atteint mais ne boucle pas | `M3-WRONG-CALLER.md` (round 76) | **corrigé — il fait un wait** |
| Le wasm fait 1 `wait-in`, le natif 219 207 | `/tmp/def.log`, `/tmp/cmp.log` | oui |
| `(4J) FullscreenProgress` manque au wasm | `M3-ALLOC-DIVERGENCE.md` (round 70) | **oui — et c'est lui qui pose le bit** |

# Corrections et chaîne complète

## Correction du round 75 et 76

Les rounds 75-76 disaient « aucun event flag wait » et « la fonction est atteinte une fois depuis le
mauvais appelant ». **Le wasm fait bien un wait**, et depuis le **bon** endroit :

    flagtrace: wait-in  thread=(4J) Chunk update(0x1000009) lr=0x0014f974
                         bits=0x3 mode=0x11 pattern 0x2 -> 0x2 waiters=1

Arguments **identiques** au natif :

    natif:  bits=0x3 mode=0x11 pattern 0x2 -> 0x2 waiters=1
    wasm :  bits=0x3 mode=0x11 pattern 0x2 -> 0x2 waiters=1

## La différence

| | natif | wasm |
|---|---|---|
| `wait-in` | 1 (puis répété) | **1** |
| `wait-ok` | **des milliers** | **0** |
| état final du thread | sort du wait | **`blocked in 85`** |

Le wasm **entre dans le wait et n'en sort jamais**.

## Pourquoi

Le mode `0x11` est **AND** : le thread attend que les bits `0x3` soient **tous deux** présents. Le
pattern courant est `0x2` — il manque **`0x1`**.

Qui pose `0x1` chez le natif ?

    flagtrace: set  from=(4J) FullscreenProgress lr=0x0014feec bits=0x1 pattern 0x2 -> 0x3

**`(4J) FullscreenProgress`** — précisément l'un des 8 threads que le wasm **ne crée jamais**
(round 57) et que le natif crée après trois allocations de 1 Mo et un `sys_vm_append_memory`
(round 70).

## La chaîne complète, fermée

    wasm s'arrête à 59 allocations (natif 119), t=18,9 s
      → pas de sys_vm_append_memory
        → (4J) FullscreenProgress jamais créé
          → bits=0x1 jamais posé
            → (4J) Chunk update bloqué à l'infini sur bits=0x3 mode=AND
              → pas de coordination du rendu par tuiles
                → frames vides, pas de menu

Chaque maillon est mesuré. Le point d'entrée de la chaîne est l'**arrêt des allocations** au round 70.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **ferme la chaîne causale** : le bit manquant est posé par le
thread que le wasm ne crée pas, et ce thread manque parce que les allocations s'arrêtent.
