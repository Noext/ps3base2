# Les quatre arguments reels de `_cellGcmInitBody`

## La mesure, prise AVANT le handler

    [gcmargs] context=0xd72800 cmdSize=0xfa000 ioSize=0x100000 ioAddr=0x40100000

| argument | valeur | commentaire |
|---|---|---|
| `context` | `0x00d72800` | le pointeur de contexte **est** l'adresse du champ nul |
| `cmdSize` | `0xfa000` | 1 024 000 o |
| `ioSize` | `0x100000` | 1 Mo, **aligne** |
| `ioAddr` | `0x40100000` | **aligne sur 1 Mo** |

## Ce que ca etablit

| verification | resultat |
|---|---|
| `ioAddr` aligne | **oui** — `0x40100000` |
| `ioSize` aligne | **oui** — `0x100000` |
| `ioAddr` mappe | **oui** — region 2 a `0x40000000`, alloue `0x40100000` |
| `MapEaIoAddress` logue quelque chose | **non** — la fonction n'est jamais atteinte |

**Les deux conditions d'echec de `gcm_map_ea_io` (alignement, mappage) sont satisfaites.** Donc l'echec
n'est pas la — le corps n'arrive pas jusqu'a cet appel.

## Ce que le corps atteint

Les cinq appels traces (`[gcmdep]`) sont tous des `_sys_memset` (401) et un `sys_lwmutex_create` (400), tous
reussis. Rien d'autre.

La reference fait, avant `gcmMapEaIoAddress` :

    sys_rsx_device_map(...)
    sys_rsx_context_allocate(...)
    gcmMapEaIoAddress(ioAddress, 0, ioSize, false)

Les trois syscalls **existent** dans `runtime/src/lv2/rsx.cpp`. Mais le corps n'atteint aucune d'elles — ses
seuls appels visibles sont les `memset`.

## Ce que ca implique

Le corps part de `0x33000cc0` et n'atteint pas le chemin qui appelle ces syscalls. Le `0x80010004` est donc
produit **avant**, par une verification interne dont le resultat depend de `context = 0x00d72800`.

## Une hypothese coherente, non demontree

La reference attend `vm::pptr<CellGcmContextData>` — un **pointeur vers un pointeur**. Si notre runtime passe
le contexte comme une valeur au lieu d'un pointeur, le corps lit une adresse invalide et echoue.

`context = 0x00d72800` est l'adresse de la structure ; la reference voudrait que `0xd72800` **contienne** un
pointeur vers `CellGcmContextData`. Le wrapper charge `gpr[3] = ld32(r2 - 12164)` — une valeur lue dans le
TOC, pas un pointeur vers un pointeur.

**Je ne conclus pas** : il faut verifier ce que `0xd72800` contient a l'entree, et si la reference attend
bien le contexte a cet endroit.

## L'etat du run

`videoimp = 0` : mes probes de ce round cassent encore l'init video. Le probe `[gcmargs]` est sur le chemin
critique. Il faudra le retirer une fois la mesure prise.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 737 751 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |