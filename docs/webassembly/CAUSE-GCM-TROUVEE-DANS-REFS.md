# La cause exacte de `0x80010004`, trouvee dans refs

## La reponse est dans la reference

`refs/rpcs3/rpcs3/Emu/Cell/Modules/cellGcmSys.cpp:466` :

    if (gcmMapEaIoAddress(ppu, ioAddress, 0, ioSize, false) != CELL_OK)
    {
        return CELL_GCM_ERROR_FAILURE;      // <-- 0x80010004
    }

**C'est `gcmMapEaIoAddress` qui produit ce code exact**, et notre runtime a cette fonction :
`runtime/src/lv2/rsx.cpp:88`, `gcm_map_ea_io`.

## Ses conditions d'echec

    if (!size || (ea & 0xFFFFF) || (io & 0xFFFFF) || (size & 0xFFFFF))
        return CELL_GCM_ERROR_FAILURE;
    if (!vm::range_mapped(ea, size)) return CELL_GCM_ERROR_FAILURE;

Deux causes possibles : un argument non aligne sur 1 Mo, ou une zone non mappee.

## Ce que j'ai mal lu au round 222

J'avais note `r3=0xd72800, r4=0xfa000` comme « l'adresse et sa taille ». **C'est faux.** Le wrapper
`f_005d27d0` **reordonne** les arguments :

    gpr[9] = gpr[4]                        // sauve r4
    gpr[0] = gpr[3]                        // sauve r3
    gpr[3] = ld32(r2 - 12164)              // charge le CONTEXTE depuis le TOC
    gpr[4] = gpr[0]                        // cmdSize <- r3 d'origine
    gpr[5] = gpr[9]                        // ioSize  <- r4 d'origine
    call_import(119)

La signature de reference est `_cellGcmInitBody(context, cmdSize, ioSize, ioAddress)`. Donc :

| registre | sens reel | valeur d'origine du jeu |
|---|---|---|
| `r3` | **contexte** (charge du TOC) | pas `0xd72800` |
| `r4` | `cmdSize` | `0xfa000` |
| `r5` | `ioSize` | `0x100000` |
| `r6` | `ioAddress` | **non mesure** |

`0xfa000` n'est pas aligne sur 1 Mo — mais c'est `cmdSize`, pas l'adresse. **L'alignement n'est donc pas en
cause.**

## Ce qui reste a mesurer

`r6` = `ioAddress`. S'il n'est pas aligne, ou si la zone qu'il designe n'est pas mappee, `gcm_map_ea_io`
echoue et rend `0x80010004`.

C'est **une valeur a lire**, et le probe est ecrit ; il n'a pas encore tourne avec succes.

## L'etat casse

Mes probes de ce round cassent le run (`videoimp: 0`, `RuntimeError: unreachable`). Le prochain round doit
les retirer et ne garder qu'un probe sur l'import 119.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 737 751 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |