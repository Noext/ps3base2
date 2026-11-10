# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| Le wasm fait 177 663 `usleep` | round 121 | oui |
| `f_002dc1e4` charge `ld8(base, r31+1804)` et boucle `syscall(141)` | `out/ppu/mc_028.cpp:21490-21502` | **oui — la boucle identifiée** |
| `r31 = 0x00be0000`, donc le drapeau est à **`0x00be070c`** | calcul | oui |
| **Ce drapeau vaut 0 à t=39, 79, 119, 159 s** | mesure | **oui — l'hypothèse est infirmée** |

# La boucle existe, mais ce n'est pas elle qui bloque

## Le code

    ctx.gpr[31] = 0 + 12451840;                      // 0x00be0000
    ctx.gpr[3] = ld8(base, r31 + 1804);              // drapeau à 0x00be070c
    if (bc_cond(...)) goto L_2dc21c;                 // sort si zéro
L_2dc204:
    ctx.gpr[3] = 0 + 141; syscall(ctx, base);        // usleep
    ctx.gpr[3] = ld8(base, r31 + 1804);
    if (bc_cond(...)) goto L_2dc204;                 // boucle si non nul

## La mesure

    [pollflag] t=39s  byte@0xbe070c=0
    [pollflag] t=79s  byte@0xbe070c=0
    [pollflag] t=119s byte@0xbe070c=0
    [pollflag] t=159s byte@0xbe070c=0

**Le drapeau est à zéro.** La condition de boucle est donc **fausse** : le guest **sort** de cette boucle
immédiatement.

## Ce que ça infirme

Mon hypothèse « le guest est bloqué dans cette boucle de sondage » est **fausse**. J'avais lu le `goto` et
supposé qu'il bouclait ; la mesure dit qu'il ne boucle pas.

Les 177 663 `usleep` viennent donc **d'ailleurs** — d'autres appels, dans d'autres threads.

## Ce que ça laisse

Le pc final `0x002dc20c` est bien **dans** cette fonction, mais c'est le point d'entrée du `syscall(141)`,
pas une preuve de boucle.

Le vrai blocage est ailleurs.

## La leçon

C'est la troisième fois dans cette campagne qu'une lecture de code me fait conclure à une boucle ou un
verrou que la mesure infirme (rounds 60, 66, 107). **Lire un `goto` ne prouve pas qu'il est pris.**

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |