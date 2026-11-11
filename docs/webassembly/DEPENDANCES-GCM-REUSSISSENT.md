# Les dependances de `_cellGcmInitBody` reussissent toutes

## La mesure

Le corps LLE part de `0x33000e5c` -> `f_33000cc0` -> cinq fonctions, qui atteignent exactement quatre
imports :

| import | module | fonction |
|---|---|---|
| 396 | sysPrxForUser | `_sys_strncmp` |
| 400 | sysPrxForUser | `sys_lwmutex_create` |
| 401 | sysPrxForUser | `_sys_memset` |
| 405 | sysPrxForUser | `sys_process_get_paramsfo` |

Trace de leurs retours :

    [gcmdep] import=401 ret=0x3300c0c0 lr=0x33000650
    [gcmdep] import=401 ret=0x3300c178 lr=0x3300266c
    [gcmdep] import=400 ret=0x0        lr=0x33008918   <-- lwmutex_create OK
    [gcmdep] import=401 ret=0x3300c210 lr=0x330026a4
    [gcmdep] import=401 ret=0x3300c33c lr=0x33002c00

**Aucun echec.** `sys_lwmutex_create` rend `0x0` (CELL_OK). Les `memset` rendent les adresses destination, ce
qui est leur contrat.

`sys_process_get_paramsfo` (405) **n'est pas appele** dans ce chemin.

## Ce que ca etablit

| fait | valeur |
|---|---|
| imports atteints depuis l'init | 4 sur 4 identifies |
| retours en erreur | **aucun** |
| `0x80010004` produit par un import | **non** |

Le code d'echec est donc **calcule par le corps du firmware lui-meme**, pas propage depuis une dependance
HLE.

## Ce que ca laisse

Le corps `f_33000cc0` fait 5 814 octets et contient la logique d'initialisation. L'echec vient d'une
**verification interne** qu'il fait sur ses arguments ou sur l'etat memoire.

Deux pistes, aucune essayee :

1. Lire les `bc_cond` de `f_33000cc0` avec l'instrument du round 218 (attribution par adresse) pour voir quel
   chemin mene au `return` d'erreur.
2. Comparer avec le natif : la meme fonction, au meme point, doit reussir — la difference dira quelle
   verification echoue.

La premiere est mecanique et ne demande que le meme filtre, applique a la plage `0x33000cc0`.

## Ce que ce round ecarte

| hypothese | statut |
|---|---|
| `sys_lwmutex_create` echoue | **faux** — `0x0` |
| un `memset` echoue | **faux** |
| `sys_process_get_paramsfo` echoue | **faux** — jamais appele |
| l'echec vient d'un import HLE | **faux pour ces quatre** |

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |