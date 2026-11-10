# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| Le drapeau lu 0xbe0704 n'est jamais écrit par le code de la boucle | round 129 | oui |
| Le toggle écrit à 0x00d70704 (autre adresse) | round 129 | oui |
| **7 fonctions écrivent à l'offset 1796** | recherche de ce round | **oui — la piste** |

# Les sept écrivains de l'offset 1796

| fonction | stores | proximité |
|---|---|---|
| **f_002dc29c_body** | 1 | **même région (0x2dc2xx)** |
| f_002da6f0_body | 1 | proche (0x2da6xx) |
| f_00203c54_body | 2 | autre |
| f_000ceb30_body | 1 | bas |
| f_000cf01c_body | 1 | bas |
| f_0065f414_body | 1 | haut |
| f_0066fbb0_body | 1 | haut |

Le drapeau **est écrit** par sept endroits. Ce n'est pas un état non initialisé.

Mais **aucune** de ces écritures ne s'est produite pendant mes 200 s : la valeur reste 0.

## L'hypothèse

`f_002dc29c` est **immédiatement adjacent** à `f_002dc238` et `f_002dc1e4`. C'est probablement la fonction
qui **devrait** poser le drapeau, et qui n'est pas appelée.

## La chaîne testable

    f_002dc29c jamais appelé
      → drapeau 0xbe0704 reste 0
        → f_002dc238 boucle sans fin
          → 300 000 usleep

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |