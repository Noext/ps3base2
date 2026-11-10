# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| Le wasm s'arrête à 65 allocations, t=38 s | round 134 | oui |
| Le natif fait 119 allocations | round 134 | oui |
| **Le wasm est en AVANCE jusqu'à t=90, puis se fait dépasser** | mesure | **oui — la chronologie** |

# Le wasm n'est pas en retard, il décroche à un moment précis

## La chronologie

| instant | natif | wasm |
|---|---|---|
| t<45 s | 56 | **65** |
| t<60 s | 56 | **65** |
| t<90 s | 61 | **65** |
| t<120 s | 66 | **65** |
| t<170 s | **119** | 65 |

## Ce que ça montre

Le wasm **dépasse** le natif jusqu'à t=90, l'**égale** vers t=120, puis **s'arrête** quand le natif bondit de
66 à **119**.

**53 allocations en 50 secondes** chez le natif — c'est la fenêtre du monde (t=120 à 170).

## Ce que ça établit

Le blocage n'est **ni précoce ni lent** : il est **ponctuel**, dans la fenêtre t=120-170.

C'est exactement là que le natif crée `FullscreenProgress`, `Server`, et fait `sys_vm_memory_map`.

## Ce que ça change

Ma lecture « le wasm est en retard » est **fausse**. Il est **à jour jusqu'à t=120**, puis **décroche**.

Cela **resserre** la fenêtre de diagnostic à 50 secondes de temps guest.

## Ce que ça ne dit pas

Ce qui, entre t=120 et t=170, déclenche les 53 allocations. C'est la même fenêtre que le map.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |