# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| L'instrument des surfaces est valide (passe par `method()`) | `M3-INSTRUMENT-VALID.md` (round 97) | oui |
| Le guest n'écrit jamais `0x0200-0x0214` | `M3-WRITES-PROFILE.md` (round 92) | oui |
| Le natif crée 6 surfaces | `/tmp/cmp.log` | oui |

# L'échelle du fait

## Le débit de méthodes

| | natif | wasm |
|---|---|---|
| méthodes / s | **1 745k** | **508k** |
| durée | 82 s | 200 s |
| **total estimé** | **~143 M** | **~101 M** |

Le wasm traite **101 millions de méthodes** — 70 % du volume natif.

## Ce que ça donne au fait

Sur **101 millions de méthodes** traitées par le wasm, **zéro** écriture dans `0x0200-0x0214`.

Ce n'est pas une question d'échantillonnage ou de volume : à cette échelle, une seule écriture de surface
aurait été comptée. Le guest en émet **exactement zéro**.

## Ce que ça élimine définitivement

| hypothèse | statut |
|---|---|
| le stub perd les écritures de surface | **éliminée** (l'instrument est validé, il les verrait) |
| le volume est trop faible pour être vu | **éliminée** (101 M méthodes) |
| le guest écrit les surfaces puis le stub les ignore | **éliminée** (0 écriture au point d'arrivée) |

## Ce qui reste comme explication

Le jeu, dans son parcours wasm, **ne prend pas la branche** qui configure une cible de rendu. Et pourtant
il émet 2,9 M de commandes de draw et 430k sémaphores.

C'est un état de jeu **incohérent avec un vrai RSX** : un vrai matériel aurait ignoré ces draws faute de
surface. Ce qui suggère que le jeu ne les vérifie pas — il émet du travail « à vide » en attendant autre
chose.

## Ce que ça voudrait dire

Le jeu serait dans une **boucle d'attente qui émet des draws** sans configurer de cible. Une fois la
condition d'attente satisfaite, il configurerait sa surface et afficherait son contenu.

C'est cohérent avec tout ce qui précède : 56 fps, 418k draws d'une seule primitive, 0 texture, 0 surface.

## Ce que je n'ai pas établi

Quelle condition le jeu attend. Les rondes 69-77 avaient proposé l'énumération des sauvegardes (absente),
puis le flag `0x9800BB00` (partagé avec le natif, donc non causal).

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |

Le jalon 3 reste ouvert. Ce round **chiffre l'échelle** du fait : 101 millions de méthodes traitées, zéro
écriture de surface. L'instrument et le volume sont hors de cause.
