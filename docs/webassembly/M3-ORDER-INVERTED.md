# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| 101 M méthodes wasm, zéro écriture de surface | `M3-SCALE-ELIMINATES.md` (round 98) | oui |
| Le natif dessine dès t=5,4 s | `/tmp/cmp.log` | **oui — nouveau** |

# L'ordre du rendu est inversé

| | natif | wasm |
|---|---|---|
| premier échantillon avec des draws | **t=5,4 s** | **t=24,0 s** |
| dessine avant de flipper ? | **oui** | **non** (flippe à t=13,8 s) |
| débit de draw final | 65k/s | **3k/s** |

    natif : surfaces (t=4,85) → dessine (t=5,4) → flippe (t=10,4)
    wasm  : flippe (t=13,8) → dessine (t=24,0)   [aucune surface]

Le wasm **présente avant de dessiner**, sans jamais configurer de surface. Les flips du wasm sont donc
**vides par construction**.

Le rapport 3k/65k suggère que le wasm ne dessine que **5 % du contenu** du natif.

# Le candidat concret

`rsx::present` retourne **toujours faux** dans le stub (round 21), donc `gpu::flip` est appelé. Si le jeu
attend que `present` retourne vrai pour continuer, c'est un blocage — et c'est testable.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |
