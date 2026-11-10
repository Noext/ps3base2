# Piste 41 -- le dialogue d'autosave

Branche `explore/41`, worktree `/root/Dev/ps3base2-wt41`, VFS `vfs-41`, tag `41`.
Titre : Minecraft BLES01976. Build RelWithDebInfo clang++, `-j 8`, Xvfb `:32`
(derive par `tools/run_world.sh`, jamais `:0`).

**En une ligne : le dialogue "level autosave feature" est dessine par le titre,
pas par le systeme -- aucun des trois imports candidats n'est jamais appele. La
desynchronisation de la sequence venait d'un pilote a nombre d'appuis FIXE, qui
n'a jamais regarde ou il etait. Sequence de creation : 10/10.**

## 1. Antecedents (`grep -Rn` ... visant `/root/Dev/ps3base/refs/`)

`refs/` est un lien symbolique vers `/root/Dev/ps3base/refs`. Dans un worktree
`refs/` **n'existe pas** et `grep -r refs/` ne trouve rien : viser le chemin
absolu, et utiliser `-R`.

| Trouvaille | Source (path:line) | S'applique ici ? |
|---|---|---|
| Valeurs de bouton : `OK = 1`, `YES = 1`, `NO = 2`, `ESCAPE = 3`, `NONE = -1` | `refs/rpcs3/rpcs3/Emu/Cell/Modules/cellMsgDialog.h:54-61` | **oui** -- l'ancien code rendait 3 (ESCAPE) a un dialogue OK |
| Types : `BUTTON_TYPE_NONE = 0`, `YESNO = 1 << 4`, `OK = 2 << 4` | `refs/.../cellMsgDialog.h:26-52` | **oui** -- l'ancien test `((type >> 4) & 7) == 1` ne connait que YESNO |
| Dialogue OK-only : Cross -> OK, Circle **ignore** ; cancel-only : Circle -> ESCAPE, Cross ignore | `refs/.../Emu/RSX/Overlays/overlay_message_dialog.cpp:155-195` | **oui** -- regle de lecture manette implementee telle quelle |
| `cellMsgDialogAbort` ferme **sans** callback ; `Close` prend un delai en ms | `refs/.../cellMsgDialog.cpp:520-585` | **oui** |
| Le callback est poste via `sysutil_register_cb`, pas execute dans le handler | `refs/.../cellMsgDialog.cpp:196-201` | **oui** -- on poste dans `sysutil_post` |
| `errDialog` : `NONE = 0`, `ALWAYS = 1`, `NOREPEAT = 2` ; un echec de callback ouvre un dialogue OK bloquant | `refs/.../cellSaveData.cpp:408-453`, `cellSaveData.h:126-128` | **oui** -- ce chemin etait lu puis jete |
| L'indicateur d'autosave systeme depend de `statSet->indicator`, un **pointeur** | `refs/.../cellSaveData.cpp:1862` | non -- observe a 0x0 partout |
| Le texte "level autosave feature" est dans les donnees du titre | `disc/PS3_GAME/USRDIR/Common/Media/MediaPS3.arc`, offset 6451583 | **oui** -- prouve que la boite est une ressource du jeu |
| Un port PS3 de Minecraft dans `refs/` documentant l'autosave | **aucun match** | rien a emprunter |
| `show_autosave_autoload_hint` | `refs/.../system_config.h:384` | non -- overlay de l'emulateur, sans rapport |

## 2. Identification : qui affiche le dialogue ? (la question du brief)

`cellMsgDialogOpen2` logue **chaque** appel qu'il recoit : une absence de ligne
est donc une preuve, pas une supposition. Mesures sur les runs qui **affichent**
la boite (capture `docs/autosave-41/dialog-autosave.png`) :

| Candidat | Mesure | Verdict |
|---|---|---|
| `cellMsgDialogOpen2` | `grep 'msgdialog: open2'` -> **0 ligne** sur 10 runs | **muet** |
| `cellSaveData` + `errDialog` | `grep 'file callback result'` -> **0 ligne** ; `errDialog` vaut `0x0` | **muet** |
| `cellMsgDialogOpenErrorCode` | importe, jamais appele | **muet** |

**Le dialogue est dessine par le titre.** Ce n'est pas un dialogue systeme.
Consequence pour les autres pistes, et c'est la trouvaille la plus utile :
chercher la cause de cette boite dans nos `cellMsgDialog*` ou dans `errDialog`
est une impasse.

## 3. Ce qui a ete implemente, et ce que ca n'apporte PAS

`cellMsgDialogOpen2` passe d'une auto-reponse devinee a une machine a etats :
ouverture reelle, second ouvert refuse avec `CELL_SYSUTIL_ERROR_BUSY`
(0x8002b105), lecture de la manette **par le systeme**, callback avec la bonne
valeur de bouton, livraison sur le thread invite, plus `Close`/`Abort`/
`OpenErrorCode`. `errDialog` est honore dans `cellSaveData`.

**Ca ne corrige pas le dialogue de Minecraft et ca ne doit pas etre lu comme
tel :** le titre n'appelle aucun de ces imports (0 appel sur 10 runs). Le
correctif est juste en soi, il servira a d'autres titres, il n'a aucun effet sur
cette boite.

## 4. La desynchronisation : ce que j'ai cru, et ce que la mesure a dit

**Hypothese 1 (REFUTEE).** "La boite est un dialogue systeme qui auto-repond."
Refutee en section 2 : aucun import n'est appele.

**Hypothese 2 (REFUTEE, par mes propres mesures).** "Les menus agissent sur la
repetition de touche, donc il faut des taps de 30 ms." Le mecanisme de double pas
est reel et reproductible -- sur le formulaire, en relisant la surbrillance :

```
hold=0.10 : 65 -> 136      une ligne
hold=0.25 : 208 -> 449     deux lignes (393 saute)
```

...mais **la conclusion "30 ms" est fausse** : 25 taps de 30 ms consecutifs sur
le dialogue du boot n'ont rien fait, alors qu'un seul appui de 0.25 s a avance.
Sous forte charge le titre tombe a **0.6 fps** (intervalle median entre deux
`rsx: flip` : 1.65 s) alors que la manette est lue a 33 Hz : un appui plus court
qu'une image peut n'etre vu par aucun rendu.

**La cause reelle : la recette envoie un nombre FIXE d'appuis sans jamais
relire l'ecran.** Un appui perdu ou double (les deux arrivent) decale tout le
reste, et la validation finit sur le mauvais widget -- mesure : six `Down`
"prevus" ont ouvert **"More Options"** au lieu de creer le monde. Ce n'est pas le
dialogue qui avale les appuis ; c'est le pilote qui ne regarde pas ou il est.

`tools/drive_world.py` corrige cela : capturer, classer l'ecran, lire la
surbrillance du widget focalise, appuyer, **relire**, recommencer jusqu'a ce que
la cible soit confirmee. La duree d'appui de 0.2 s n'est qu'un reglage.

## 5. Preuve 10/10

`tools/proof10.sh` enchaine dix creations, **chacune dans son propre processus**
-- donc dix passages reellement neufs par le dialogue et la creation -- et juge
chaque essai sur le log de CET essai (`grep -c 'sys_vm_memory_map ->'` >= 1).

```
/root/Dev/ps3base2-wt41/tools/proof10.sh vfs-41 10 /tmp/proof41-final 41f 420
```

| iter | vm_map | system_dialogs | trames de dialogue du titre | verdict |
|---|---|---|---|---|
| 1 | 1 | 0 | 2 | PASS |
| 2 | 1 | 0 | 2 | PASS |
| 3 | 1 | 0 | 2 | PASS |
| 4 | 1 | 0 | 2 | PASS |
| 5 | 1 | 0 | 3 | PASS |
| 6 | 1 | 0 | 2 | PASS |
| 7 | 1 | 0 | 2 | PASS |
| 8 | 1 | 0 | 2 | PASS |
| 9 | 1 | 0 | 3 | PASS |
| 10 | 1 | 0 | 3 | PASS |

`PASS 10 / 10`. `system_dialogs = 0` sur les dix confirme la section 2 : aucun
dialogue systeme n'est jamais ouvert par ce titre. Le nombre de trames ou la
boite du titre est a l'ecran est compte frame par frame par le pilote.

Reproduit plusieurs fois (`/tmp/proof41`, `/tmp/proof41b`, `/tmp/proof41-final`),
toujours 10/10. Journal complet : `/tmp/proof41-final/summary.txt`.

Verifie aussi **apres fusion de `main`** dans la branche (le correctif de
`cellMsgDialogOpen2` n'a pas casse la sequence) : 3 essais, 3 PASS
(`/tmp/proof41-post/summary.txt`), et les 9 tests du projet passent
(`ctest` : `100% tests passed, 0 tests failed out of 9`).

### Deux bugs de mon propre harnais, trouves et corriges

Ils valent d'etre notes parce qu'ils produisent des echecs qui ressemblent a des
crashes de l'emulateur :

1. `kill -9 -- "-$runner"` tuait le groupe de processus **du script de preuve**,
   pas du sous-shell (un sous-shell non interactif n'a pas son propre groupe).
   Symptome : l'essai suivant meurt a t=59 s, exit 137, aucun crash dans le log.
   Corrige en tuant le runner par pid simple.
2. Deux preuves lancees sur le meme VFS derivent le **meme** display, et
   `run_world.sh` relance un Xvfb mort avec `pkill -9 -f "Xvfb $DISPLAY"` : la
   seconde preuve tue le serveur X de la premiere. Corrige par un garde
   "une preuve a la fois par VFS" dans `proof10.sh`.

### Ce que `vm_map >= 1` prouve, et ce qu'il ne prouve pas

Il prouve que la creation a ete engagee et que dialogue + formulaire ont ete
traverses. Il ne prouve **pas** que le monde est rendu : sous cette charge la
generation depasse le budget d'une iteration. Je ne presente donc **pas**
`iter-10.png` comme un monde (cette capture montre "Generating spawn area"). Les
captures de monde rendu de cette campagne viennent d'autres runs :
`docs/autosave-41/world-rendered-41a.png` (survie, coeurs + hotbar + terrain) et
`docs/world-on-main.png` sur `main`.

## 6. Feuille des fichiers

| Fichier | Role |
|---|---|
| `runtime/include/runtime/msgdialog.h` | etat du dialogue systeme, valeurs RPCS3 citees |
| `runtime/src/hle/msgdialog.cpp` | machine a etats : open/close/abort, lecture manette, callback |
| `runtime/src/hle/cellSysutil.cpp` | `cellMsgDialogOpen2`/`Close`/`Abort`/`OpenErrorCode` |
| `runtime/src/hle/cellPad.cpp` | `fill()` appelle `msgdialog::on_pad` sur les octets finaux |
| `runtime/src/hle/cellSaveData.cpp` | `errDialog` honore ; `Op` porte le champ |
| `tools/drive_screens.py` | classement d'ecran par plus proche reference ; lecture de la surbrillance |
| `tools/drive_world.py` | pilote en boucle fermee, juge sur le log |
| `tools/proof10.sh` | la preuve 10/10 |
| `tools/screens/*.png` | ecrans de reference, nommes a l'oeil |
| `tools/run_world.sh` | publie `gamepid` pour tuer par groupe de processus |

## 7. Forcages

**Aucun.** Aucune entree `--force`, aucun `PS3BASE_FORCE`. Seules variables
posees : `PS3BASE_ALLOW_UNFOCUSED_INPUT=1` (lire le clavier sur un Xvfb sans
gestionnaire de fenetres) et, sur certains runs de mesure, `PS3BASE_PAD_LOG=1`
pour verifier que les appuis arrivent.

## 8. Resultat

- **Dialogue identifie** : dessine par le titre ; les trois imports candidats
  sont muets, mesure a l'appui. Notre auto-reponse n'y est pour rien.
- **Cause de la desynchronisation identifiee** : sequence a nombre d'appuis fixe,
  sans relecture de l'ecran. Corrigee par un pilote en boucle fermee.
- **Deux hypotheses refutees et retractees** : "le dialogue vient de
  cellMsgDialogOpen2" et "il faut des taps de 30 ms".
- **Preuve** : 10 creations consecutives, **10/10**, reproduit trois fois,
  chaque essai avec `vm_map >= 1` dans son propre log et 0 dialogue systeme.
- **Capture** : `docs/autosave-41/dialog-autosave.png` (la boite du titre, celle
  du brief) et `docs/autosave-41/world-rendered-41a.png` (un monde rendu, autre
  run de la campagne). Pas de monde rendu issu du run 10/10 lui-meme.
