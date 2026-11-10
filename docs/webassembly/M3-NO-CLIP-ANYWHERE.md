# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le guest écrit format, pitch, 3 textures — mais pas `0x200`/`0x204` | `M3-PARTIAL-SURFACE.md` (round 104) | oui |
| `clip_w() = R(SURFACE_CLIP_HORIZONTAL) >> 16` | `rsx_renderer.cpp:1181` | **oui** |
| Le natif crée des surfaces **1280×480** puis **1280×720** | `/tmp/cmp.log` | oui |

# L'asymétrie, chiffrée

Pour que `clip_w()` retourne 1280 : `R(0x200) = 1280 << 16 = 0x05000000`.

| registre | natif (déduit) | wasm (mesuré) |
|---|---|---|
| `0x200` clip horizontal | `0x0500xxxx` | **0** |
| `0x204` clip vertical | `0x01E0xxxx` | **0** |
| `0x208` format | écrit | **0x121** |
| `0x1A00` texture 0 | écrit | **0x280000** |

## Aucun registre non nul ne contient 1280 en haut

Notre dump montre `0x000=31337000` (partie haute **12595**) et `0x180=66604200` (partie haute **26118**).
Ni l'un ni l'autre n'est un clip décalé.

**Aucun registre non nul du dump ne contient 1280 (`0x0500`) dans ses 16 bits hauts.**

# Ce que ça établit

Le guest wasm **n'écrit à aucun endroit** la dimension 1280 sous une forme compatible avec `clip_w()`.

# Hypothèses éliminées

| hypothèse | statut |
|---|---|
| le guest écrit le clip ailleurs | **éliminée** |
| le stub perd l'écriture | **éliminée** — registre lu directement |
| problème de format/masque | **éliminée** — `>> 16` donne 12595 ou 26118 |

# Ce qui reste

Le guest **omet** l'écriture des dimensions. Il configure **4 des 6** registres d'une surface (format,
2 pitchs, 3 textures) et omet **les 2 dimensions** et l'**offset couleur**.

Je ne sais pas l'expliquer et je n'inventerai pas de cause.

# Une piste concrète

`SURFACE_FORMAT` (`0x208`) et `SURFACE_CLIP` (`0x200`) sont écrits par le **même code** chez un jeu
ordinaire (`cellGcmSetSurface` écrit tout d'un bloc). Ici, l'un est écrit et l'autre pas.

Cela suggère que le jeu **n'appelle pas** la fonction de configuration complète, mais écrit les registres
un par un — et s'arrête au milieu.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 MB | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |
