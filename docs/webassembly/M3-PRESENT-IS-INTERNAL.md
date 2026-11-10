# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Ordre du rendu inversé : le wasm présente avant de dessiner | `M3-ORDER-INVERTED.md` (round 99) | oui |
| `rsx::present` est **interne** au stub, pas visible du guest | `rsx_no_display.cpp:305-309` | **éliminé** |
| Le jeu importe `cellGcmGetFlipStatus`, `cellGcmSetFlip`, `cellGcmGetCurrentField`, `cellGcmGetCurrentDisplayBufferId` | `out/ppu/table.cpp` | **oui** |

# Le candidat du round 99 était mal posé

## Ce que j'ai vérifié

`rsx::present` est une fonction **interne** au stub :

    bool present(uint32_t buffer_id) {
        (void)buffer_id;
        return false;
    }

Le guest ne la voit pas : elle sert seulement au chemin FIFO interne (`lv2/rsx.cpp:160`). Un jeu ne peut
pas « attendre » qu'elle retourne vrai.

Donc le candidat du round 99 **ne tient pas**.

## Ce que le guest voit réellement

Le jeu importe quatre fonctions de présentation :

    cellGcmGetFlipStatus
    cellGcmSetFlip
    cellGcmGetCurrentField
    cellGcmGetCurrentDisplayBufferId

**Aucune n'est implémentée** dans notre code (`grep` sur `runtime/` : zéro occurrence).

Elles devraient donc produire un log `unimplemented cellGcm*`. Or :

| | natif | wasm |
|---|---|---|
| logs `unimplemented cellGcm*` | **0** | **0** |

**Aucune des deux parties ne les appelle.** Ni le natif, ni le wasm.

## Ce que ça élimine

| hypothèse | statut |
|---|---|
| `rsx::present` bloquerait le jeu | **éliminée** — invisible du guest |
| un HLE de flip manquant bloquerait | **éliminée** — jamais appelé des deux côtés |

## Ce que ça laisse

Le wasm présente avant de dessiner sans passer par ces HLE. La présentation vient donc d'ailleurs — du
chemin FIFO interne, où `lv2/rsx.cpp:160` appelle `gpu::flip` quand `gpu::rsx::present` retourne faux.

C'est **notre propre code** qui produit ces flips, pas une demande du guest.

## Ce que ça implique

Les 11 221 flips du wasm sont peut-être **artificiels** : produits par notre chemin interne sur réception de
paquets vblank, et non par une vraie demande de présentation du jeu.

Ce serait cohérent avec l'ordre inversé : le jeu n'a jamais demandé à présenter, c'est nous qui comptons une
présentation à chaque paquet.

## Ce qu'il faudrait mesurer

Compter les paquets vblank (package `0x102`) reçus, et voir s'ils correspondent aux flips comptés. Si oui,
les flips sont un **compteur dérivé**, pas une activité du jeu.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |

Le jalon 3 reste ouvert. Ce round **invalide le candidat du round 99** et reformule : les flips sont
probablement un compteur interne, pas une activité du jeu.
