# Le crash fibre de RDR : cause et correction

**Mesuré le 2026-11-13. Trois sous-agents indépendants sont arrivés à la même conclusion.**

## CORRECTION IMPORTANTE (Q2, 2026-11-13)

Une première version de ce document affirmait que `cellFiberPpuYield` n'était
**pas liftée**, parce que la table d'imports pointe `0x3600e930` et que le lift
semblait s'arrêter à `0x36005c80`. **C'était une erreur de catégorie.**

`0x3600e930` est dans **PT_LOAD 1** de `libfiber.elf` (`0x36006560..0x3600eb58`,
flags `0x600006`, **X=0**) : c'est un **`.opd`**, pas du code. Le descripteur y
lit `{code 0x360033dc, toc 0x36016a80}`, et `0x360033dc` **est** liftée
(`table.cpp:153490`, `{0x360033dc, f_360033dc}`).

Comparer une adresse `.opd` à une borne de texte est une erreur. Le lift de
libfiber est complet : 89 fonctions, et **0 des 962 cibles de branchement
directes** ne manque de label.

Et le `PERM` retourné 426 077 fois est **libfiber qui se comporte correctement** :
`0x360033ec` lit le pointeur par thread, `0x36003400` le compare à -1, et
`0x3600340c` retourne `PERM` quand le thread n'a pas de contexte fibre. Le titre
appelle `Yield` 426 077 fois **avant** de créer une fibre.

Le crash reste réel et la correction HLE reste juste — mais sa cause n'est pas un
lift incomplet.

---

## Le symptôme

RDR avançait jusqu'à son écran ALERT puis à l'écran WARNING, et mourait sur un
SIGSEGV hôte, sans timeout :

    crash: signal 11 ... fault 0x74ad28000010 (guest 0x10) thread main_thread 0x1000000
      #0 exe+0x17aa2b6   -> ppu::ld64, dans f_360002a0_body (libfiber_000.cpp:232)
      #3 ppu::call_indirect

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

## La correction

`cellFiber` est maintenant en **HLE** (`runtime/src/hle/cellFiber.cpp`, 547
lignes) avec de vraies fibres hôtes `ucontext` :

- une pile hôte par fibre ;
- une table de fibres par thread ;
- les piles **invitées** fournies par le titre ;
- une **chaîne de retour auto-référentielle**, pour que l'épilogue ci-dessus ne
  puisse pas lire zéro ;
- `JoinFiber`.

`GOWA_FIBER_HLE=0` restaure le LLE pour tout A/B. **Défaut : ON.**

## L'effet, mesuré

| | LLE (avant) | HLE (après) |
|---|---|---|
| exit | 139 | **134** |
| SIGSEGV guest 0x10 | **oui** | **aucun** |
| flips | 781 | **1201** |

3 530 à 10 407 bascules de fibres réelles dans ces mêmes minutes, 700+ fibres
créées, joins rendant un statut.

## Ce qui reste

Le titre s'arrête à **48-71 s sur `rdr+0xad5618`** :

    F ppu: unknown indirect target 0x00ae5818 -- rdr+0xad5618 (no lifted code) (lr=0x00adc030)

C'est une **table de sauts à `0x013e5c8c`, 17 entrées**, et `0xae5818` est
l'entrée 13. Les deux détecteurs la ratent. Prochaine frontière.
