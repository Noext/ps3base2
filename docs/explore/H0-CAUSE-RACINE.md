# H0 — CAUSE RACINE : deux blocs de controle, aucun alimente par le guest

## Les trois adresses, mesurees

    NATIF          : rsx: context at 0x50100000 (driver 0x50200000 reports 0x50300000 queue 0x8d00)
    WASM rsx.cpp   : rsx: context at 0x00e00000 (hle init)
    WASM gcm_fifo  : gcmfifo: reader armed: context=0x01100000 control=0x01100040 ring=0x40100000

**Le natif alloue son contexte dans la region video `0x50000000`.** Notre HLE alloue en memoire
principale (`0x00e00000`), et `gcm_fifo` alloue **encore un autre bloc** (`0x01100000`).

## La consequence mecanique

Le guest n'ecrit `put`/`get` que dans **un seul** bloc. Le lecteur qui possede ce bloc (`gcm_fifo`)
reimplemente la semantique et ne connait pas le flip. Celui qui connait le flip (`rsx.cpp`) lit un
bloc que personne n'alimente : les logs montrent `methods 0.0% (0k/s)` sur tout le run.

Deux lecteurs, deux blocs, aucun ne suit le guest. C'est pour cela que les 40 en-tetes divergent des
le premier mot, et que les flips restent a zero.

## Le natif n'utilise pas notre HLE

`native-3000.log` : **0 ligne `gcmstub`, 0 ligne `imp119`**. Le natif execute le corps LLE de
`_cellGcmInitBody` et obtient `0x50100000`. Notre HLE n'est jamais sollicite chez lui.

## Retirer PS3BASE_HLE_OVER_LLE ne suffit pas (teste)

Sans le drapeau, le boot meurt plus tot :

    F 0.895693 ppu: unknown indirect target 0x00000000 (lr=0x005d2c08) r3=0x0

Le NULL vient de `f_005d2b44` (`out/ppu/mc_058.cpp`) qui fait `ld32(gpr[9] + 0)` puis
`call_indirect` : il appelle un callback que **personne n'a ecrit**. Retirer le drapeau deplace le
crash de 2 s a 0,89 s sans rien produire.
**Le prealable n'est pas de retirer le drapeau, c'est de remplir ce callback.**

## La correction, dans l'ordre

1. Le HLE de `_cellGcmInitBody` doit allouer son contexte dans la **region video `0x50000000`**,
   comme le natif, pour que `rsx.cpp` et le guest parlent du meme bloc ;
2. `gcm_fifo` doit lire ce bloc (via `rsx_context_base()` / `rsx_control_block()`, deja exposes)
   au lieu d'en allouer un second ;
3. le callback null de `f_005d2b44` doit etre fourni par le LLE de `libgcm_sys`, ce qui est le
   travail de _gcm_intr_thread.

## Correction d'un de mes instruments

Mon histogramme de methodes aliasait : index `(method>>2) & 2047`, puis affichage `index << 2`.
`0xE920` tombait dans le bucket `0x248` et s'affichait `0x0920` ; `0xFEAC` dans `0x7AB` et
s'affichait `0x1EAC`. Mon chiffre « distinct=137 » etait **137 buckets**, pas 137 methodes, et les
deux numeros recherches ne pouvaient pas s'afficher sous leur vrai nom.

La conclusion tient apres correction, mais la mesure etait fausse. Corrigee par la piste Z3.