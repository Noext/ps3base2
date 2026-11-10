# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `new color surface` : natif 6, wasm 0 | `M3-NO-SURFACE.md` (round 91) | oui |
| `0x0000-0FFF` reçoit ~930 000 écritures | `/tmp/ps3rng.js` | oui — mais lesquelles ? |
| Les registres de surface sont `0x0200-0x0214` | `rsx_renderer.cpp:61-65` | oui |

# Ce que le guest écrit réellement

## La mesure

J'ai compté les écritures par registre, quart par quart, sur `0x00-0x100` :

    low regs: 0x50=9020  0x60=18041  0x64=347189  0x68=329151  0x6c=18038

**Cinq registres, tous dans `0x50-0x6C`.** Tous les autres sont à zéro, y compris :
- `0x0200`, `0x0204`, `0x0208`, `0x0210`, `0x0214` (surface) → **0**
- `0x1A00+` (texture) → **0**
- `0x1808`, `0x1814` (draw) → voir ci-dessous

## Ce que sont ces cinq registres

| registre | signification | source |
|---|---|---|
| `0x50` | `dma_control + 0x48` | `lv2/rsx.cpp:313` |
| `0x60` | `sema_ctx_406e` | `lv2/rsx.cpp:314` |
| `0x64` | `sema_off_406e` | `lv2/rsx.cpp:315` |
| `0x68` | sémaphore | `lv2/rsx.cpp:316` |
| `0x6C` | sémaphore | `lv2/rsx.cpp` |

**Ce sont les registres de flip et de sémaphore de la FIFO.** Pas un registre de rendu.

## La contradiction avec mes rounds précédents

Le round 81 comptait **2 927 739 écritures dans `1800-19FF`** et le round 86 **418 572 draws de type 0**.

Or ici, `0x1808` (`BEGIN_END`) et `0x1814` (`DRAW_ARRAYS`) ne sont **pas** dans la liste des registres
bas — mais ils sont > `0x100`, donc **hors de mon compteur à 64 entrées**. Mon compteur ne couvrait que
`0x00-0xFF`.

Donc les deux mesures ne se contredisent pas : le guest écrit **à la fois** les sémaphores (`0x50-0x6C`,
430 000 fois) **et** les registres de draw (`0x1800-0x19FF`, 2,9 M fois). Mais il n'écrit **ni surface
ni texture**.

## Ce que ça établit

Le profil du guest est :

| catégorie | écritures |
|---|---|
| sémaphores / flip | **430 000** |
| draw (`1800-19FF`) | **2 900 000** |
| clear / programme (`1A00-1FFF` hors texture) | **1 240 000** |
| **surface (`0200-0214`)** | **0** |
| **texture (`1A00+ champ 0`)** | **0** |

**Le jeu dessine dans une surface qu'il n'a jamais configurée, avec des textures qu'il ne lie pas.**

## Ce que ça implique

Ce n'est pas un blocage de l'émulateur : c'est un **état de jeu cohérent avec un écran qui n'a pas de
cible de rendu**. Le jeu émet des draws que le matériel réel aurait ignorés faute de surface.

Ce qui suggère fortement que le jeu est **avant** l'initialisation du rendu — pas bloqué dedans.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |

Le jalon 3 reste ouvert. Ce round **établit le profil exact des écritures RSX** : 430 000 sémaphores,
2,9 M draws, **zéro surface, zéro texture**. Le jeu émet des draws sans cible de rendu configurée.
