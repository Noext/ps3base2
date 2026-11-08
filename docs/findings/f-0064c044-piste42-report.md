# Piste 42 -- f_0064c044 : le mot de code en +28, et jusqu'ou l'heure a tenu

Branche `explore/42`, worktree `/root/Dev/ps3base2-wt42`, VFS `vfs-42`.
Build RelWithDebInfo clang++, `-j 8`. Suite de tests : 9/9.

**Resultat en une phrase : le crash est bien une corruption de `node+28` par un
mot du `.text`, prouve par desassemblage et par le dump de registres ; mais
l'heure complete de jeu n'a PAS ete atteinte, et je ne la revendique pas.**
Le maximum mesure est 927 s dans le monde, captures a l'appui.

## 1. Antecedents (grep -Rn ... refs/)

| Trouvaille | Source (path:line) | S'applique ici ? |
|---|---|---|
| Un watchpoint de garde de page existe pour attraper les stores que `vm_write*` ne voit pas | `refs/jdc-ps3recomp/runtime/ppu/ppu_loader.cpp:1171` | **oui** -- meme forme que `PS3BASE_CODEWATCH` |
| `stwcx` est vulnerable a ABA et corrompt via des blocs reutilises | `refs/jdc-ps3recomp/runtime/ppu/ppu_loader.cpp:821` | **non** -- reservation mesuree exacte a 2 et 4 threads |
| Un garde de re-entrance est obligatoire dans le handler de faute | `refs/jdc-ps3recomp/runtime/ppu/ppu_loader.cpp:1179` | oui, note |
| `_sys_lwmutex_lock` prend son propre mutex autour d'un mot `signaled` ; la boucle de retry est chez l'APPELANT | `refs/rpcs3/rpcs3/Emu/Cell/lv2/sys_lwmutex.cpp:135` | **oui** -- c'est pourquoi deux lr dans un log ne sont pas deux ecrivains |

## 2. Ce que le crash est vraiment (mesure)

`f_0064c044` est atteint depuis `f_0064c2f8` -> `f_0064be6c`. Desassemblage de
`bin/mc/mc.elf` (segment 0 : vaddr 0x10000, offset fichier 0) :

```
0064be90  lwz    r4, 0x28(r31)      <- presence de liste, test NULL
0064beac  lwz    r29, 0x24(r31)
0064bef0  lwz    r3, 0(r3)          <- table[count]
0064bef4  addic  r6, r3, -0x20      <- node = head - 32
0064bf28  lwz    r28, 0x1c(r29)     <- NODE+28, LE champ
0064bf30  beq    0x64bf4c
0064bf34  lwz    r4, 0(r28)         <- chargement vtable
0064bf3c  lwz    r4, 0x14(r4)
0064bf40  lwz    r4, 0(r4)
0064bf44  mtctr  r4
0064bf48  bctrl                    <- appel indirect a travers
```

Registres live a la faute (`/tmp/run-world-33/game.log`) :

```
r29 = 0x378c9db0   <- le noeud
crash: object r6 (0x378c9db0): 00000028 378c9de0 00000028 00000028
                               00000028 00000008 0000000f 0048f640
```

donc `node+0x1c = 0x0048f640`, et la faute est guest 0x0 parce que
`lwz r4,0x14(r4)` part dans le `.text`.

**`0x0048f640` est une instruction, pas une vtable :**

```
0048f640  addic  r4,r4,0xb
0048f644  clrldi r4,r4,0x10
0048f648  sradi  r6,r4,0x2f
0048f64c  subfc  r5,r6,r5
```

Ce n'est **pas** une entree de fonction liftée (`grep f_0048f640 out/ppu/funcs.h`
-> 0 resultat ; l'entree la plus proche est `0x0048f628`, 24 octets avant).
C'est donc une adresse *a l'interieur* d'un corps de fonction : exactement la
signature "mot de code dans un champ pointeur".

## 3. Deux corrections, chacune par les octets

### Corrige : "deux threads de sauvegarde ecrivent le champ" (piste 33, retractee)

```
$ grep 'line-write 0x378c9c7c+4' /tmp/run-world-33/game.log | grep -oE '\[[0-9]+\]' | sort | uniq -c
     38 [1054653]   # (4J) Server
      5 [1059742]   # (4J) Save thread 0
```

38 des 43 ecritures sont UN seul thread hote. Les deux lr `0x005eb2c0` et
`0x005ea480` sont les instructions *apres* `bl 0x5e9b10`, qui est
`call_import(ctx, base, 326)` = `sysPrxForUser::sys_lwmutex_lock`. Une adresse
de retour d'acquisition de verrou est ce que `ctx.lr` contient a cet instant ;
ce n'est pas un site de store. La piste 33 est arrivee a la meme conclusion
independamment.

### Corrige : "le +28 n'est pas corrompu, c'est de l'UTF-16" (retractation 5 de la 33)

Cette retractation generalisait depuis le dump d'un *autre* run. Dans le run 9
le champ vaut `0x0048f640`, un mot d'instruction a une adresse mi-fonction, et
les voisins sont de la comptabilite de liste plausible (`0x28, 0x378c9de0,
0x28, 0x28, 0x28, 0x08, 0x0f`). La lecture d'origine du dump -- un champ de 4
octets d'un noeud ecrase par un mot du flux de code -- est **juste**. Une zone
UTF-16 peut aussi contenir une valeur en forme de mot, ce qui est pourquoi un
test de valeur a besoin du filtre "est-ce une entree de fonction legale" ; mais
un dump de registres n'est pas ambigu.

## 4. Outillage livre : syncwatch

`runtime/include/runtime/syncwatch.h` tient une pile par thread des objets LV2
que le thread guest possede. Pushes dans `sys_mutex_lock/trylock`, les
`_sys_lwmutex_*` (syscalls LV2 97/98/99 **et** le HLE `sysPrxForUser` que ce
titre utilise reellement), `sys_semaphore_wait/trywait`, `sys_rwlock_wlock` ;
pops dans les releases correspondants. Le `line-write` de codewatch se termine
maintenant par `held <liste>`.

Cela repond a la question que la session precedente ne pouvait que poser :
*le champ a-t-il ete ecrit pendant que le thread tenait son propre verrou ?*

Deux defauts de cette instrumentation ont ete trouves **par la mesure**, et
corriges :

1. `_sys_lwmutex_lock/unlock/trylock` (fichier `lv2/lwmutex.cpp`) ne poussaient
   rien : le commit precedent n'instrumentait que le HLE dans `lwmutex_hle.h`.
2. L'ordre comptait. Dans le HLE, `mark_held()`/`mark_free()` sont eux-memes
   des stores guest dans le mot de verrou ; un push emis *apres* `mark_held`
   faisait que ces stores se voyaient `held -`.

## 5. Mesures brutes

### Le champ `held` : non concluant, et je le dis

Sur le run 42 (binaire d'AVANT le correctif d'ordre), **532 line-writes sur 532
ont rapporte `held -`**. Deux explanations, et la seconde est mon propre bug :

* soit ce chemin n'appelle pas le lwmutex HLE ;
* soit `mark_held` ecrivait le mot de verrou avant le push (corrige par `eaa261d`).

Sur le run 42f (binaire d'APRES le correctif) : **0 line-writes**, donc le
chemin de sauvegarde n'a jamais ete atteint et `held` n'a pas pu etre re-mesure.
**Je ne fournis donc pas les deux chiffres demandes : la mesure n'a pas eu lieu.**
Le prochain run qui atteint la sauvegarde les donnera.

### Duree de jeu reellement atteinte

| Run | Duree dans le monde | Crash | Capture |
|---|---|---|---|
| 42 (budget 3600 s, tue a 3912 s) | **927 s** | non | oui, `evidence-42/world-in-survival-t2985.png` |
| 42f (budget 9000 s, tue a 700 s) | 0 s (generation) | non | non |

Le monde a ete cree et **joue** : `docs/findings/evidence-42/world-in-survival-t2985.png`
montre coeurs, barre de faim, hotbar et terrain en Survie ;
`world-player-moved-t3050.png` montre le joueur ayant bouge et le HUD
`Crafting / Inventory / R2 Mine`. **Ce n'est pas une heure.**

## 6. Cause racine des runs qui mouraient sans crash (utile aux autres pistes)

Plusieurs de mes runs se sont arretes a 60-700 s avec, dans le log,
`gpu: window closed, exiting` et **zero crash**. Ce n'etait pas l'emulateur :
le processus enfant est tue quand l'appel d'outil qui l'a lance se termine.

Matrice mesuree :

| Lancement | Survit ? |
|---|---|---|
| `setsid` + `nohup` + `disown` depuis l'appel | **non** (tues a 66 s, 321 s, 418 s) |
| `run_in_background` nu | non (60-150 s) |
| `nohup ... & disown` depuis l'appel, avec `tools/run_world.sh` | **oui** (700 s, et 3912 s plus tot) |

Les Xvfb survivent toujours ; seule l'instance du jeu meurt, ce qui fait
ressembler le probleme a un defaut du jeu.

## 7. Lecon de methode : un detecteur de capture peut mentir

Mon premier detecteur classait "WORLD" sur "des pixels sombres en bas de
l'ecran" (la hotbar). Il a donne deux faux positifs :

1. l'ecran de **chargement**, dont la boite d'astuce grise est en bas ;
2. l'ecran **"You Died"**, rouge sombre, sans barre verte ni logo.

J'ai donc rapporte "dans le monde" a t=1635 s alors que la barre de generation
etait a 35 %. Le detecteur final teste la structure et est valide sur 8
references (`docs/world-on-main.png`, `playability-31/world-survival.png`,
`mine-hole.png`, et les captures de chargement/mort) :

* barre de progression verte saturee, ou logo du titre bien visible -> `LOADING` ;
* rouge sombre dominant (>55 % de l'image) -> `DEAD` ;
* sinon, bordures de hotbar detectees -> `WORLD`.

**Un critere de capture qui ne teste pas la structure n'est pas une mesure.**

## 8. Resultat

* La corruption est **reelle et localisee** : `node+28`, un mot du `.text`
  (`0x0048f640`, mi-fonction), et la chaine `lwz r4,0x14(r4)` sur ce mot est ce
  qui produit la faute guest 0.
* L'ecrivain de ce mot **n'est toujours pas nomme**. Le watch est arme sur les
  lignes des noeuds que la marche atteint, et `held` enregistre l'etat de verrou
  a chaque ecriture ; le prochain run qui atteint le crash avec le watch arme
  le nommera.
* L'heure complete **n'a pas ete atteinte** : 927 s dans le monde est le
  maximum mesure, avec capture.

Quatre recits sont deja morts sur la mesure ("la reservation est cassee",
"un mot de code a ete stocke", "deux threads se courent apres", "+28 est de
l'UTF-16"). Traiter toute histoire venant d'un dump post-mortem seul comme une
hypothese, pas comme un resultat.
