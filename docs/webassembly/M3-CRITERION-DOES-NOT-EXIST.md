# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le prompt exige « la ligne de log du menu » comme critère du jalon 3 | `docs/PORT-WEB-PROMPT.md:201-202` | oui |
| **Aucun log de menu n'existe** dans le runtime | `grep 'menu' runtime/src/` → 0 `LOG_*` | **oui — le critère ne correspond à aucun log** |
| Le jalon 2 utilise `sys_vm_memory_map ->` comme critère mécanique | `tools/drive_world.py:59-62,79` | **oui — c'est un critère réel** |

# Le critère du prompt ne correspond à aucune ligne de log

## La vérification

    grep -rniE 'menu|main menu|title screen' runtime/src/ --include=*.cpp | grep 'LOG_'
    (aucun résultat)

**Notre runtime n'émet aucune ligne de log de menu.** Le critère du prompt — « le log montre le titre
atteignant le menu principal, critère mécanique : la ligne de log du menu » — désigne une ligne qui
**n'existe pas** dans notre implémentation.

## Ce que le jalon 2 fait à la place

Le jalon 2 a un critère mécanique qui **existe** :

    def world_mapped(log):
        with open(log, "rb") as f:
            return b"sys_vm_memory_map ->" in f.read()

C'est une ligne réellement produite par le runtime, et `drive_world.py` la cherche pour imprimer
`WORLD-MAPPED`.

## La mesure

| | natif (run monde) | wasm |
|---|---|---|
| `sys_vm_memory_map` | **2** | **0** |

Le wasm n'atteint **jamais** l'appel qui marque l'entrée dans le monde, alors que le natif l'atteint
deux fois.

## Ce que ça clarifie

Le jalon 3 n'a **pas** de critère mécanique implémentable en l'état : la ligne de log qu'il désigne
n'existe pas. Deux options :

1. **Ajouter la ligne au runtime** — instrumenter le point où le titre entre dans son menu, une fois
   qu'on saura où il est.
2. **Utiliser le critère du jalon 2** — `sys_vm_memory_map ->`, qui est mécanique, réel et déjà utilisé
   par l'outillage.

L'option 2 est cohérente avec l'ordre du prompt (jalon 2 avant jalon 3) : le critère du monde est plus
exigeant que celui du menu, et si le wasm l'atteint, le menu l'est aussi.

## Ce que ce round ne fait pas

Il ne franchit pas le jalon 3. Il établit que **son critère textuel n'est pas mesurable tel quel**, ce
qui est une information nécessaire pour ne pas prétendre l'avoir satisfait.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **n'existe pas dans le runtime** |
| `sys_vm_memory_map ->` (critère réel du jalon 2) | **non** (0 contre 2) |

Le jalon 3 reste ouvert. Ce round **clarifie le critère** : celui du prompt n'est pas implémentable
tels quels, et celui du jalon 2 donne une mesure concrète que le wasm ne satisfait pas non plus.
