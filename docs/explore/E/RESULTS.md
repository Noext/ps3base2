# Piste E — trace differentielle natif / WebAssembly

But : trouver LE PREMIER point ou le natif et le wasm divergent.

Etat : comparee, livrable principal trouve et mesure. Chaque mesure porte la commande exacte
et la sortie brute.

## Antecedents (grep refs/ et docs/webassembly/ avant toute hypothese)

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le natif « ne demarre pas » : exit 124 | `docs/webassembly/CAUSE-BLOCAGE-NATIF.md` | non — refute (Mesure 1) |
| Histogrammes natif vs wasm : 6 754 559 usleep natif contre 79 540 wasm | `docs/webassembly/M3-NATIVE-SLEEPS-MORE.md` | oui — mais /tmp/cmp.log n'existe plus |
| Le wasm ne bascule pas a draw 68k/s a t=500-600 s | `docs/webassembly/M3-SAME-PHASE.md` | oui — contexte |
| `trace::init()` pilote par `GOWA_EVTRACE` (categories sys, import, fs, rsx, tex, dma, trap, draw, atomic) | `runtime/src/trace.cpp:158-194` | oui — instrument commun aux deux binaires |
| Le dispatch d'import est `call_import()`, emet `T_IMPORT` (a0,a1,a2,ret,lr) | `runtime/src/ppu_runtime.cpp:547-562` | oui |
| `cellGcmGetLabelAddress(index) = fw.label_addr + 0x10*index` | `refs/rpcs3/rpcs3/Emu/Cell/Modules/cellGcmSys.cpp:159-163` | **oui — c'est le noeud du blocage** |
| `cellGcmGetLabelAddress` est un export LLE de libgcm_sys a l'OPD 0x3300bbbc | `out/ppu/table.cpp:24508` | oui |
| Le code LLE de cette fonction est `f_33003408` : `base = f_33000114(ctx); return base + (index<<4)` | desassemblage, Mesure 6 | **oui — la base vient du contexte GCM** |

## Mesure 1 — le binaire natif DEMARRE (contrairement a ce que disent les docs)

Commande exacte :

    cd /root/Dev/ps3base2 && DISPLAY=:0 PS3BASE_DISC_DIR=/root/Dev/mc-port/disc \
      PS3BASE_LOG=info timeout 45 ./build/ps3base --no-launcher > /tmp/E-native-try1.log 2>&1; echo EXIT=$?

Sortie brute (fin) :

    I  44.335341 [1838655] rsx: flip 901 (buffer 0) worst 34.9 ms, >50ms 0, >100ms 0
    I  44.994008 [1838614] gpu: window closed, exiting
    EXIT=124

`EXIT=124` est le `timeout` de 45 s, pas un plantage : le jeu tournait et dessinait (flip 901,
11 threads PPU crees). Les docs `CAUSE-BLOCAGE-NATIF.md` / `BUILD-NATIF-BLOQUE-173.md` sont perimees.

## Mesure 2 — la trace evenementielle native existe et est produite ici

    cd /root/Dev/ps3base2 && rm -f /tmp/E-native-trace.bin* && \
      env DISPLAY=:0 PS3BASE_DISC_DIR=/root/Dev/mc-port/disc PS3BASE_LOG=info \
          GOWA_EVTRACE=sys,import GOWA_EVTRACE_FILE=/tmp/E-native-trace.bin \
          timeout 150 ./build/ps3base --no-launcher > /tmp/E-native-trace.log 2>&1; echo EXIT=$?

Sortie brute :

    EXIT=124
    -rw-r--r-- 1 root root 114047992 Nov 12 08:42 /tmp/E-native-trace.bin
    -rw-r--r-- 1 root root      5993 Nov 12 08:40 /tmp/E-native-trace.bin.str

    $ python3 tools/trace.py --file /tmp/E-native-trace.bin info
    records 2036571, 0.375s - 149.902s
      thread   11
      sys      367211
      import   1669349
    threads:
      PPU main_thread                          1318343
      PPU MSSTimer                              701614
      PPU MSSAsync                                9119
      PPU _gcm_intr_t                             7345
      PPU (4J) OpenSt                               80
      PPU (4J) Chunk                                30
      PPU trophy_init                               20
      PPU thread_opti                               12
      PPU (4J) Tile u                                3
      PPU (4J) Basic                                 3
      PPU MinecraftSp                                2

## Mesure 3 — la comparaison doit normaliser, et voici ce qui ne se normalise pas

Deux differences sont de la CONFIGURATION, pas de l'execution, et ne doivent pas etre lues comme
divergences :

| element | natif | wasm | traite comment |
|---|---|---|---|
| pile du thread principal | `0xd0010000..0xd0020000` | `0x60010000..0x60020000` | normalisee en offset (voir Mesure 5) |
| ordre de chargement des PRX | sysmodule, gcm, audio, io, sre, fs, trophy | sysmodule, gcm, audio, io, sre, fs, trophy | **identique** (les deux logs, lignes 25-50 natif / 39-61 wasm) |
| adresses des images LLE | `libgcm_sys 0x33000000`, `libsre 0x32000000`, `libfs 0x34000000` | **identiques** | comparables directement |
| nombre de threads PPU | 11 jusqu'a t=7,4 s | 2 seulement : main_thread + MinecraftSpursHdlr0 | **vraie divergence** (Mesure 4) |

Correction de ma propre section precedente : le wasm CHARGE BIEN libgcm_sys/libsre/libfs en LLE, aux
memes adresses que le natif. La ligne `images mc=0 liblv2=1 of 9` compte les images au moment du log,
pas la liste complete. La sortie brute qui le prouve (wasm) :

    I   0.901946 [    2] loader: loaded libgcm_sys (out-mc/gen/reloc/libgcm_sys.elf) text 0x33000000-0x3300b730 toc 0x33014000 entry 0x3300bf3c
    I   0.902041 [    2] prx: load '/dev_flash/sys/external/libgcm_sys.sprx' -> 0x23000d00 (LLE, start 0x3300bf3c)
    I   0.902917 [    2] loader: loaded libsre (out-mc/gen/reloc/libsre.elf) text 0x32000000-0x3202e000 toc 0x32039ab0 entry 0x00000000
    I   0.903416 [    2] loader: loaded libfs (out-mc/gen/reloc/libfs.elf) text 0x34000000-0x3400fef0 toc 0x340194c0 entry 0x340103c0

## Mesure 4 — le wasm ne cree QUE DEUX threads PPU : c'est la divergence structurelle

Le natif cree 11 threads, le wasm 2. Sortie brute de la trace wasm (`trace.py info`) et du dump
`guest::status_dump()` dans /tmp/E-wasmtrace2.log:200-220 :

    records 1906480, 1.196s - 158.9s
    threads:
      PPU main_thread                          1906478
      PPU MinecraftSpursHdlr0                        2

    ps3base-wasm: --- guest thread dump at t=4s ---
    I   4.887512 [    1] stacks: 3 guest stacks tracked
    I   4.887917 [    1] status: ppu 0x1000000 'main_thread' blocked in 141 (r3 0x1e r31 0x6001f6e0) syscalls +23867 lr 0x2da598
    I   4.888037 [    1] status: ppu 0x1000001 'MinecraftSpursHdlr1' blocked in 130 ... syscalls +1 lr 0x0
    I   4.888083 [    1] status: ppu 0x1000002 'MinecraftSpursHdlr0' blocked in 178 ... syscalls +2 lr 0x0

Le wasm n'a **aucun** `_gcm_intr_thread` (le natif en a un, cree a t=0,402, qui execute 7 344 des
7 397 evenements dont le `lr` est dans le texte de libgcm_sys).

## Mesure 5 — LIVRABLE : le premier point de divergence est `_cellGcmInitBody`, a l'index 9850

Outil ecrit ici : `tools/difftrace.py` (nouveau). Il compare, thread par thread, les imports dans
l'ordre, apres trois normalisations declarees :

* fenetre de pile `0xd001xxxx` (natif) et `0x6001xxxx` (wasm) ramenee a une base commune ;
* retours volatils ignores (horloge, ids de handles alloues dans un autre ordre) — la liste est
  explicite dans le fichier, pas un joker ;
* `lr` conserve : les images sont aux memes adresses, donc `lr` designe le meme site d'appel.

Commande exacte :

    cd /root/Dev/explore-E/tools && python3 difftrace.py \
      --native /tmp/E-native-trace.bin --wasm /tmp/wasm-trace-dir/wasm-trace.bin --kind import

Sortie brute :

    native: 2036571 records, threads ['PPU (4J) Basic ', 'PPU (4J) Chunk ', 'PPU (4J) OpenSt', 'PPU (4J) Tile u', 'PPU MSSAsync', 'PPU MSSTimer', 'PPU MinecraftSp', 'PPU _gcm_intr_t', 'PPU main_thread', 'PPU thread_opti', 'PPU trophy_init']
    wasm  : 1906480 records, threads ['PPU MinecraftSpursHdlr0', 'PPU main_thread']
    thread 'main_thread': native tids [1], wasm tids [1]
    ========================================================================================================
    import / main_thread: FIRST DIVERGENCE at index 9850 (native 1151945 events, wasm 958037)
    ========================================================================================================
    9849   both   cellGcmSys::cellGcmInitDefaultFifoMode   (0x1, 0x0, 0xffffffff) -> 0x0  lr=0x002da754
    9850   NATIVE sysPrxForUser::_sys_memset              (0x3300c0c0, 0x0, 0x84) -> 0x3300c0c0  lr=0x33000650
    9850   WASM   cellGcmSys::_cellGcmInitBody             (0xd72800, 0xfa000, 0x100000) -> 0x0  lr=0x005d27f8

    the wasm event next appears in native at index 9865 (delta 15)
    the native event next appears in wasm at index None (delta None)

Lecture : sur les **9 849 premiers imports du thread principal**, les deux builds executent la meme
chose, arguments compris. Le premier ecart est `_cellGcmInitBody` : le natif l'execute en **LLE**
(le corps firmware de libgcm_sys, d'ou un `_sys_memset` a `lr=0x33000650`, a l'interieur du corps),
le wasm l'execute en **HLE** (notre stub de `runtime/src/hle/cellGcmSys.cpp:87`, qui imprime
`[gcmstub] context=0xd72800 ...`, absent du log natif).

Cause dans notre code, lit en clair : `runtime/src/ppu_runtime.cpp:193-202` force tout `cellGcmSys`
sur le HLE quand `PS3BASE_HLE_OVER_LLE` est defini, et `tools/build_wasm.sh:56` le definit :

    if (imp.name && *imp.name && std::strcmp(imp.module, "cellGcmSys") == 0) {
        if (hle::Fn fn = hle::find(imp.module, imp.name)) { slot.hle = fn; return kHle; }
    }

Le binaire natif n'a pas ce define (aucune occurrence de `PS3BASE_HLE_OVER_LLE` dans
`/root/Dev/ps3base2/build/CMakeCache.txt`, et `strings build/ps3base | grep -c gcmstub` = 0).
**C'est la seule difference de configuration entre les deux binaires sur ce chemin.**

## Mesure 6 — la consequence : `cellGcmGetLabelAddress` rend 0x1fe0 au lieu de 0x50300ff0

Le corps LLE (export a l'OPD `0x3300bbbc`, `table.cpp:24508`) desassemble avec capstone :

    0x33003408: stdu  r1, -0x90(r1)          <- f_33003408 = corps de cellGcmGetLabelAddress
    0x3300341c: bl    0x33000114             <- f_33000114 rend la BASE des labels
    0x33003428: rlwinm r29, r29, 4, 0x14, 0x1b   <- index << 4
    0x33003430: add   r3, r3, r29            <- base + 0x10*index

    0x33000114: bl    0x330000f0             <- f_330000f0 rend le contexte GCM
    0x33000124: lwz   r11, -0x8000(r2)       <- global de l'image
    0x3300012c: lwz   r9, 0x7c(r11)
    0x33000130: lwz   r3, 0x14(r9)           <- fw.label_addr
    0x33000134: add   r3, r3, r0             <- + lwz r0,0x30(ctx)

Ce sont exactement les champs que `_cellGcmInitBody` remplit. Notre stub HLE n'ecrit que
`+0/+4/+8/+12` du contexte (`cellGcmSys.cpp:104-133`) : il ne touche ni le global `+0x7c`, ni
`+0x14`, ni `+0x30`.

Comptage mesure sur les deux traces (index et valeur de retour de chaque appel) :

    NATIVE cellGcmGetLabelAddress: 19842 calls
       indices: {255: 1, 64: 1, 65: 19840}
       returns: {1345327088: 1, 1345324032: 1, 1345324048: 19840}
    WASM cellGcmGetLabelAddress: 948169 calls
       indices: {255: 948169}
       returns: {8160: 948169}

Decimal -> hex : natif 0x50300ff0 (index 255), 0x50300400 (index 64), 0x50300410 (index 65, appele
19 840 fois a 60 Hz a partir de t=7,8 s). Wasm : 0x1fe0 (8160) pour **les 948 169 appels**.

Difference de comptage : le natif appelle `f_002da36c` **une seule fois** sur label[255] (t=0,403)
puis n'y revient plus, tandis que le wasm y tourne 948 169 fois. Le wasm ne lit donc pas seulement une
mauvaise adresse : il attend sur une adresse que rien n'ecrit, alors que le natif, lui, sort de
l'attente au premier tour.

## Mesure 7 — l'hypothese « le wasm ne charge pas libgcm_sys » est REFUTEE

Preuve directe, en comptant les evenements dont le `lr` tombe dans le texte de libgcm_sys
`[0x33000000, 0x3300b730)` :

    NATIVE events with lr inside libgcm_sys text 0x33000000-0x3300b730: 7397 of 2036560
    WASM events with lr inside libgcm_sys text 0x33000000-0x3300b730: 0 of 1906478
    NATIVE distinct gcm call sites: 31
    WASM distinct gcm call sites: 0

Le module EST charge des deux cotes, mais cote wasm **aucun code de libgcm_sys n'est jamais
execute** — parce que chaque import `cellGcmSys::*` est detourne vers notre HLE avant d'atteindre
l'export LLE (Mesure 5). Les 7 397 evenements natifs viennent a 7 344 du thread `_gcm_intr_t`, que le
wasm ne cree pas (Mesure 4).

## Mesure 8 — le test causal : en desactivant PS3BASE_HLE_OVER_LLE, le boot meurt plus tot

J'ai recompile UN SEUL objet (`runtime/src/ppu_runtime.cpp`) sans `-DPS3BASE_HLE_OVER_LLE`, relie dans
`/tmp/out-E/game-nohle.js`, et lance 95 s :

    "== recompile ppu_runtime WITHOUT PS3BASE_HLE_OVER_LLE"
    LINK_OK
    -rwxr-xr-x 1 root root 45406568 Nov 12 10:17 /tmp/out-E/game-nohle.wasm

Sortie brute (fin) :

    I   0.859482 [    3] spu: group_start 0x4000100 'MinecraftCellSpursKernelGroup'
    [impl-entry] addr=0x33000e5c in_map=1
    [gcminit] code entry r3=0xd72800 r4=0xfa000
    [blk] seq=1 bo=12 bi=30 taken=1
    [blk] seq=2 bo=4 bi=18 taken=1
    [blk] seq=3 bo=4 bi=18 taken=0
    I   0.870832 [    2] rsx: local memory 0xf900000 bytes at 0x50000000
    [read-d72800] page_mapped=1 value=0x00000000
    W   0.872920 [    2] ppu: call target 0x00000000 is UNMAPPED (lr=0x005d2c08 r3=0x0 r31=0x0)
    F   0.873571 [    2] ppu: unknown indirect target 0x00000000 (lr=0x005d2c08, thread 0x1000000)
    Aborted()

Lecture : avec le corps LLE, le boot va plus loin sur un point (il annonce `rsx: local memory
0xf900000 bytes at 0x50000000`, ce que le stub HLE ne fait pas) et meurt plus tot sur un autre (le
contexte `0xd72800` reste a zero : `[read-d72800] value=0x00000000`, d'ou l'appel indirect nul a
`lr=0x005d2c08`). Autrement dit : **le seul fait de basculer en LLE ne suffit pas**, parce que la
structure que le corps LLE lit (`0xd72800`) n'est pas celle que le guest a initialisee — c'est le
meme point que la piste « chaine pointeur nul » avait approche.

Le wasm actuel (HLE) ne meurt pas, mais il n'ecrit pas non plus `fw.label_addr` : il rend 0x1fe0 et le
thread principal boucle 948 169 fois sur label[255] (Mesure 6).

## Mesure 9 — ce que le contexte GCM contient vraiment cote wasm (sonde directe)

Sonde ajoutee dans la boucle de `wasm_entry.cpp` (une ligne par seconde), sortie brute de
/tmp/E-probe3.log:185-186 :

    ps3base-wasm: label[255] given=0x1fe0 mapped=0 val=0x00000000 | real=0x50300ff0 mapped=0 val=0x00000000
    ps3base-wasm: labelchain g=0x3300c0c0 s=0x00000000 fw.label_addr=0x00000000 ctx+0x30=0x00000000 => base=0x00000000

Les trois maillons de la chaine que `f_33000114` parcourt (Mesure 6) :

| maillon | valeur wasm | ce qu'il faudrait |
|---|---|---|
| `g = lwz(0x3300c0c0)` (global de l'image) | `0x3300c0c0` (le global lui-meme, non dereference) | meme |
| `s = lwz(g + 0x7c)` | **0** | un pointeur vers la structure firmware |
| `fw.label_addr = lwz(s + 0x14)` | **0** | 0x50300000 |
| `ctx + 0x30` | **0** | 0 |

Deux consequences mesurees, pas deduites :

1. `0x1fe0` n'est **meme pas mappe** (`mapped=0`), et l'adresse native `0x50300ff0` ne l'est pas
   non plus cote wasm, parce que la region video n'a jamais ete placee : le log natif contient
   `vm: region 3 placed at 0x50000000`, le log wasm **aucune** ligne de ce type (0 occurrence).
2. Le contexte `0xd72800` que le guest remplit n'est pas la structure que le corps LLE lit : la
   chaine part d'un **global de l'image libgcm_sys** (`0x3300c0c0`), pas du contexte passe en r3.

## Mesure 10 — le wasm ne fait AUCUN appel systeme RSX, le natif en fait 19

Comptage par numero sur les deux traces (les numeros 668-677 sont la famille `sys_rsx_*`) :

    NATIVE total syscalls 367211
       sys_rsx_memory_allocate           1
       sys_rsx_context_allocate          1
       sys_rsx_device_map                1
       sys_rsx_attribute                 2
       sys_rsx_context_attribute        12
       sys_rsx_context_iomap             2
    WASM total syscalls 39895
       sys_rsx_memory_allocate           0
       sys_rsx_context_allocate          0
       sys_rsx_device_map                0
       sys_rsx_attribute                 0
       sys_rsx_context_attribute         0
       sys_rsx_context_iomap             0

Sortie brute des logs de demarrage :

    NATIVE: I   0.395997 [1853411] vm: region 3 placed at 0x50000000
    WASM:   (0 occurrence de "region 3 placed")

**C'est le mecanisme exact du blocage decrit dans le contexte** : sans
`sys_rsx_memory_allocate` -> `sys_rsx_context_allocate`, la region video n'existe pas, donc
`0x50300000` n'est pas mappe, donc `fw.label_addr` ne peut designer rien, donc
`cellGcmGetLabelAddress(255)` rend une valeur inutilisable et `f_002da36c` reboucle.

Note de methode : ces 19 appels sont dans le thread principal natif ; aucun n'apparait cote wasm.
Le HLE `_cellGcmInitBody` de `cellGcmSys.cpp:87` ne les fait pas — il ecrit quatre mots dans le
contexte et rend 0.


## Mesure 11 — seconde execution independante, meme premier ecart (reproductible)

Trace wasm complete refaite de zero (165 s), puis la meme comparaison :

    $ rm -f /tmp/wasm-trace-dir/* && cd /tmp/out-E && timeout -s KILL 165 node --experimental-wasm-exnref game.js > /tmp/E-wasm-final.log 2>&1
    $ ls -la /tmp/wasm-trace-dir/
    -rw-r--r-- 1 root root 104691344 Nov 12 10:32 wasm-trace.bin
    -rw-r--r-- 1 root root      2495 Nov 12 10:32 wasm-trace.bin.str

    $ cd tools && python3 difftrace.py --native /tmp/E-native-trace.bin --wasm /tmp/wasm-trace-dir/wasm-trace.bin --kind import
    9849   both   cellGcmSys::cellGcmInitDefaultFifoMode   (0x1, 0x0, 0xffffffff) -> 0x0  lr=0x002da754
    9850   NATIVE sysPrxForUser::_sys_memset              (0x3300c0c0, 0x0, 0x84) -> 0x3300c0c0  lr=0x33000650
    9850   WASM   cellGcmSys::_cellGcmInitBody             (0xd72800, 0xfa000, 0x100000) -> 0x0  lr=0x005d27f8

**Meme index 9850, memes deux evenements, deux executions independantes.** Le premier ecart est
deterministe, pas un artefact d'ordonnancement.

Cote syscalls, le premier ecart est a l'index 272 et il est la consequence visible du meme fait :
le natif continue dans libgcm_sys (`sys_95 lr=0x33008918`), le wasm dort 30 us sur la boucle de
label (`sys_141 (0x1e, ...) lr=0x002da598`).

    $ python3 difftrace.py --native /tmp/E-native-trace.bin --wasm /tmp/wasm-trace-dir/wasm-trace.bin --kind sys
    272    NATIVE sys_95   (0x51acf650, 0x2, 0x3300e490) -> 0x0  lr=0x33008918
    272    WASM   sys_141  (0x1e, 0x0, 0x2)              -> 0x0  lr=0x002da598

## Conclusion en une phrase

Le premier point de divergence entre le natif et le wasm est l'import numero 9850 du thread
principal, `cellGcmSys::_cellGcmInitBody` : le natif execute le corps firmware (LLE), le wasm notre
stub (HLE, force par `PS3BASE_HLE_OVER_LLE` dans `tools/build_wasm.sh:56`), et c'est de la que
decoule tout le reste — pas de `sys_rsx_memory_allocate`, pas de region video a `0x50000000`, pas
de `fw.label_addr`, `cellGcmGetLabelAddress(255) = 0x1fe0` non mappe, et la boucle de
`f_002da36c` sur label[255] qui n'a plus de producteur.

## Ce que la mesure dit de la suite

1. Le premier ecart n'est ni le GPU, ni le FIFO, ni le SPU : c'est **le corps de `_cellGcmInitBody`**,
   a l'index 9850 des imports du thread principal.
2. Le wasm n'a **jamais** execute une seule instruction de libgcm_sys (0 evenement sur 1 906 478),
   alors que le natif en execute 7 397, dont l'essentiel depuis un thread (`_gcm_intr_t`) que le wasm
   ne cree pas du tout.
3. `cellGcmGetLabelAddress` rend 0x1fe0 au lieu de 0x50300ff0 parce que `fw.label_addr` n'est ecrit
   par personne : le HLE ne connait pas ce champ, et le LLE n'est jamais atteint.
4. Ce que la suite doit fournir, cote HLE, pour rester dans la meme trajectoire que le natif :
   ecrire `fw.label_addr = 0x50300000` (les labels vivent dans la memoire locale RSX, et le runtime
   l'a bien allouee : `rsx: local memory 0xf900000 bytes at 0x50000000`), OU laisser le LLE remplir le
   contexte et n'utiliser le HLE que pour ce que le LLE ne peut pas faire sans RSX.

## Outils ajoutes par cette piste

* `tools/difftrace.py` — comparaison ordonnee, par thread, avec normalisation declaree de la pile et
  des retours volatils ; imprime l'index, les deux evenements et les sauts en arriere.
* `tools/trace_pre.js` — monte `/hosttmp` en NODEFS dans `onRuntimeInitialized` ; sans lui la trace
  d'un boot complet (114 Mo) irait en MEMFS et disparaitrait avec la page.
* Instrumentation wasm dans `runtime/src/trace.cpp` (constantes de compilation a la place de
  `getenv`, pas de thread de vidage) et `runtime/src/wasm_entry.cpp` (`trace::init` au demarrage,
  `trace::flush_all` a chaque seconde, `trace::shutdown` a la sortie), plus
  `runtime/src/host.cpp` qui rend un vrai nom de thread au lieu de `"wasm"` — sans quoi tous les
  threads arrivent sous le meme nom et la comparaison par thread est impossible.

## Reproduire

    # natif (150 s)
    cd /root/Dev/ps3base2 && env DISPLAY=:0 PS3BASE_DISC_DIR=/root/Dev/mc-port/disc PS3BASE_LOG=info \
      GOWA_EVTRACE=sys,import GOWA_EVTRACE_FILE=/tmp/E-native-trace.bin \
      timeout 150 ./build/ps3base --no-launcher > /tmp/E-native-trace.log 2>&1

    # wasm (158 s) — necessite le build de ce worktree et le pre-js
    mkdir -p /tmp/wasm-trace-dir && rm -f /tmp/wasm-trace-dir/*
    cd /tmp/out-E && timeout -s KILL 160 node --experimental-wasm-exnref game.js > /tmp/E-wasmtrace.log 2>&1

    # comparaison
    cd /root/Dev/explore-E/tools && python3 difftrace.py --native /tmp/E-native-trace.bin \
      --wasm /tmp/wasm-trace-dir/wasm-trace.bin --kind import

## Note — le marqueur « MAIN MENU REACHED » n'a pas ete utilise comme preuve ici

Le marqueur de `runtime/src/wasm_entry.cpp` exige `g_menu_thread_creations > 0` ET
`g_ui_thread_creations >= 2`, en comptant `thread_options_load` parmi les threads d'interface. Or le
natif cree `thread_options_load` a t=12,16 s, 42 s AVANT son menu, donc ce second critere ne
corrobore rien. Je ne m'appuie dessus nulle part : les seules preuves de cette piste sont les traces
(callback commun, index, arguments) et les deux captures de comparaison.

