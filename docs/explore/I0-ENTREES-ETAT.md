# I0 — Etat des entrees : le jeu n'interroge pas le pad

## Ce qui est mesure

    I 1.248282 sys_io: cellPadInit(4): one pad on port 0

**`cellPadInit` est appele. `cellPadRead` ne l'est jamais.** Le jeu n'interroge pas le pad,
donc aucun script d'entree — fichier ou integre — ne peut le piloter. C'est le bloquant de H1,
et il n'est pas dans le chemin d'entree : il est dans le fait que le jeu ne lit jamais.

Verification : `grep -ac pad` sur 148 s de run donne **1** ligne, celle de l'init. Aucune ligne
`pad: queued`, donc `load_command_queue()` n'est jamais atteint.

## Ce que l'instrumentation existante aurait du produire

Le dessin doit etre atteint des que le pad est lu. Or :

    pad: queued N built-in command(s)

n'apparait pas. **Lindependant de l'entree, le blocage est ailleurs.**

## Ou le jeu s'arrete

    t=148s pc=0x003a18b0 syscalls=12820 exited=0
    sleep pc=0x003a18b0 r3=50000 r11=141 lr=0x0021c18c

`r3=50000` = `usleep(50000)` = 50 ms. `lr=0x0021c18c`. Apres 10 s d'activite reelle
(`pngdec`, `fs: open ... menu ... clock.png`, `sceNp: ManagerGetNpId -> 0x8002aa0c OFFLINE`),
le thread principal dort en boucle dans une fonction non liftée depuis une table
(`0x003a18b0` n'est pas une entree de `table.cpp` : c'est dans une image liftee annexe).

`sceNp ManagerGetNpId` rend `0x8002aa0c` (OFFLINE, non connecte). Le jeu pourrait attendre une
reponse PSN qui n'arrivera jamais — a verifier, pas a supposer.

## Corrections faites au critere de menu (deux erreurs refutees)

### `thread_options_load` retire des corroborateurs

`runtime/src/lv2/ppu_thread.cpp` : le natif cree `thread_options_load` a **t=12,16 s**, soit
**42 s avant le menu**. Le compter parmi les threads d'interface satisfaisait `ui >= 2` sur un boot
qui n'avait jamais atteint le menu. Retire ; seuls `thread_getSavesInfo` et
`thread_readSaveCacheFile` corroborant maintenant, ceux que le natif cree **avec** le menu
(t=54,97 s).

### Le critere n'est plus evalue une seule fois

`wasm_entry.cpp` n'imprimait le verdict qu'a `i == 100`. Un boot lent pouvait donc afficher
« NOT reached » alors que le menu arrivait apres. Maintenant a `i == 100 || 300 || 900`.

## Pourquoi les threads du menu n'existent pas

    GetFriends: 0    readSaveCache: 0    getSavesInfo: 0

Le jeu n'ouvre pas le menu. Il n'y a rien a corriger dans le critere : la condition est fausse
parce que l'evenement n'a pas lieu.

## Ce que H1 doit trouver ensuite

1. pourquoi `cellPadRead` n'est jamais appele (est-il garde par un etat que le jeu n'atteint pas ?) ;
2. ce que le thread principal attend a `lr=0x0021c18c` — remonter au call site dans le code lifte ;
3. si `sceNp ManagerGetNpId` OFFLINE est ce qui bloque, ou si c'est autre chose.
---

# Correction : la fonction est cellPadGetData

J'avais ecrit `cellPadRead` : ce nom n'existe pas. La table d'imports contient :

    {"sys_io", 0x1CF98800u, "cellPadInit", 0},
    {"sys_io", 0x578E3C98u, "cellPadSetPortSetting", 0},
    {"sys_io", 0x8B72CDA1u, "cellPadGetData", 0},
    {"sys_io", 0xA703A51Du, "cellPadGetInfo2", 0},

Mesure sur 148 s de run : `cellPadGetData` = **0 occurrence**, `cellPadGetInfo` = 0,
`cellPadInit` = **1**. Le jeu initialise le pad et **ne le lit jamais**.

## Ce que cela signifie

Le blocage n'est pas dans le chemin d'entree : il est **avant**. Le jeu n'atteint pas l'ecran ou il
interrogerait le pad. Tant que `cellPadGetData` n'est pas appele, aucun script — fichier, integre ou
issu de la page — ne peut changer quoi que ce soit.

## Le thread principal au meme moment

    I 4.843092 status: ppu 0x1000000 'main_thread' running, last 0 (r3 0x42528210) syscalls +6

6 syscalls en 4,8 s : le thread tourne mais n'appelle presque rien. Les deux handlers SPURS sont
bloques en 130 et 178, comme en natif.

## La question a trancher

Le jeu a fait 10 s d'activite reelle (pngdec, fs, sceNp) puis s'est endormi a `pc=0x003a18b0`.
Il faut remonter du `lr=0x0021c18c` au call site dans le code lifte pour savoir **quelle condition**
il attend. C'est une fonction d'une image annexe (pas de `table.cpp`), donc la recherche doit couvrir
`out-mc/gen/reloc/*.elf` lifts, pas seulement `out/ppu/mc_*.cpp`.