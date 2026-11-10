# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le guest n'émet aucun `TEXTURE_OFFSET` (3 instruments) | `M3-NO-TEXTURE-OFFSET-AT-FIFO.md` (round 90) | oui |
| Les pixels décodés vont aux mêmes adresses des deux côtés | round 90 | oui |
| `new color surface` : natif 6, wasm 0 | `/tmp/cmp.log`, `/tmp/def.log` | **oui — le fait décisif** |

# La différence n'est pas dans les textures, elle est avant

## Le fait mesuré

| événement | natif | wasm |
|---|---|---|
| **`new color surface`** (allocation de surface de rendu) | **6** | **0** |
| `new depth surface` | oui | **0** |
| `pipeline cache` (chargement du cache de pipeline) | **1** | **0** |
| `texloc` (liaisons de texture) | **40** | **0** |
| `texfifo` (écriture du registre d'offset) | — | **0** |

## Leur ordre chez le natif

    t=4,500  cellPngDec: Open ... (décodage d'un PNG)
    t=4,854  rsx: new color surface at local 0x280000 1280x480 format 44 pitch 0x1400
    t=4,854  rsx: new depth surface at local 0x0 1280x480 format 129
    t=4,856  rsx: pipeline cache: loaded 525093 bytes
    t=4,857  rsx: vertex program fd4be450bbeda1e5 start 9: ok
    t=4,947  texloc: off 0x2e50000 ...

**Le natif alloue ses surfaces AVANT de lier des textures.** C'est la première différence, et elle est
en amont de tout le reste.

## Ce que ça change

Les rounds 79-90 cherchaient la cause dans la **liaison de texture**. Elle est **en aval** de la
création des surfaces : un jeu qui n'a pas de surface de rendu ne peut pas lier de texture dessus.

Donc le point d'entrée remonte encore :

    pas de surface couleur (0 contre 6)
      → pas de cible de rendu
        → pas de TEXTURE_OFFSET émis
          → pas de texloc
            → pas de thread d'affichage
              → pas de sys_vm_memory_map
                → pas de monde

## Pourquoi le wasm ne crée pas de surface

`new color surface` vient du renderer (`rsx_renderer.cpp`, côté `bindkind`/surfaces). Le stub no-display
ne crée **aucune** surface : il compte les commandes.

C'est un défaut du stub **conforme au prompt** (point 6 : compter les commandes), mais qui empêche le
jeu de poursuivre.

## Ce que je n'ai pas établi

Je n'ai pas vérifié que la création de surface est **une condition** du progrès du jeu, ou seulement un
symptôme concomitant. Pour le tester, il faudrait que le stub alloue une surface factice et voir si le
`TEXTURE_OFFSET` apparaît.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |

Le jalon 3 reste ouvert. Ce round **remonte le point d'entrée** : ce n'est pas la liaison de texture qui
manque en premier, c'est l'**allocation des surfaces de rendu** (0 contre 6).
