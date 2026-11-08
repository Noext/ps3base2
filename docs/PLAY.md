# Jouer a Minecraft PS3 (BLES01976) sur ps3base v2

## Lancement rapide

```bash
cd /root/Dev/ps3base2
tools/play.sh
```

## Table des touches

Lue dans `runtime/src/host_input.cpp` (fonction `key()`), pas de memoire.

| Touche | Bouton PS3 | Usage en jeu |
|---|---|---|
| `z` | Cross | valider, sauter |
| `x` | Circle | annuler, quitter |
| `a` | Square | |
| `s` | Triangle | inventaire |
| `i` / `k` | stick gauche avant / arriere | marcher |
| `j` / `l` | stick gauche gauche / droite | marcher |
| `r` / `f` | stick droit haut / bas | viser verticalement |
| `t` / `g` | stick droit gauche / droite | viser horizontalement |
| `1` | L2 | poser un bloc |
| `3` | R2 | casser un bloc |
| `Q` | L1 | **changer de slot de hotbar** (vers la gauche) |
| `E` | R1 | **changer de slot de hotbar** (vers la droite) |

**Note sur la hotbar** : sur PS3 le changement de slot se fait avec L1/R1, pas
avec le D-pad. Le D-pad a ete teste en jeu et ne change effectivement pas le slot
selectionne ; `Q` et `E` sont les touches a utiliser.

## Creer un monde

1. Accepter les termes (`z`), fermer le dialogue d'autosave (`z`).
2. `Play Game` puis `Start Game` (`z`, `z`).
3. Sur le formulaire, descendre jusqu'a **Create New World** (fleche bas x6) --
   le curseur atterrit d'abord sur la difficulte, verifiez a l'ecran.
4. Valider (`z`) puis attendre ~250 s la generation.

## Jouer

- Casser : viser le sol (sticker droit), maintenir `3` (R2).
- Le bloc casse tombe en objet ; marcher dessus le ramasse et remplit un slot.
- Poser : `1` (L2), HUD affiche `L2 Place`.

## Limites connues

- Le dialogue d'autosave peut se repeter et ignorer les appuis. Si le jeu semble
  bloque, verifiez `grep -c 'sys_vm_memory_map ->'` dans le log : zero signifie
  qu'aucun monde n'a ete cree.
- Le crash `f_0064c044` peut survenir apres plusieurs minutes de jeu (course
  entre deux threads de sauvegarde, en cours d'investigation).
- Le chargement d'un monde existant crashe encore.
