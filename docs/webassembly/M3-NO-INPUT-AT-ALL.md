# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| Le wasm n'écrit pas de sauvegarde et ne bascule pas en mode jeu | rounds 111-112 | oui |
| **Le run natif de référence utilise `PS3BASE_INPUT_CMD`** | `tools/run_world.sh:228` | **oui — jamais reproduit côté wasm** |
| Mon stub wasm renvoie un pad toujours neutre | `host_input_port.cpp` | **oui** |

# Le wasm n'a jamais reçu d'appui

## Le mécanisme natif

`tools/run_world.sh` lance le jeu avec un fichier de commandes d'entrée :

    PS3BASE_INPUT_CMD=$ICMD

et le format est `name [hold_ms] [gap_ms]`, avec 16 boutons nommés (`cross`, `start`, `up`, ...).

`cellPad.cpp` lit ce fichier via `getenv`, puis pousse les appuis dans une file jouée
automatiquement.

## Ce que fait le wasm

| | natif | wasm |
|---|---|---|
| pad attaché | `ps3base virtual pad` | non |
| `PS3BASE_INPUT_CMD` lu | oui | **non — `getenv` inerte** |
| appuis injectés | oui (scriptés) | **aucun** |

## Pourquoi je ne l'avais jamais vu

`getenv` est inerte dans le build wasm (mesuré aux rounds 20-24). Le fichier de commandes n'est donc
**jamais lu**, et le pad reste neutre à chaque appel.

Les rounds 32 et 78 avaient déjà buté sur ce problème pour d'autres variables. Je n'avais jamais relié
cela à l'**entrée**. 

## Ce que ça implique

Le natif qui atteint le monde a reçu **une séquence d'appuis scriptée** (probablement `cross` pour
valider les termes, puis des appuis de menu). Le wasm n'en a reçu **aucune**.

Si le jeu exige un appui pour passer de l'écran de chargement au menu, le wasm ne le franchira
**jamais** — et aucune quantité de temps n'y changera rien.

C'est cohérent avec tout ce que les rounds 109-112 ont mesuré : le wasm progresse mais reste bloqué à
3k/s de draws, là où le natif a basculé après avoir reçu des appuis.

## La correction à appliquer

1. Injecter une séquence d'appuis en **constante de compilation** (le format est connu : `name [hold] [gap]`).
2. Reproduire au minimum : `cross` (accepter), puis `start` si nécessaire.

La table des boutons est disponible dans `button_of` :

    {"cross", 3, 0x40, 14}  {"start", 2, 0x08, 0}  {"circle", 3, 0x20, 13}

## Ce que ça ne prouve pas

Que les appuis soient **la** cause. Le natif a aussi des threads de sauvegarde et un renderer. Mais
c'est la première différence **structurelle** identifiée depuis plusieurs rounds, et elle est
reproductible.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |