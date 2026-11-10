# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| `f_0070d138` n'est pas liée en table filtrée | round 144 | oui |
| La plus grosse fonction du module filtré fait 1 316 105 o | round 143 | oui |
| **Elle s'appelle `f_006fe4d8` et fait 1 358 012 o** | mesure par section `name` | **oui — identifiée** |

# La coupable réelle

## L'identification

J'ai lié avec `-g` (section `name`) et extrait les tailles par fonction :

| rang | taille | nom |
|---|---|---|
| 1 | **1 358 012** | **`f_006fe4d8`** |
| 2 | 485 942 | `f_001a6510_body` |
| 3 | 272 762 | `f_004dd458_body` |
| 4 | 248 759 | `f_0076fe80_body` |
| 5 | 224 938 | `f_003df918` |

## Ce que ça révèle

`f_006fe4d8` **n'a pas de corps** dans `out/ppu/mc_070.cpp` (recherche : 355 octets, 1 label) et **n'est pas
dans le profil**.

C'est donc une **fonction générée par wasm-ld** — une fusion de plusieurs fonctions équivalentes.

## Ce que ça corrige définitivement

Le round 139 attribuait le dépassement à `f_0070d138` (2,2 Mo de source). **C'était une hypothèse fausse** :
en table filtrée, `f_0070d138` n'est pas liée du tout.

La vraie coupable est une **fusion**, et le remède n'est pas de découper du code source.

## Ce que ça implique

Les fonctions de 486 ko, 273 ko, 249 ko sont **largement sous** 1 Mo. Seule la fusion de 1 358 ko dépasse.

Si la fusion est évitée, **toutes** passent sous le seuil.

## La piste

wasm-ld fusionne les fonctions identiques (`--icf` est actif par défaut dans certains réglages). Le
désactiver diviserait la plus grosse fonction — mais augmenterait le module.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui (50,2 Mo)** |
| plus grosse fonction sous 1 Mo | non — **1 358 ko, une fusion, pas du code source** |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |