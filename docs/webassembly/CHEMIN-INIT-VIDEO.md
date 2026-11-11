# Le chemin du crash est l'initialisation video

## Les six imports de l'appelant

`f_002da36c_body` (`mc_028.cpp`) fait, dans l'ordre :

| `lr` | import | module |
|---|---|---|
| `0x2da3dc` | 144 `cellVideoOutGetDeviceInfo` | cellSysutil |
| `0x2da498` | 166 `cellVideoOutGetResolution` | cellSysutil |
| `0x2da504` | 138 `cellGcmGetLabelAddress` | cellGcmSys |
| `0x2da598` | 142 `cellVideoOutConfigure` | cellSysutil |
| `0x2da5d8` | 118 `cellGcmGetTiledPitchSize` | cellGcmSys |
| `0x2da5e4` | 124 `cellGcmSetFlipMode` | cellGcmSys |

Puis `ctx.lr = 0x2da54c; f_005d2b44(ctx, base);` — l'appel qui meurt.

## Ce que ca etablit

C'est une **initialisation video** : lire le mode d'affichage, obtenir la resolution, configurer la sortie
video, calculer le pitch de tuile, regler le mode de flip.

Le HLE de `cellVideoOutConfigure` existe, suit RPCS3 (`refs/.../cellVideoOut.cpp:185-236`) et gere les cas
d'erreur. Ce n'est donc pas un stub vide.

## L'objet nul

L'appel qui meurt (`f_005d2b44`) ne fait **aucun appel d'import** : c'est une methode sur un objet. Son `this`
vient de `ld32(0x00d72800)`, qui vaut zero.

Cet objet est donc une **structure d'etat video** que le jeu construit avant d'appeler la methode, et qui n'est
pas remplie.

## Ce qui reste inexplique, et que je ne comble pas

| question | statut |
|---|---|
| pourquoi `0x00d72800` vaut zero | inconnu — aucune ecriture PPU liftée ne le touche |
| qui devrait l'ecrire | inconnu — candidats : code non lifté, tache SPU, DMA |
| est-ce lie aux 114 allocations manquantes | **non demontre** — l'adresse differe de la ou le natif alloue ensuite |

## Ce que ce round ajoute

Un **nom de phase** : le jeu meurt en **initialisant la sortie video**, apres avoir interroge le mode
d'affichage et avant d'avoir configure quoi que ce soit de plus. C'est plus precis que « au demarrage », et
cela oriente la recherche vers l'etat video plutot que vers la memoire en general.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 44,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |