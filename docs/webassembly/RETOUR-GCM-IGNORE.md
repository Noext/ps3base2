# Le jeu ignore le retour de l'initialisation GCM

## La chaine, du jeu jusqu'a l'echec

    f_002d8a50 (l'appelant reel, mc_028:19665)
      gpr[3] = 0x00d72800   <-- l'adresse de la structure
      gpr[4] = 0x00fa000    <-- sa taille (1 024 000 o)
      lr = 0x2da76c; f_005d27d0(ctx, base)

    f_005d27d0 (wrapper)
      lr = 0x5d27f8; call_import(119)   // _cellGcmInitBody
      gpr[3] = int32(gpr[3])            // tronque le code de retour
      lr = gpr[0]; return                // rend a l'appelant

## Ce que l'appelant fait ensuite

    ctx.lr = 0x2da76c; f_005d27d0(ctx, base);
    ctx.gpr[0] = ctx.gpr[0] | 0x0ull;
    ctx.gpr[3] = 0 + 2;                    // <-- ecrase r3, le code d'erreur est PERDU
    ctx.lr = 0x2da778; ppu::call_import(ctx, base, 125);

**Le code de retour n'est ni teste ni stocke.** Deux instructions apres l'appel, `gpr[3]` est reecrit avec la
valeur `2`.

## Ce que ca etablit

| fait | valeur |
|---|---|
| `_cellGcmInitBody` recoit `0x00d72800` | mesure du round 222 |
| il rend `0x80010004` | mesure du round 221 |
| le jeu verifie ce retour | **non** |
| consequence | la structure reste vide, le jeu continue |

## Ce que ca explique definitivement

| observation | explication |
|---|---|
| `0x00d72800` n'est jamais ecrit | l'initialisation a echoue |
| le jeu ne s'arrete pas la | **il n'a jamais regarde le retour** |
| le crash arrive 2 000 instructions plus loin | les effets de l'echec se propagent |

## Ce que ca signifie pour le portage

Ce n'est **pas** un bug du jeu : sur console, `_cellGcmInitBody` **reussit**, et le champ est rempli. Le jeu
n'a aucune raison de verifier un appel qui ne peut pas echouer dans les conditions normales.

C'est **notre runtime** qui fait echouer l'init, et le jeu n'est pas ecrit pour le detecter.

## Ce qui reste, et qui est desormais bien cadre

Pourquoi le corps LLE de `_cellGcmInitBody` rend `0x80010004`. Le round 224 a etabli qu'il n'y a aucune
constante d'erreur dans le corps et aucun `return` d'erreur : la valeur vient d'ailleurs, peut-etre d'un
`gpr[0]` positionne a `1` ou `0` et interprete par le wrapper.

Le corps positionne `gpr[0] = 1` a deux endroits (branches `0x33000e28` et `0x33000e4c`). Le wrapper tronque
`gpr[3]`, pas `gpr[0]`. La valeur `0x80010004` doit donc venir d'ailleurs encore.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |