# La pile reelle de l'appel nul

## Ce que la mesure donne

En instrumentant `ppu::call_indirect` avec un anneau de profondeur, la pile apparait :

    [vcall-stack] depth=4 lr=0x005d2c08 r3=0x0 r31=0x0
      frame[0] called 0x3300126c from lr 0x002da504
      frame[1] called 0x3000512c from lr 0x0021c270
      frame[2] called 0x3000512c from lr 0x005e9be0
      frame[3] called 0x3000562c from lr 0x005e9b2c

## Les fonctions nommees

| adresse | fonction | image |
|---|---|---|
| `0x3300126c` | `f_3300126c` | **libgcm_sys** |
| `0x3000512c` | `f_3000512c` | **liblv2** |
| `0x005e9b2c` | `f_005e9b10 + 0x1c` | `mc_059` |
| `0x002da504` | `f_002da36c + 0x198` | `mc_028` |

Le chemin travers： `mc_028` -> `mc_020` -> `mc_059` -> **liblv2** -> **libgcm_sys** -> retour a `mc_058`
avec `r3 = 0`.

## L'appelant reel, et l'import

    ctx.lr = 0x2da504; ppu::call_import(ctx, base, 118);

**Import 118 = `cellGcmSys::cellGcmGetTiledPitchSize`** (NID `0x055BD74D`).

Son implementation HLE existe et suit RPCS3 :

    HLE_FUNC(cellGcmSys, cellGcmGetTiledPitchSize) {
        const uint32_t size = a32(ctx, 0);
        for (size_t i = 0; i + 1 < kTiledPitchCount; i++) { ... }

## Ce que le code en fait

Le resultat est stocke a `sp+124`, puis — plus loin, a `L_2da534` :

    ctx.gpr[29] = 0x00d70000;
    ctx.gpr[3]  = ld32(gpr[29] + 10240);   // ld32(0x00d72800)
    ctx.lr = 0x2da54c; f_005d2b44(ctx, base);

`f_005d2b44` fait `r31 = r3`, et `r3` vaut **0**.

## Une troisieme erreur d'instrumentation, a noter

Mon probe du round 197 (`-DPS3BASE_LD_TRAP`) n'etait actif que dans `ppu_ops.o` et le fichier de stubs,
**pas dans les `mc_*.cpp`** : `ld32` est `inline` dans `ops.h`, donc le probe n'existait pas dans les unites
de traduction qui font les lectures.

Mon resultat « `0xd72800` n'est jamais lu » etait donc **sans valeur** — pas parce que l'adresse n'est pas lue,
mais parce que le probe n'etait pas compile la ou la lecture a lieu.

C'est la **troisieme** erreur de ce type dans ce projet, et la cause est toujours la meme : un instrument dont
je suppose qu'il couvre le chemin, sans verifier qu'il est present dans les TU concernees.

## La question qui reste

`cellGcmGetTiledPitchSize` rend-il bien la valeur attendue ? Le HLE existe ; reste a verifier que la valeur
stockee a `sp+124` est utilisee, et que `ld32(0xd72800)` a ete ecrit par le jeu avant cet appel.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 44,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |