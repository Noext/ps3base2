# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| Natif 8 fichiers musicaux, wasm 1 | round 136 | oui |
| Hypothèse : thread audio ne redemande pas | round 136 | **non vérifiée** |
| Le wasm **initialise** l'audio (port 0, 32 blocs à `0x100b0000`) | `/tmp/big.log` | oui |
| Les stats audio sont dans le chemin **non-wasm** | `cellAudio.cpp:210` | **oui — ce n'est pas un défaut** |

# L'audio fonctionne, mais sans périphérique

## La mesure

| | natif | wasm |
|---|---|---|
| `cellAudioInit` | oui | **oui** |
| port ouvert | oui | **oui** (2 canaux, 32 blocs) |
| `audio stats` | 938 blocs | **aucune ligne** |

## Pourquoi aucune ligne n'apparaît

`cellAudio.cpp:210` logue les statistiques dans le chemin **SDL** (`#else`).

En wasm, l'arm est un **sink no-op** : les blocs sont comptés et jetés, sans statistiques.

**Ce n'est pas un défaut** : c'est le portage correct d'un module audio sans périphérique.

## Ce que ça retire

Mon hypothèse du round 136 — « le thread audio ne redemande pas de piste » — **n'a aucune preuve**.

Je n'ai pas mesuré le thread audio ; j'ai extrapolé de l'absence d'une ligne de log qui, par construction,
n'existe pas en wasm.

## Ce que le code confirme

L'arm wasm est documenté : *« Every other part of the HLE -- port allocation, the index area, the
block/offset protocol -- is device-independent, and a title that receives a valid port config proceeds. »*

Le jeu reçoit donc une configuration de port **valide**. Rien ne l'empêche de continuer.

## Ce que ça laisse

Charger 1 piste au lieu de 8 peut être **normal** : un jeu ne charge la musique qu'au moment où il entre
dans le menu ou le monde. Un wasm qui n'y arrive pas charge moins de musique — **conséquence**, pas cause.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |