# Z0 — La mesure decisive : le decodeur ne voit aucune commande de presentation

## Le fait, mesure

J'ai instrumente `execute()` dans runtime/src/gpu/gcm_fifo.cpp pour enregistrer CHAQUE numero de
methode decodee, avant tout classement. Resultat sur 120 s de run :

    [methods] distinct=137 top: 0x1f0c:1824 0x1f08:1824 0x1f04:1824 0x1f00:1824 0x1efc:1824
                               0x030c:1824 0x0304:1824 0x0a3c:1216 0x0a38:1216
    [methods] FLIP_COMMAND 0xFEAC seen: 0   FLIP_HEAD 0xE920: 0

**137 numeros de methodes distincts. Ni 0xFEAC ni 0xE920.**

## Un bug de numerotation corrige

Le decodeur faisait :

    case 0xE920:
    case 0xE924: g_flips.fetch_add(1); return;

Or `refs/rpcs3/rpcs3/Emu/RSX/gcm_enums.h:826-829` dit :

    GCM_FLIP_HEAD    = 0x0000E920 >> 2,   // 0xE920:0xE924, flip head 0 or 1
    GCM_FLIP_COMMAND = 0x0000FEAC >> 2,

et `rsx_methods.cpp:1773` : `bind(GCM_FLIP_COMMAND, flip_command);`.

**0xE920 est FLIP_HEAD (quel head est le prochain), 0xFEAC est le flip.** Les compter ensemble
entretenait la confusion. Corrige : `case 0xFEAC` compte les flips, `0xE920/0xE924` comptent les
heads, separes.

## Pourquoi ce n'est pas un probleme de compteur

Les deux numeros sont maintenant comptes separement et affiches : `flips=0 heads=0`. Le flux ne les
contient pas. Ce n'est donc pas une erreur de classification, c'est une absence.

## Ce que cela restreint

Le decodeur consomme 241 M de methodes sur 137 numeros distincts. Un flux de cette taille qui
n'emet jamais de commande de presentation signifie l'un de ces trois cas :

| hypothese | comment la trancher |
|---|---|
| il lit le mauvais ring | comparer put/get et la base du ring avec ce que le guest ecrit |
| il s'arrete avant les paquets de presentation | dumper les N dernieres commandes avant chaque arret |
| le guest n'emet pas ces paquets dans notre build | comparer au natif sur la meme fenetre |

## Correction du worktree natif

Le natif de reference est `/root/Dev/ps3base2` a `main 22d2a0c`. Les pistes Z doivent prendre CE
binaire comme oracle, pas un build wasm.

## Etat des pistes Z

| piste | etat |
|---|---|
| Z0 (orchestrateur) | **cette mesure** : 137 methodes, 0 flip, 0 head |
| Z1 | n'a rien produit (squelette, worktree mort avant mesure) |
| Z2 | 20 lignes |
| Z3 | n'a rien produit (squelette) |
| Z4 | **12 PASS / 0 FAIL sur HEAD**, test C repare |
| Z5 | 38 lignes, en cours |