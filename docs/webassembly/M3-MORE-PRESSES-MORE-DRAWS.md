# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| La séquence réelle : `Down` puis `z` (`cross`) dans 4 états sur 5 | round 115 | oui |
| 4 appuis → 8k/s de draws | round 114 | oui |
| Sans appuis → 3k/s | rounds 109-112 | oui |

# La séquence réelle produit plus

## La mesure

| séquence | appuis | draws |
|---|---|---|
| aucune | 0 | **3k/s** |
| essai 1 | 4 (`cross` ×3, `start` ×1) | **8k/s** |
| **séquence réelle** | **16** (`down` + `cross` ×15) | **11k/s** |

## Ce que ça établit

Le dessin **croît avec le nombre d'appuis** : 3 → 8 → 11 k/s. Chaque appui ajouté pousse le jeu plus
loin dans son parcours.

Ce n'est pas une preuve que la séquence soit correcte, mais c'est une **corrélation mesurée sur trois
points**.

## Ce que ça suggère

Le jeu **progresse** à travers ses écrans à chaque appui. Il n'est pas bloqué : il avance lentement.

Et 16 appuis avec 1,2 s de pause = ~19 s de séquence. Le natif jouait jusqu'à 400 s.

## La correction évidente

**Allonger la séquence.** 16 appuis ne couvrent qu'une partie du parcours ; le natif en envoie
potentiellement des centaines.

Je peux générer 100+ appuis en boucle dans le script intégré.

## Ce qui reste incertain

`FORM` demande une navigation par focus que je ne peux pas faire à l'aveugle. Si le parcours l'exige, une
séquence aveugle s'arrêtera là.

Mais `PSNOFFLINE`, `DIALOG`, `MENU` et `LIST` acceptent tous `z` — soit quatre états sur cinq.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |