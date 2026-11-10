# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| La géante serait une enveloppe avec inlining | round 146 | **testée ici** |
| `-fno-inline` sur `mc_070.cpp` : 1 225 031 → 581 509 o | mesure | oui |
| **Mais la plus grosse fonction reste 1 316 960 o** | mesure | **non — l'hypothèse est infirmée** |

# L'inlining n'explique pas la géante

## La mesure

| | `-Oz` | `-Oz -fno-inline` |
|---|---|---|
| objet `mc_070.o` | 1 225 031 o | **581 509 o** |
| module | 50,2 Mo | **49,6 Mo** |
| **plus grosse fonction** | 1 316 105 o | **1 316 960 o** |

## Ce que ça établit

`-fno-inline` **réduit l'objet de moitié** et le module de 0,6 Mo, mais **la plus grosse fonction ne bouge
pas**.

Elle n'est donc **pas** produite par l'inlining de `mc_070.cpp`.

## Ce que ça élimine

| hypothèse | statut |
|---|---|
| fusion par wasm-ld | **éliminée** (aucune option) |
| inlining de `mc_070.cpp` | **éliminée** par cette mesure |
| code source lifté géant | **éliminée** (max 13 ko) |

## Ce qui reste

La fonction de 1 316 960 octets est produite par **un autre fichier** ou par **le runtime C++**.

Le nom extrait (`f_006fe4d8`) est correct mais pointe vers un corps de 355 o — ce qui reste contradictoire.

## La leçon

Je n'ai toujours pas identifié la fonction. Trois hypothèses testées, trois éliminées. Je le dis plutôt que
de forcer une conclusion.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui — 49,6 Mo avec -fno-inline** |
| plus grosse fonction sous 1 Mo | non — 1 316 960 o, **origine non établie après 3 tests** |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |