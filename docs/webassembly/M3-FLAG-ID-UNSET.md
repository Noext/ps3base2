# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `sys_event_flag_wait` (85) et `sys_event_flag_set` (87) dans notre HLE | `runtime/src/lv2/sync.cpp:481`, `:599` | **oui** — l'HLE est correct, la condition manque |
| `flag_watched` filtre par `getenv`, inerte en wasm | `runtime/src/lv2/sync.cpp:79-88` | oui — d'où le probe par id |
| Le site d'attente est `0x25d0a4` dans `f_0025d01c_body` | `out/ppu/mc_023.cpp` | oui |

# Le blocage, nommé par identifiant

Le round 51 avait montré qu'un thread attend `0x7` et que le bit `0x4` n'arrive jamais. Ce
round va plus loin : **le problème n'est pas un bit manquant sur un flag partagé, c'est un flag
entier que personne ne touche.**

En traçant chaque `wait` et chaque `set` par identifiant :

    flagid: wait id=2550184704 bits=0x7 mode=0x11 lr=0x0025d0a4   (t=7.55 s)
    flagid: set  id=2550426624 bits=0x2 lr=0x00145d14            (t=8.80 s)
    flagid: wait id=2550426624 bits=0x3 mode=0x11 lr=0x0014f974   (t=8.86 s)
    flagid: set  id=2550496512 bits=0x1 lr=0x003a19e4            (t=9.38 s)

| | |
|---|---|
| flag attendu par `(4J) Tile update` | **2550184704** |
| nombre de `set` sur ce flag | **0** |
| flags effectivement posés | 2550426624, 2550496512 — **deux autres** |

**Le flag 2550184704 n'est jamais posé.** Ce n'est pas « le bit `0x4` manque sur un flag
partagé » — c'est « ce flag n'existe que pour être attendu ».

Les identifiants sont des adresses d'objet invité (`0x98000000 | handle`) : 2550184704 =
`0x9800C0C0`, 2550426624 = `0x9808C0C0`, 2550496512 = `0x980A00C0`. Trois objets
`EventFlag` distincts.

## Ce que ça implique

`(4J) Tile update` est l'un des threads du rendu par tuiles. Il bloque à l'infini sur un flag
que personne ne signalera, donc il ne participe pas au pipeline — cohérent avec les frames
vides.

Deux causes possibles, et je ne sais pas laquelle :

1. **Le poseur existe mais n'est jamais atteint** — un autre thread devrait poser ce flag et ne
   va pas jusque-là.
2. **Le poseur n'existe pas dans le chemin emprunté** — le jeu l'aurait posé par un chemin que
   ce boot n'emprunte pas (par exemple après une transition d'écran qui ne se produit pas).

## Ce que le round apporte de solide

Ce n'est plus une corrélation. C'est : **un identifiant précis, un compte de poses égal à zéro,
et la méthode pour le vérifier** (probe par id, indépendant de `getenv`).

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | oui, 56 Mo, vérifié à l'exécution |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | non |

Le jalon 3 reste ouvert. Le prochain pas est de chercher, dans le code lifté, une écriture qui
cible l'objet `0x9800C0C0`, et de voir si la fonction qui la contient est atteinte.
