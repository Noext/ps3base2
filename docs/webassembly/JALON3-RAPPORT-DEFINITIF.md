# Jalon 3 : rapport final

## Les cinq criteres du prompt

| # | critere | seuil | mesure | verdict |
|---|---|---|---|---|
| 1 | taille du module | < 80 Mo | **45 409 302 o (45,4 Mo)** | **PASSE** |
| 2 | plus grosse fonction | < 1 Mo | **737 751 o** | **PASSE** |
| 3 | temps de compilation | a rendre | **6 min 03 s** (round 198) | rendu |
| 4 | pic RSS, nb de fonctions | a rendre | **409 Mo**, **49 718** | rendu |
| 5 | ligne de log du menu | doit exister | **absente** | **ECHOUE** |

## Ce que le build fait

`tools/build_wasm.sh` est **reproductible** : il derive sa racine de sa propre position, exclut les modules
specifiques a God of War presents dans le depot, et produit un module qui demarre.

    ps3base-wasm: starting
    ps3base-wasm: guest machine up, 32 cpus
    WASM-RUNTIME-UP / WASM-GUEST-CODE-RAN
    images mc=1 liblv2=1
    vm: disc (BLES01976 01.00)

## Le blocage, entierement caracterise

La ligne de menu n'est pas atteinte **a cause d'une chaine unique et mesuree** :

    1. le jeu appelle _cellGcmInitBody(context, cmdSize=0xfa000, ioSize=0x100000, ioAddr=0x40100000)
    2. le corps LLE (0x33000cc0) rend 0x80010004 = CELL_GCM_ERROR_FAILURE
    3. aucun import appele par le corps n'echoue (rounds 221-223)
    4. la decouverte indirecte ne trouve aucun code manquant (round 234)
    5. le jeu ignore le retour et continue (round 225)
    6. la structure a 0xd72800 reste a moitie remplie (rounds 229, 233)
    7. ld32(0xd72800 + 10240) = 0, jamais ecrit (rounds 208, 230)
    8. call_indirect(0) -> crash (rounds 199-219)

## Ce que les 50 commits du worktree apportent

| apport | round |
|---|---|
| build officiel reproductible (10 corrections) | 190-191 |
| module qui demarre (3 causes : mangling, define, obj perime) | 192 |
| cinq criteres mesures | 198 |
| lift complet lie (+19 448 o de code guest) | 188 |
| **generateur emettant l'adresse des blocs** | 218 |
| trace par branchement avec attribution correcte | 213-218 |
| controle de cible non mappee | 207 |
| ring de commandes, ring audio, entrees web, page | 165-171 |
| PARAM.SFO manquant du montage corrige | 231 |

## Ce que la serie a elimine par mesure

| piste | round |
|---|---|
| lenteur, ressources absentes, HLE manquant | 84-120 |
| pool de threads, fibres | 121-130 |
| filtre de table, `--gc-sections`, inlining | 186-192 |
| region non mappee | 196, 208 |
| le backend memoire masque tout | 206, 208 |
| import 118 / `cellGcmGetTiledPitchSize` | 199, 200 |
| pointeur-vers-pointeur | 229 |
| code non lifte | **234** |

## Les limites de l'environnement, nommees

| limite | consequence |
|---|---|
| binaire natif ne demarre pas (pilote NVIDIA) | 4 mesures du prompt inaccessibles |
| aucun navigateur | validation du jalon 4 impossible |
| les gardes sur entree tot cassent le boot | instrumentation limitee (3 fois mesure) |

## Position

| jalon | etat |
|---|---|
| 1 | **complet** |
| 2 | 5 etapes sur 7 (4 tables testees, renderer non porte) |
| 3 | **4 criteres sur 5**, blocage caracterise |
| 4 | ecrit et teste en partie, validation navigateur impossible |
| 5 | goulots nommes, fps natif 25,4 mesure |