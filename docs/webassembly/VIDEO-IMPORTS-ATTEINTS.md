# L'initialisation video : ce qui est atteint et ce qui ne l'est pas

## La comparaison avec le natif

| | natif | wasm |
|---|---|---|
| `cellVideoOutGetDeviceInfo` (144) | appele | **appele**, `ret=0x0` |
| `cellVideoOutGetResolution` (166) | appele | **appele**, `ret=0x0` |
| `cellVideoOutConfigure` (142) | `res 2 format 0 aspect 1 pitch 5120` | **jamais atteint** |
| `cellGcmGetTiledPitchSize` (118) | contourne | `0x1400` (= 5120) |

Le wasm atteint les deux premieres et **saute** `Configure`. Le natif les fait dans l'ordre.

## Ce que j'ai verifie, et qui est correct

| verification | resultat |
|---|---|
| le buffer de sortie de `GetResolution` est mappe | **oui**, `out=0x6001f6f0 mapped=1` |
| `resolution_info` couvre les ids courants | oui, table complete de RPCS3 |
| `write_video_out_resolution` ecrit | oui, le buffer est mappe |
| `cellVideoOutConfigure` HLE existe | oui, avec les cas d'erreur |

## La branche qui decide

Entre les imports 166 et 138 :

    gpr[7] = ld16(sp + 128);       // largeur ecrite par GetResolution
    gpr[3] = ld16(sp + 130);       // hauteur
    st32(r30 + 26872, gpr[7]);
    st32(r30 + 26876, gpr[3]);
    cr_set(ctx, 0, int32(gpr[3]), 576);
    if (bc_cond(ctx, 4, 1)) goto L_2da4c0;

`bc_cond(4,1)` teste une **condition issue de `cr0`**, posee sur `gpr[3]` = la hauteur.

Si la hauteur vaut zero, la branche est prise et `Configure` est saute.

## Ce que je ne sais pas encore

La hauteur lue a `sp+130` : est-elle zero, ou `Write16` a-t-il echoue malgre `mapped=1` ?

C'est une mesure d'une ligne (`ld16(sp+130)` au moment de la branche), et c'est la prochaine etape. Je ne
conclus pas avant de l'avoir.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 44,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |