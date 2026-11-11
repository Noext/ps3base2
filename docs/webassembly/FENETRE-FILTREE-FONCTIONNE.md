# La fenetre fonctionne, filtree a la sortie

## Le principe

Ouvrir la fenetre **a l'entree** d'une garde cassait le boot (round 214). La solution est de filtrer **a la
sortie** : la trace reste globale et le collecteur decide.

    extern "C" void ps3base_block_trace(uint32_t bo, uint32_t bi, bool taken) {
        extern uint32_t g_bt_lr;
        const bool kWant = g_bt_lr >= 0x002da36cu && g_bt_lr < 0x002dac00u;
        if (!kWant || n > 400) return;
        ...
    }

`g_bt_lr` est publie par le dispatcher a chaque appel : c'est le `lr` du frame courant, donc l'appelant.

**Aucune modification du chemin d'execution.** C'est ce qui distingue cette approche de la precedente.

## Le resultat

    blk=44
    [blk] bo=4 bi=30 taken=1
    [blk] bo=12 bi=26 taken=1
    [blk] bo=4 bi=28 taken=0
    [blk] bo=12 bi=29 taken=1

Quarante-quatre decisions de branchement, toutes prises pendant l'execution de la fonction d'initialisation
video.

## Les deux erreurs de ce round

| tentative | resultat |
|---|---|
| fenetre par garde d'entree sur `0x2da36c` | **casse le boot** — `hooks installed` disparait |
| `extern` declare deux fois dans le meme fichier | erreur de compilation, corrigee |

La premiere est instructive : `add_guard` **arme `hook_impl`** et pose le drapeau, donc le wrapper de la
fonction passe par `hook_call` au lieu d'executer son corps. Sur une fonction appelee tres tot, ce detour
change l'ordre et le boot echoue. C'est la troisieme fois qu'une garde modifie ce qu'elle observe.

## Ce que la trace va permettre

Comparer l'ordre des `bi` (bits de condition) pris ici avec ce que le natif fait au meme point. La ou les deux
divergent, la branche fautive est identifiee par son `bo`/`bi` et son rang.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 44,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |