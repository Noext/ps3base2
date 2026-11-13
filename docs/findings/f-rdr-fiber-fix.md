# Le crash fibre de RDR : cause et correction

**Mesuré le 2026-11-13. Trois sous-agents indépendants sont arrivés à la même conclusion.**

## Le symptôme

RDR avançait jusqu'à son écran ALERT puis à l'écran WARNING, et mourait sur un
SIGSEGV hôte, sans timeout :

    crash: signal 11 ... fault 0x74ad28000010 (guest 0x10) thread main_thread 0x1000000
      #0 exe+0x17aa2b6   -> ppu::ld64, dans f_360002a0_body (libfiber_000.cpp:232)
      #3 ppu::call_indirect

## La cause

`cellFiberPpuYield` est dans la table d'imports à `0x3600e930` :

    table.cpp:78072: {"cellFiber", 0x0C44F441u, "cellFiberPpuYield", 7, 0x3600e930}

Or **le lift de libfiber s'arrête à `0x36005c80`** — 89 fonctions, et la
fonction Yield n'en fait pas partie. Elle n'est pas liftée.

Conséquence mesurée : **426 077 appels à `cellFiberPpuYield` sur 426 077 ont
retourné `0x80760009` = `CELL_FIBER_ERROR_PERM`**. Le jeu ne vérifie pas ce
retour (`rdr_052.cpp:18552 f_0053338c`), donc il continue. Puis :

    cellFiberPpuCreateFiber(0x165d580, 0xd0033100, 0x1385418) -> 0
    ... 2 ms plus tard, SIGSEGV

## La mécanique exacte

Le thunk ELFv1 relit son propre back-chain :

    ld r2,0x28(r1)   -> 0     (le TOC qu'il avait stocké lui-même à 0x360002b4)
    ld r9,0(r1)      -> 0     (son propre back-chain stocké à 0x360002a8)
    ld r0,0x10(r9)   -> FAUTE à guest 0x10

`r1` au moment de la faute vaut `0xd0032d30`, soit 79 664 octets **sous** le
`r1_min` de main_thread (`0xd0046460`). La seule instruction de libfiber qui
charge `r1` est `ld r1,0(r3)` à `0x36000130` : la **restauration de contexte
fibre**.

La trace par ring buffer (512 événements, dumpée depuis le handler de signal)
prouve que la bascule est réelle :

    t=39.514803 ENTER  cellFiberPpuCreateFiber  r1=0xd0025140  lr=0x0053334c
    t=39.514902 LEAVE  cellFiberPpuCreateFiber  r1=0xd0025140  r3=0x0
    t=39.514903 ENTER  cellFiberPpuRunFibers    r1=0xd00252e0  lr=0x00533498   <- pas de LEAVE
    t=39.514955 ENTER  cellFiberPpuYield        r1=0xd0032d60  lr=0x0053339c
    ... SIGSEGV, r1=0xd0032d30, guest 0x10

`CreateFiber` a reçu `eaStack=0xd0025490, sizeStack=0xdc70`, donc les deux
valeurs de `r1` sont **dans la pile invitée de la fibre elle-même**, alors que
les 24 439 yields précédents de ce thread tournaient à `r1=0xd004f440`. La faute
se produit **à l'intérieur d'un `RunFibers` non terminé** (profondeur 2).

Et `*NO NAME*` (les mots `2a4e4f20 4e414d45` du rapport de crash) est le nom
par défaut d'une fibre : présent dans `libfiber.prx` en `.rodata` à
`0x36006478`, à côté de `fiber_ppu.c`. Absent de `EBOOT.elf`.

## La correction

`cellFiber` **ne peut pas rester en LLE** : le lift est incomplet.
`runtime/src/hle/cellFiber.cpp` (547 lignes) l'implémente avec de vraies fibres
hôtes `ucontext` :

- une pile hôte par fibre ;
- une table de fibres par thread ;
- les piles **invitées** fournies par le titre ;
- une **chaîne de retour auto-référentielle**, pour que l'épilogue ci-dessus ne
  puisse pas lire zéro ;
- `JoinFiber`.

`GOWA_FIBER_HLE=0` restaure le LLE pour tout A/B.

## L'effet, mesuré

| | LLE (avant) | HLE (après) |
|---|---|---|
| exit | 139 | **134** |
| SIGSEGV guest 0x10 | **oui** | **aucun** |
| flips | 781 | **1201** |

3 530 à 10 407 bascules de fibres réelles dans ces mêmes minutes, 700+ fibres
créées, joins rendant un statut.

## Ce qui reste

Le titre s'arrête maintenant à **48 s sur `rdr+0xad5618`** :

    F 40.172663 ppu: unknown indirect target 0x00ae5818 -- rdr+0xad5618 (no lifted code)

C'est un **trou de couverture du lifter**, pas un défaut de fibre. Prochaine
frontière.

## Une leçon de méthode

Le crash est **intermittent** : 4 runs LLE sur 8 aujourd'hui. Aucun taux de crash
n'est revendiqué ici. Et trois sous-agents ont trouvé la même cause par trois
chemins différents (addr2line, census TLS, ring buffer) — c'est ce qui en fait un
fait et non une hypothèse.
