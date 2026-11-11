# La chaine complete, du debut a l'appel nul

## Le chemin, mesure et lu

    f_002da36c  (mc_028, initialisation video)
      cellVideoOutGetDeviceInfo (144)   -> OK
      cellVideoOutGetResolution (166)   -> OK, 1280x720 ecrits
      ... branches 1..5 ... 
      branche 6 (bo=4 bi=1)             -> ATTEINTE, non prise (hauteur 720 > 576)
      cellGcmGetTiledPitchSize (118)    -> 0x1400 (5120)
      bc_cond_at(0x2da520, 12, 2)       -> L_2da534
      L_2da534:
        gpr[29] = 0x00d70000;
        gpr[3]  = ld32(gpr[29] + 10240);   // = 0  <-- L'OBJET NUL
        gpr[5]  = ld32(0x00be0000 + 1808);
        gpr[4]  = 255;
        f_005d2b44(ctx, base)              // -> call_indirect(0)

Puis, **jamais atteints** :

      cellGcmGetLabelAddress (138)
      cellVideoOutConfigure (142)
      cellGcmSetFlipMode (124)

## Ce que la trace a permis d'etablir

| question des rounds 211-217 | reponse |
|---|---|
| la branche 6 est-elle atteinte ? | **oui**, `n=39`, `taken=0` |
| pourquoi Configure n'est-il pas appele ? | le flux prend `L_2da534` **avant** lui |
| d'ou vient le pointeur nul ? | `ld32(0x00d72800)`, champ jamais ecrit |

## Le fait decisif

`L_2da534` est atteint par `bc_cond_at(0x2da520, 12, 2)` — une branche **posterieure** a la 6, donc le flux
passe bien la 6 puis continue, puis **bifurque vers le bloc qui lit le pointeur nul**.

Les imports 138, 142 et 124 sont **apres** ce bloc dans le source : ils ne sont donc jamais atteints, ce qui
explique exactement le `[videoimp]` du round 211 (2 sur 6).

## Ce que ca elimine

| hypothese | statut |
|---|---|
| la branche 6 saute Configure | **faux** — elle n'est pas prise |
| `GetResolution` echoue | **faux** — 1280x720 correct |
| `GetTiledPitchSize` echoue | **faux** — 0x1400 correct |
| le flux n'atteint pas la fonction | **faux** — 42 branches attribuees |

## Ce qui reste

**Un seul fait, precis** : `ld32(0x00d72800)` vaut zero, et aucun code lifté n'ecrit ce champ (verifie au round
208 sur les 100 fichiers).

Le champ est a l'offset 10240 d'une structure basee a `0x00d70000`. Cette structure est lue juste apres un
appel a `cellGcmGetTiledPitchSize` — donc c'est un **objet de configuration GCM** que le jeu devrait avoir
rempli, et qui ne l'est pas.

La prochaine mesure : chercher les **ecritures SPU** vers `0x00d72800`, puisque le PPU lifte n'en a aucune.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |