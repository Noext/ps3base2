# Piste 43 -- trouver l'appelant manquant de f_002eeae4

Status : **l'appelant est trouve et la condition est mesuree.** Le defaut de
la feuille (import HLE) est trouve et corrige. Le monde existant n'est
**pas encore** charge : le correctif a debloque le premier barrage et revele
un second, distinct, que je documente avec sa mesure.

## Antecedents (grep -Rn dans refs/)

`refs/` n'existe pas dans ce worktree ; les greps ont ete faits dans
`/root/Dev/ps3base/refs/`.

| Constat | Source (path:line) | S'applique ici ? |
|---|---|---|
| `sceNpManagerGetChatRestrictionFlag` doit ecrire **0** | `refs/rpcs3/rpcs3/Emu/Cell/Modules/sceNp.cpp:4271` (`*isRestricted = 0`), et `refs/jdc-ps3recomp/libs/network/sceNp.c:317`, `refs/dod3-recomp/ps3recomp/libs/network/sceNp.c:288` | **oui -- c'etait notre bug.** Notre HLE ecrivait 1 |
| `CellSaveDataDirList` = dirName[32] + listParam[8] + reserved[8] | `refs/rpcs3/.../cellSaveData.h:197-202` | oui, conforme |
| `f_002eeae4` / OPD `0xbbe7f8` | aucun match | code du jeu, pas du SDK |
| Trace d'arbre d'appels `PS3BASE_CALL_TRACE` | aucun match dans refs/ (mecanisme maison) | — |

## Hypothese et resultat

**Hypothese :** f_002eeae4 n'est joignable que par `call_indirect` via un OPD
calcule a l'execution ; la condition qui l'empeche est l'etat d'un automate
dans `f_002eee64`.

**Resultat : confirmee, et nommee.**

### Pourquoi le scan 32 bits ne trouvait rien

L'OPD n'est pas une constante du binaire : il est **calcule**.

```
f_002eee64 (mc_029.cpp:11370-11373)
    ctx.gpr[5] = 0 + 12320768;              // 0x00BC0000
    { ... a + -6152; ... }                  // 0x00BC0000 - 6152 = 0x00BBE7F8
    f_002e8ae8(ctx, base);                  // r5 = l'OPD de f_002eeae4
```

Verification arithmetique : `0x00BC0000 - 6152 = 0x00BBE7F8`, et la table OPD
de l'ELF a bien `0xBBE7F8 -> {code 0x002eeae4, toc 0x00bd7998}`. Le scan de
32 bits cherchait la valeur *finale* ; le code ne la contient jamais, il la
construit. C'est exactement pour cela que 0 hit ne prouvait pas l'absence
d'appelant.

### La condition, mesuree

`f_002eee64` est l'arbitre du chargement. Il lit `obj+2924` (l'etat) et
dispatche. **Seul l'etat 5** mene au bloc qui calcule l'OPD de f_002eeae4 :

```
    ppu::cr_set(ctx, 7, int64_t(int32_t(ctx.gpr[4])), 5);   // cmp state, 5
    if (ppu::bc_cond(ctx, 12, 30)) { goto L_2eeef4; }       // state == 5
```

Sonde `PS3BASE_LOAD_PROBE=1` sur f_002eee64 (200 appels observes, run 43) :

```
loadprobe: probes installed: f_002eee64 (arbiter), f_002e4120 (arm),
           f_002eeb78 (setter), f_002ea564 (state-5 writer)
loadprobe: #0 lr=0x002ee1a4 obj=0x00d0d4f8 state=0 cnt=0 lim=0 \
           thumb_size=0 thumb_ptr=0x00000000 [324]=0
...
loadprobe: #199 state=0 ...
```

**200 appels, 200 fois `state=0`. Jamais 5.** Donc f_002eeae4 n'est jamais
appele, `obj+168`/`+172` restent 0, et f_002e8ae8 prend sa branche (0,0) -
ce qui confirme et explique la mesure des pistes 22 et 32.

### L'ecrivain qui pourrait mettre 5, et qui n'est jamais appele

17 sites ecrivent `+2924`. Un seul peut y mettre **5** :

```
f_002ea564 (mc_029.cpp:5556-5557), bloc L_2ea500 :
    ctx.gpr[4] = 0 + 8;
    ppu::cr_set(ctx, 0, int64_t(int32_t(ctx.gpr[28])), 0);
    if (ppu::bc_cond(ctx, 12, 2)) { goto L_2ea514; }   // si gpr28 == 0 -> 8
    ctx.gpr[4] = 0 + 5;                                 // sinon 5
    ppu::st32(base, uint32_t(ctx.gpr[31] + 2924), uint32_t(ctx.gpr[4]));
```

Sonde sur f_002ea564, meme run :

```
grep -c 'ADVANCE' game.log  ->  0
```

**f_002ea564 n'est JAMAIS appele.** C'est la reponse a la mission : non
seulement f_002eeae4 n'est jamais atteint, mais l'ecrivain qui pourrait armer
l'etat 5 ne l'est pas non plus. La chaine est coupee un cran plus haut que
l'hypothese de depart.

### Les deux candidats de la piste 32, instrumentes

```
SETTER f_002eeb78 #0 lr=0x002f043c obj=0x00d0d4f8 arg=14 (state <- 14)
SETTER f_002eeb78 #1 lr=0x002f04bc obj=0x00d0d4f8 arg=0  (state <- 0)
SETTER f_002eeb78 #2 lr=0x002e906c obj=0x00d0d4f8 arg=8  (state <- 8)
SETTER f_002eeb78 #3 lr=0x002e90ec obj=0x00d0d4f8 arg=0  (state <- 0)

ARM f_002e4120 #0 lr=0x001042d0 r3=0x00d0d4d8
ARM f_002e4120 #1..#5 lr=0x00104488 r3=0x00d0d4d8
```

f_002eeb78 **est** appele (4 fois) mais n'ecrit que 14, 0, 8, 0 -- jamais 5.
f_002e4120 est appele (6 fois) mais avec r3=`0x00d0d4d8`, qui n'est pas
`0x00d0d4f8` (l'objet du chargement) : c'est un autre sous-objet. Les deux
candidats de la piste 32 sont donc **refutes comme declencheurs**, par mesure.

## Le defaut de la feuille, trouve et corrige

En pilotant reellement le jeu (presse une par une, capture entre chaque), le
panneau de detail s'est ouvert **sans crash** et l'appui sur **Load** a
affiche :

> Failed to create game -- You are not allowed to create this game session
> because one of your local players has Online disabled on their Sony
> Entertainment Network account due to chat restrictions. Uncheck the
> "Online Game" box in "More Options" to start an offline game.

Cause mesuree : notre HLE renvoyait **1** (restreint) la ou RPCS3 et les deux
ports voisins ecrivent **0**.

```diff
--- a/runtime/src/hle/np.cpp
+++ b/runtime/src/hle/np.cpp
 HLE_FUNC(sceNp, sceNpManagerGetChatRestrictionFlag) {
-    if (uint32_t p = a32(ctx, 0)) vm::write32(p, 1);
+    if (uint32_t p = a32(ctx, 0)) vm::write32(p, 0);
+    LOG_INFO("sceNp", "ManagerGetChatRestrictionFlag -> 0");
     ret(ctx, 0);
 }
```

Mesure apres correctif :

```
I 53.635142 sceNp: ManagerGetChatRestrictionFlag -> 0
I 171.786073 sceNp: ManagerGetChatRestrictionFlag -> 0
```

Le dialogue "chat restrictions" **disparait** et Load progresse. C'est un
vrai progres mesurable : le blocage change de nature.

## Ou j'en suis : le second barrage

Apres le correctif, Load donne un ecran **different** :

> Network Error -- Connection to the server was lost. Exiting to the main menu.

Le log au meme instant :

```
sceNp: Matching2GetServerIdListLocal ctx=0 out=0x00000000 num=0
sceNp: Matching2ContextStartAsync(0, 10000000)
W import: unimplemented sceNpCommerce2::sceNpCommerce2Init
W import: unimplemented sceNpCommerce2::sceNpCommerce2CreateCtx
W import: unimplemented sceNpCommerce2::sceNpCommerce2CreateSessionStart
```

Le titre, ayant obtenu "pas de restriction", tente la session **en ligne** et
echoue sur les imports `sceNpCommerce2` non implementes. L'etat 5 de
f_002eee64 n'est toujours pas atteint (`ADVANCE=0` sur tout le run).

## Ce que j'ai etabli, et ce qui reste

**Etabli par mesure :**
1. L'appelant manquant est `f_002eee64`, et il est joignable : il **tourne**
   200 fois par run. Il ne prend jamais la branche etat 5.
2. L'OPD `0xbbe7f8` est **calcule** (`0xbc0000-6152`), ce qui explique le scan
   a 0 hit ; ce n'etait pas une absence d'appelant.
3. L'unique ecrivain capable d'armer l'etat 5, `f_002ea564`, n'est **jamais
   appele** (0 appel).
4. Les deux candidats de la piste 32 sont **refutes par mesure** :
   f_002eeb78 n'ecrit que 0/8/14, f_002e4120 recoit un autre objet.
5. Un bug HLE reel et corrige : `GetChatRestrictionFlag` renvoyait 1.

**Reste a faire :** implementer `sceNpCommerce2` (au moins Init / CreateCtx /
CreateSessionStart) pour que la session en ligne demandee par le titre apres
le correctif aboutisse, ou determiner pourquoi le titre choisit la voie en
ligne alors que le mode hors-ligne devrait etre disponible. C'est le prochain
cran, et il est nomme.

## Forcages utilises

**Aucun `PS3BASE_FORCE`.** Le seul changement de comportement est le
correctif HLE ci-dessus, qui aligne notre reponse sur RPCS3 et sur les deux
ports voisins. Les autres variables ajoutees sont de la journalisation
(`PS3BASE_LOAD_PROBE`) qui ne modifie rien de ce que voit l'invite.

## Captures

![Panneau de detail ouvert, Load au focus -- le crash des pistes 22/32 ne se reproduit plus avec ce build](/root/Dev/ps3base2-wt43/docs/loading-43/01-detail-panel-open.png)

![Load refuse : "Online is disabled ... due to chat restrictions" (avant le correctif HLE)](/root/Dev/ps3base2-wt43/docs/loading-43/02-before-chatfix-online-blocked.png)

![Apres le correctif : le dialogue chat restrictions a disparu ; un autre barrage apparait](/root/Dev/ps3base2-wt43/docs/loading-43/03-after-chatfix-network-error.png)

![More Options : la case "Online Game" cochee, celle que le titre demandait de decocher](/root/Dev/ps3base2-wt43/docs/loading-43/04-more-options-online-game.png)

![Liste des 6 sauvegardes existantes](/root/Dev/ps3base2-wt43/docs/loading-43/05-world-list.png)

## Fichiers modifies

- `runtime/src/hle/np.cpp` -- `sceNpManagerGetChatRestrictionFlag` renvoie 0
  (etait 1), avec log. C'est le correctif de feuille.
- `runtime/src/hooks.cpp` -- quatre sondes d'observation activees par
  `PS3BASE_LOAD_PROBE=1` : f_002eee64 (arbitre), f_002e4120 (arm),
  f_002eeb78 (setter), f_002ea564 (ecrivain etat 5). Aucune ne modifie l'etat.
- `tools/drive43.sh` -- pilote pas-a-pas.
