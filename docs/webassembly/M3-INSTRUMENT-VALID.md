# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le code des constantes de surface s'exécute, mais rien n'est écrit | `M3-SURFACE-CODE-REACHED.md` (round 96) | oui |
| Les contextes `0x300-0x603` sont des *packages*, pas des méthodes FIFO | `runtime/src/lv2/rsx.cpp:211-216` | **oui — écarté** |
| Le natif crée ses surfaces à t=4,85 s | `/tmp/cmp.log` | oui |

# Ce que le natif crée, et ce que ça implique

## Les surfaces natives

    t=4,854  new color surface at local 0x280000 1280x480 format 44 pitch 0x1400
    t=4,854  new depth surface at local 0x0      1280x480 format 129 pitch 0x1400
    t=4,951  new color surface at local 0x500000 1280x720 format 44 pitch 0x1400
    t=4,951  new depth surface at local 0x0      1280x720 format 129 pitch 0x40

Trois choses remarquables :

1. **Deux tailles** : 1280×480 puis 1280×720. Le jeu redimensionne sa cible de rendu.
2. **Le depth offset est 0x0** dans les deux cas, alors que le color est à `0x280000` puis `0x500000`.
   Le depth et le color ne partagent pas la même région.
3. **Format 44 pour la couleur, 129 pour la profondeur** — valeurs RSX fixes.

## Ce que ça implique pour notre HLE

Le renderer natif **créait** ces surfaces en lisant les registres de surface. Puisque le natif les lit,
le guest les a écrits.

Chez nous, le guest ne les écrit pas. Ce n'est donc pas un problème du stub : c'est que le chemin invité
qui les écrit n'est pas emprunté.

## Ce que j'ai écarté ce round

J'ai vérifié si les registres de surface étaient **interceptés avant** `method()` :
`lv2/rsx.cpp:211-216` intercepte `0x300-0x302` et `0x600-0x603`, mais ce sont des **packages de contexte**
(traités par la fonction `context_attribute`), pas des méthodes FIFO. Les registres `0x0200-0x0214` passent
bien par `method()`, donc mon compteur les verrait.

**Mon instrument est valide.**

## Le fait qui reste

| | natif | wasm |
|---|---|---|
| surfaces couleur créées | **6** | **0** |
| écritures `0x0200-0x0214` | (guest les écrit) | **0** |

Le jeu, dans le run wasm, n'écrit pas les registres qui décriraient une cible de rendu — et pourtant il
émet 2,9 M de commandes de draw.

## Ce que ça implique pour le jalon 3

Le prompt demande un stub qui **compte** les commandes RSX. Un stub qui compte n'a pas besoin de
surfaces. Mais ce jeu **conditionne visiblement son progrès** à l'existence d'une cible : il ne
configure pas de surface, donc le renderer n'en crée pas, donc… rien ne suit.

Ce n'est pas notre stub qui l'empêche : c'est que le guest n'a jamais demandé.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |

Le jalon 3 reste ouvert. Ce round **valide l'instrument** (les registres de surface passent bien par
`method()`) et documente ce que le natif crée précisément.
