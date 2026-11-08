# Piste 42 -- f_0064c044 : le mot de code en +28, l'heure non atteinte, et `held` non mesurable

Branche `explore/42`, worktree `/root/Dev/ps3base2-wt42`, VFS `vfs-42`.
Build RelWithDebInfo clang++, `-j 8`. Suite de tests : **9/9**.

**Resultat en une phrase : le crash est bien une corruption de `node+28` par un
mot du `.text`, prouve par desassemblage et par un dump de registres ; l'heure
complete n'a PAS ete atteinte (maximum mesure : 927 s dans le monde) ; et
`held` n'a pas pu etre mesure.** Les trois sont dits separement, parce que deux
d'entre eux sont des non-resultats.

## 1. Antecedents (grep -Rn ... refs/)

| Trouvaille | Source (path:line) | S'applique ici ? |
|---|---|---|
| Un watchpoint de garde de page existe pour attraper les stores que `vm_write*` ne voit pas, en loggant le RIP fautif | `refs/jdc-ps3recomp/runtime/ppu/ppu_loader.cpp:1171` | **oui** -- meme forme que `PS3BASE_CODEWATCH` |
| `stwcx` est vulnerable a ABA ; un commit obsolete corrompt via un bloc reutilise | `refs/jdc-ps3recomp/runtime/ppu/ppu_loader.cpp:821` | **non** -- la reservation est mesuree exacte a 2 et 4 threads (piste 33) |
| Un garde de re-entrance dans le handler de faute est obligatoire | `refs/jdc-ps3recomp/runtime/ppu/ppu_loader.cpp:1179` | oui, note |
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

Deux defauts de cette instrumentation ont ete trouves **par la mesure**, et
corriges (`eaa261d`) :

1. `_sys_lwmutex_lock/unlock/trylock` (fichier `lv2/lwmutex.cpp`) ne poussaient
   rien : le commit precedent n'instrumentait que le HLE dans `lwmutex_hle.h`.
2. L'ordre comptait. Dans le HLE, `mark_held()`/`mark_free()` sont eux-memes
   des stores guest dans le mot de verrou ; un push emis *apres* `mark_held`
   faisait que ces stores se voyaient `held -`.

## 5. Mesures brutes

### `held` : NON MESURE, et voici exactement pourquoi

C'est le point que je ne veux pas laisser ambigu :

| Binaire | line-writes | `held -` | `held` non-vide |
|---|---|---|---|
| avant correctif d'ordre (run 42) | 532 | **532** | **0** |
| apres correctif (runs 42f, 42g) | **0** | 0 | 0 |

Sur le binaire d'avant correctif, 532 ecritures sur 532 rapportaient `held -`,
et l'une des deux causes possibles est mon propre bug d'ordre (corrige). Sur les
binaries d'apres correctif, le chemin de sauvegarde n'a **jamais** ete atteint
dans mes runs, donc l'instrumentation n'a jamais parle.

**Conclusion honnete : `held` n'a pas ete mesure.** Une instrumentation dont on
sait qu'elle n'a pas encore parle vaut mieux qu'une instrumentation dont on
croit qu'elle a parle.

### Duree de jeu reellement atteinte

| Run | Duree DANS le monde | Crash | Capture |
|---|---|---|---|
| 42 (budget 3600 s) | **927 s** | non (tue : voir section 6) | oui |
| 42f | 0 s (generation) | non (tue) | non |
| 42g | ~1 s (segfault en generation) | **oui, segfault** | non (voir ci-dessous) |

Le monde a ete cree et **joue** deux fois. Preuves :

* `docs/findings/evidence-42/world-in-survival-t2985.png` -- 10 coeurs, faim,
  hotbar, terrain, en Survie (run 42, t=2985) ;
* `docs/findings/evidence-42/world-player-moved-t3050.png` -- le joueur a bouge,
  HUD `Crafting / Inventory / R2 Mine` (run 42) ;
* `docs/findings/evidence-42/world-42g-entered.png` -- deuxieme entree
  independante, 22:27:18, 10 coeurs pleins, arbres, ocean, nuages.

**Ce n'est pas une heure, et je ne la revendique pas.**

### Le segfault du run 42g, et ce qu'il ne montre pas

42g a segfaute pendant la generation (`Segmentation fault` rapporte par
`run_world.sh`, 0 `crash: signal` dans le log applicatif). Un dump de 510 MB a
ete pris (`blackbox/memory_0.bin`, 22:27). Je l'ai analyse avec le filtre des
sessions precedentes :

* `node+0x1c` a `0x378c9dcc` : **quatre mots a zero** -- le noeud de la piste 33
  n'existe pas dans ce dump (heap different, ou dump pris a un autre moment) ;
* `0x378c9c80` (le mot surveille) : zero ;
* scan "mot de code dans le heap" apres le filtre complet (aligne, dans le
  `.text`, pas une entree de fonction, pas de l'UTF-16, pas a moins de 0x20000
  d'une entree) : **66 valeurs**. Ce sont les memes adresses que dans les sept
  dumps precedents -- donc de la donnee ordinaire, pas cette corruption.

Ce segfault n'est donc **pas** demontre etre `f_0064c044`. Je ne l'attribue pas.

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
ressembler le probleme a un defaut du jeu. C'est la cause que l'orchestrateur a
independamment reproduite et documentee (`docs/findings/turn-318-status.md`).

## 7. Lecon de methode : un detecteur de capture peut mentir

Mon premier detecteur classait "WORLD" sur "des pixels sombres en bas de
l'ecran" (la hotbar). Il a donne deux faux positifs :

1. l'ecran de **chargement**, dont la boite d'astuce grise est en bas ;
2. l'ecran **"You Died"**, rouge sombre, sans barre verte ni logo.

J'ai donc rapporte "dans le monde" a t=1635 s alors que la barre de generation
etait a 35 %. Le detecteur final teste la structure et est valide sur 8
references :

* barre de progression verte saturee, ou logo du titre bien visible -> `LOADING` ;
* rouge sombre dominant (>55 % de l'image) -> `DEAD` ;
* sinon, bordures de hotbar detectees -> `WORLD`.

**Un critere de capture qui ne teste pas la structure n'est pas une mesure.**

## 8. Resultat

* La corruption est **reelle et localisee** : `node+28`, un mot du `.text`
  (`0x0048f640`, mi-fonction), et `lwz r4,0x14(r4)` sur ce mot est ce qui
  produit la faute guest 0.
* L'ecrivain de ce mot **n'est toujours pas nomme**. Le watch est arme sur les
  lignes des noeuds que la marche atteint, et `held` enregistre l'etat de verrou
  a chaque ecriture ; le prochain run qui atteint le crash avec le watch arme
  et le binaire corrige le nommera.
* L'heure complete **n'a pas ete atteinte** : 927 s dans le monde est le
  maximum mesure, avec capture.
* `held` **n'a pas ete mesure**, et c'est ecrit comme tel.

Quatre recits sont deja morts sur la mesure ("la reservation est cassee",
"un mot de code a ete stocke", "deux threads se courent apres", "+28 est de
l'UTF-16"). Traiter toute histoire venant d'un dump post-mortem seul comme une
hypothese, pas comme un resultat.
