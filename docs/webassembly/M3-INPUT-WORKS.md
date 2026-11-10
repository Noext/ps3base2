# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| Le wasm n'a jamais reçu d'appui (`getenv` inerte) | round 113 | oui |
| Le natif de référence utilisait `PS3BASE_INPUT_CMD` | `tools/run_world.sh:228` | oui |
| Table des boutons : `cross 3 0x40`, `start 2 0x08` | `cellPad.cpp:52-56` | oui |

# La séquence intégrée, et son effet mesuré

## Ce que j'ai ajouté

1. Un script **en constante de compilation** (`PS3BASE_INPUT_BUILTIN`) :

       cross 120 900 / cross 120 900 / start 120 900 / cross 120 900

2. Un déclencheur : `load_command_queue()` était appelé **uniquement si `getenv`** renvoyait un chemin.

       #if PS3BASE_INPUT_BUILTIN
           if (!cmd_loaded()) load_command_queue();
       #endif

## Le résultat

    pad: queued 4 built-in command(s)

| | sans appuis | avec appuis |
|---|---|---|
| **draws** | **3k/s** | **8k/s** |
| part de draw | 0,0 % | **0,1 %** |
| crash | non | **non** |
| threads | 20 | 17 |
| `unknown indirect` | 0 | **0** |

**Les appuis multiplient le dessin par 2,7.** Le jeu réagit à l'entrée.

## Le piège évité

Le premier essai a crashé sur `unknown indirect target 0x00752740` : mes objets de table **filtrés**
(`p_table4.o`) ne contenaient pas cette fonction, alors que la table **complète** la contient.

Après reliaison avec `p_table.o` : **0 crash**.

C'est la troisième fois que mes propres tables filtrées cassent un run. La leçon des rounds 42, 49 et 107
se répète : **une table filtrée est un instrument, pas un livrable.**

## Ce que ça établit

Le wasm **réagit** à l'entrée. Le jeu n'est donc pas dans une boucle insensible : il attendait des appuis.

Passer de 3k/s à 8k/s reste loin des 68k/s natifs, mais c'est le **premier progrès mesurable** depuis
plusieurs rounds.

## Ce que ça ne prouve pas

Que la séquence soit la bonne. Le natif recevait un script produit par `run_world.sh`, dont je ne connais
pas le contenu exact. Mes quatre appuis sont une supposition raisonnable.

## La prochaine mesure

Allonger le run avec l'entrée active et voir si `draw` continue de croître, ou si le jeu s'arrête à 8k/s.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |