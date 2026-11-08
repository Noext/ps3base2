# Piste 32 — charger un monde existant

Status: **monde existant non charge**. Deux resultats mesures: (1) la source
memoire de `cellPngDecOpen` **fonctionne** et les vignettes de monde sont
decodees avec succes; (2) le crash n'est pas dans le decodage mais dans le
**transfert** du resultat, et j'ai la trace d'execution qui montre la branche
exacte. Un correctif d'ABI reel a ete trouve et applique (userdata 64 bits),
verifie par mesure, mais il ne suffit pas a lui seul.

## Antecedents (grep -Rn dans refs/)

| Constat | Source (path:line) | S'applique ici ? |
|---|---|---|
| `CellPngDecSrc` : `+20 streamPtr`, `+24 streamSize`, `srcSelect` 0=FILE 1=BUFFER | `refs/rpcs3/rpcs3/Emu/Cell/Modules/cellPngDec.h:130-140` et `:44-51` | **oui** — nos offsets sont corrects, verifies par le log |
| La source memoire lit `streamSize` octets et pose `cursor = 8` apres le header | `refs/rpcs3/rpcs3/Emu/Cell/Modules/cellPngDec.cpp:495-501` | oui, et notre implementation fait de meme |
| Un argument de pile (9e+) est un **dword** lu a `gpr[1]+0x30+8*(i-1)`, tronque en u32 par l'appelant | `refs/rpcs3/rpcs3/Emu/Cell/PPUThread.cpp:3251` + `Modules/sys_libc_.cpp:27` (`i < 8 ? gpr[3+i] : +*ctx->get_stack_arg(i)`) | **oui, et c'etait notre bug** — on lisait 32 bits au debut du dword, donc la moitie HAUTE (toujours 0 en big-endian) |
| `cellSaveDataUserAutoLoad` a 9 arguments, le 9e est `userdata` | `refs/rpcs3/rpcs3/Emu/Cell/Modules/cellSaveData.cpp:2477-2483` | oui |
| Aucun antecedent pour « vignette de monde copiee dans l'entree de liste » | aucun match | — |
| Aucun antecedent pour `f_002eeae4` / OPD `0xbbe7f8` (code du jeu, pas du SDK) | aucun match | — |

## Hypotheses et ce qui les refute

Le brief demandait d'implementer la source memoire **si** elle echouait. Elle
n'echoue pas. Mesure: trois `cellPngDecOpen` avec `srcSelect 1`, tous `rc 0`,
64x64, 16384 octets de RGBA. **Je n'ai donc pas reecrit `read_src_bytes`**, et je
retracte d'avance toute conclusion qui aurait fait du decodage la cause.

## Mesures brutes

### 1. Log de tous les `cellPngDecOpen` pendant le chargement

Ajoute dans `runtime/src/hle/cellImageDec.cpp`: `srcSelect` a chaque appel
(`read_src_bytes`), plus les octets du tampon, plus un log d'echec explicite
avec le code de retour sur **chaque** sortie de `cellPngDecOpen`.

Pendant le chargement (run 32d, t=69.68 s), trois vignettes de **monde**:

```
I 69.686108 pngdec: SRC: src=0xd000caf0 srcSelect 1 (0=file 1=buffer)
I 69.686155 pngdec: SRC buffer: ptr 0x33b14ac0 size 767 mapped 1
I 69.686161 pngdec: SRC buffer bytes: 89 50 4e 47 0d 0a 1a 0a
I 69.686189 cellPngDec: Open: handle 1 -> sub 104 64x64, 16384 bytes of RGBA, rc 0
I 69.686245 pngdec: DecodeData: h104 64x64 space 20 bpp 4 pitch 256 dest 0x34bfd310
I 69.696087 pngdec: SRC: src=0xd000caf0 srcSelect 1     -> sub 105 64x64 rc 0, dest 0x351d66c0
I 69.704178 pngdec: SRC: src=0xd000caf0 srcSelect 1     -> sub 106 64x64 rc 0, dest 0x34bfd310
E 69.826687 crash: signal 11 (122 ms apres le 3e decodage)
```

Reproduit a l'identique dans 32b, 32c, 32d, 32e: **4 runs, 3 decodages, rc 0,
meme crash**. C'est stable.

Les autres `Open` (UI, polices) sont en `srcSelect 0` (fichier) et n'ont jamais
echoue non plus.

### 2. La vignette est decodee dans un temporaire, jamais dans l'entree

Les trois destinations (`0x34bfd310`, `0x351d66c0`, `0x34bfd310`) sont des
tampons de travail: deux decodages successifs ecrivent au **meme** endroit
(`0x34bfd310` pour sub 104 puis sub 106). Ce n'est donc pas `entry+172`.

### 3. Trace: la branche (0,0) est prise, et le store est saute

Trace d'execution avec le hook du runtime (`PS3BASE_GAME_HOOKS=1`,
`GOWA_HOOK=2eeae4,1013b4,ff1d8,4a7a4`), run 32e:

```
I 119.788750 hook: -> f_001013b4 #0 lr 002e8be0 r3=314d0f70 r4=0 r5=0 ...
I 119.788897 hook: <- f_001013b4 #0 r3=0
I 119.790525 pngdec: srcSelect 1     (les 3 decodages)
I 119.808799 hook: -> f_000ff1d8 #0 lr 000df1d4 r3=314d0f70 ...
I 119.902719 hook:   -> f_0004a7a4 #0 lr 000ff318 r3=ce5c28 r4=cdfa50c r5=72ff6763
I 119.902767 hook:      r4@0cdfa50c: ??          <- non mappe
E 119.903324 crash: signal 11 ... fault guest 0xcdfa50c
```

Ce que cela etablit, precisement:

- `f_001013b4` **est** appele, une fois, avec **`r4=0, r5=0`** (ptr=0, size=0).
- Son `lr = 0x002e8be0` correspond exactement au `call_indirect` de
  `L_2e8bc0` dans `f_002e8ae8` (`out/ppu/mc_029.cpp:3717`) — **la branche
  codee en dur (0,0) que la piste 22 avait deduite, je la mesure maintenant.**
- Dans `f_001013b4` (`out/ppu/mc_009.cpp:17192-17204`), les deux tests
  `cr_set(0, gpr29, 0)` / `cr_set(0, gpr30, 0)` sont vrais, donc
  `goto L_10142c` saute les deux `st32` vers `+2512`/`+2516`. Rien n'est stocke.
- Plus tard `f_000ff1d8` (`mc_009.cpp:14977-14979`) relit `+2512`/`+2516`,
  y trouve de la donnee perimee (`0xcdfa50c`, `0x72ff6763`), et les passe a
  `f_0004a7a4` qui dereference -> SIGSEGV.
- `f_002eeae4` (OPD `0xbbe7f8`, l'ecrivain de `+168`/`+172` signale par la
  piste 22): **0 appel** dans la trace. La piste 22 a raison sur ce point.

### 4. Le crash arrive a l'OUVERTURE du panneau, pas sur "Load"

Correction au brief de campagne: les trois decodages et le crash se produisent
quand on ouvre le panneau de detail d'un monde (`cross` sur une entree), avant
tout appui sur **Load**. C'est une correction de localisation, mesuree.

### 5. Le nombre 3 n'est pas une boucle tronquee

L'orchestrateur demandait de trancher entre « seules les entrees visibles sont
decodees » et « la boucle s'arrete a la 3e ». J'ai fait la mesure demandee: la
liste **defile** (captures `list-scrolls-3-visible.png` et
`list-scrolls-to-bottom.png` montrent 7 entrees, dont 6 mondes + le tutoriel,
avec les deux fleches actives). En faisant defiler puis en ouvrant un monde,
on obtient **toujours 3 decodages**. Les 6 `THUMB` sont presents et non vides
(2517-2518 octets chacun, 6 md5 differents). Donc la boucle n'est pas tronquee
par une entree manquante; 3 correspond a autre chose (les 3 entrees rendues
dans le panneau). **Le nombre n'est pas la cause** — le crash ne depend pas de
la 4e entree.

## Le correctif trouve (reel, mesure, mais pas suffisant)

### Bug d'ABI: userdata lu dans la moitie haute du dword

Le titre stocke l'argument 9 avec `st64 sp+112, r30` (`out/ppu/mc_029.cpp:4201`).
C'est un **dword** dans l'aire de parametres. Sur POWER big-endian, la moitie
**basse** d'un `u64` est a `slot+4`. Notre code faisait `vm::read32(sp + 0x70)`,
donc lisait la moitie **haute** — toujours 0 pour une petite valeur.

RPCS3 n'a pas ce bug parce que `get_stack_arg` rend un `be_t<u64>*` et que
l'appelant tronque (`+*ctx->get_stack_arg(i)` puis cast u32), c'est-a-dire lit
le dword entier et garde les bits bas.

Correction dans `runtime/src/hle/cellSaveData.cpp` (les deux handlers,
`UserAutoLoad` et `UserListAutoLoad`):

```diff
-    const uint32_t userdata = vm::read32(sp + 0x70);
+    const uint64_t slot64 = vm::read64(sp + 0x70);
+    const uint32_t userdata = static_cast<uint32_t>(slot64 & 0xffffffffu);
+    LOG_INFO("savedata", "UserAutoLoad stack arg9: slot64 0x%llx hi 0x%08x lo 0x%08x -> userdata 0x%x",
+             (unsigned long long)slot64, vm::read32(sp + 0x70), vm::read32(sp + 0x74), userdata);
```

Mesure avant/apres, meme site, meme run-type:

```
run 32  (avant) : savedata: UserAutoLoad user 1 'BLES01976-CACHE' userdata=0x0 -> 0
run 32b (apres) : savedata: UserAutoLoad stack arg9: slot64 0x1 hi 0x00000000 lo 0x00000001 -> userdata 0x1
                  savedata: UserAutoLoad user 1 'BLES01976-CACHE' userdata=0x1 -> 0
```

`userdata` passe de 0 a **1**, et le `slot64` montre que la valeur est bien dans
la moitie basse (`lo 0x00000001`). Le `hi` restait 0 dans tous les cas, ce qui
explique pourquoi l'ancien code ne voyait jamais rien.

Note honnete: ce correctif est reel et verifie, mais **il ne debloque pas le
chargement a lui seul**. Le declencheur du crash est la branche (0,0) de
`f_002e8ae8`, qui depend de `entry+168` (ecrit par `f_002eeae4`, jamais appele),
pas de `userdata`.

## La feuille, et ce qui manque

La chaine est maintenant etablie de bout en bout par mesure:

```
f_002e8ae8 (mc_029.cpp:3652)
   lit entry+2892, entry+2900, entry+2912
   test entry+168 == 0  ->  OUI (jamais ecrit)
   -> L_2e8bc0 : call_indirect(cb, dst, 0, 0)        [mc_029.cpp:3709-3717]
        -> f_001013b4 (mc_009.cpp:17179)
             garde gpr29==0 / gpr30==0 -> saute st32(+2512), st32(+2516)
f_000ff1d8 (mc_009.cpp:14908)
   relit +2512/+2516 (perimes) -> 0xcdfa50c / 0x72ff6763
   -> f_0004a7a4 (mc_002.cpp:8790) : strcmp sur un pointeur non mappe
        -> SIGSEGV  (exe+0xcb2171 = ppu::ld8, ops.h:41)
```

Ce qui manque est l'appelant de `f_002eeae4`. Son OPD est `0xbbe7f8`
(`{code 0x2eeae4, toc 0xbd7998}`, verifie en lisant l'ELF), et il n'existe
**aucune** reference a cette valeur dans le binaire: ni un mot de 32 bits egal a
`0x00bbe7f8` (scan de tout l'ELF: 0 hit), ni un immediat calculant
`0xbb0000 + 0xe7f8` dans le code leve. Il n'est atteignable que par
`call_indirect` via un vtable rempli a l'execution. C'est coherent avec le fait
que la trace ne le voit jamais.

Deux candidats de vtable existent et sont instrumentables:
- `f_002eeb78` est appele juste avant `UserAutoLoad` dans `thread_autoload`
  (`mc_029.cpp:4190`, `4215`) — c'est le gestionnaire d'etat qui devrait
  enregistrer les callbacks.
- `f_002e4120` (`mc_029.cpp:31505`) appelle `f_002e8ae8` avec
  `r3 = 0x00d10000 - 11016 = 0x00d0d4f8`; c'est l'entree par laquelle la
  fonction est atteinte, et c'est la ou la condition devrait etre armee.

## Ce que je n'ai pas fait, et pourquoi

Je n'ai **pas** force `entry+168` ni patche le titre. Poser une taille/pointeur
de vignette inventes ferait dereferencer un tampon qui n'existe pas, ce qui
deplace le crash sans rien prouver. La preuve exigee (un terrain de sauvegarde a
l'ecran) n'est **pas** obtenue, et je ne la declare pas.

## Forcages utilises

**Aucun.** Tous les runs sont sans `PS3BASE_FORCE`. Les seules variables
ajoutees sont de la journalisation (`PS3BASE_SAVEDATA_TRACE`,
`PS3BASE_GAME_HOOKS`, `GOWA_HOOK`) qui ne change pas l'etat invité.

## Captures

![Liste des mondes, 6 sauvegardes visibles](/root/Dev/ps3base2-wt32/docs/loading-32/world-list-6-saves.png)

![La liste defile: 6 mondes + tutoriel, deux fleches actives](/root/Dev/ps3base2-wt32/docs/loading-32/list-scrolls-to-bottom.png)

![Panneau de detail, Load a le focus — le crash suit l'ouverture](/root/Dev/ps3base2-wt32/docs/loading-32/load-panel-focused.png)

## Fichiers modifies

- `runtime/src/hle/cellImageDec.cpp` — journalisation de `srcSelect`, des
  octets de tampon, et de **chaque** sortie d'erreur de `cellPngDecOpen` avec
  son code. Aucun changement de comportement.
- `runtime/src/hle/cellSaveData.cpp` — correctif d'ABI `userdata` (64 bits,
  moitie basse) sur les deux handlers, avec log avant/apres.
- `tools/drive32.sh` — pilote pas-a-pas (une touche + une capture).
