# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| L'absence de renderer ne bloque pas le jeu (200 s, 0 surface) | `M3-NO-RENDERER-DOES-NOT-BLOCK.md` (round 108) | oui |
| Le natif a mis 9 126 s | `/tmp/cmp.log` | oui |
| **Ma boucle d'attente est limitée à 200 itérations** | `runtime/src/wasm_entry.cpp:277` | **oui — la mesure était mal conçue** |

# Le run long n'a pas eu lieu

## Ce que j'ai lancé

`timeout 1560 node --experimental-wasm-exnref /tmp/ps3clip.js` — soit 26 minutes.

## Ce qui s'est produit

    ps3base-wasm: t=200s pc=0x002dc20c syscalls=434665 exited=0
    (le programme se termine)

**Le processus s'est terminé à t=200 s**, pas à 1 560 s. Ma boucle d'attente dans `wasm_entry.cpp` fait
**200 itérations**, puis le programme sort proprement.

Le run de 26 minutes **n'a jamais tourné** : le module s'arrête lui-même à 200 secondes.

## Ce que j'en tire

Je ne peux pas conclure sur « le wasm progresserait avec plus de temps » tant que le module s'arrête à 200 s
par construction.

C'est une limite de mon propre harnais de mesure, pas du jeu.

## Ce que le run a quand même montré

| | run court (200 s) | run long (200 s) |
|---|---|---|
| durée | 200 s | 200 s |
| threads | 18 | **19** |
| pc distincts | 3 | **8** |
| syscalls | 401 741 | **434 665** |

Le second run a produit **un thread de plus** et **cinq pc de plus** — mais les deux ont tourné 200 s. La
différence vient de la variabilité, pas d'une progression liée au temps.

## La correction nécessaire

Pour tester « le jeu progresserait avec plus de temps », il faut **allonger la boucle d'attente** de
`wasm_entry.cpp` — pas allonger le `timeout` du processus.

C'est une modification d'une ligne, et je l'ai apprise à mes dépens : mon `timeout` de 26 minutes contrôlait
le processus Node, alors que c'est le module qui décidait de sortir à 200 s.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |

Le jalon 3 reste ouvert. Ce round **échoue à produire la mesure visée** : le module s'arrête à 200 s par
construction, donc mon run de 26 minutes n'a pas eu lieu. La correction est d'allonger la boucle d'attente.
