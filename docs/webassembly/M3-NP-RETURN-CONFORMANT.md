# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| `(4J) GetFriendsThreadProc` apparaît avec 600 appuis | round 116 | oui |
| `sceNp: ManagerGetNpId -> 0x8002aa0c (OFFLINE)` | `/tmp/big.log` | oui |
| RPCS3 retourne `not_an_error(SCE_NP_ERROR_OFFLINE)` | `refs/rpcs3/.../sceNp.cpp:4017-4035` | **oui — mais même valeur** |
| Notre retour est `0x8002aa0c` | `runtime/src/hle/np.cpp:578-580` | oui |

# Le retour sceNp est conforme

## La vérification

| | RPCS3 | nous |
|---|---|---|
| statut hors-ligne | `not_an_error(SCE_NP_ERROR_OFFLINE)` | `0x8002aa0c` |
| **valeur reçue par le guest** | **`0x8002aa0c`** | **`0x8002aa0c`** |

`not_an_error()` ne change pas la valeur : il indique seulement qu'RPCS3 ne la considère pas comme
bloquante dans ses propres logs. Le guest reçoit **la même valeur** des deux côtés.

**Notre implémentation est correcte.**

## Ce que ça élimine

La piste « `sceNp` bloque le parcours » est **éliminée** : notre retour est identique à la référence.

## Ce que le code nous dit

Le commentaire de `np.cpp:160-173` documente que :

1. **Offline est le défaut délibéré** ;
2. le natif atteint le monde **en offline** ;
3. forcer ONLINE fait mourir le jeu après la création du monde (« Connection to the server was lost »).

Donc le mode offline est **le bon**, et il n'empêche pas le parcours.

## Ce que ça laisse

`GetFriendsThreadProc` est **créé** — c'est déjà la preuve d'un avancement jusqu'à un écran de menu.
Son échec réseau est attendu et n'arrête pas le jeu.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |