# Jouer a Minecraft PS3 (BLES01976) sur ps3base v2

Tout ce qui suit a ete mesure dans ce worktree (`explore/44`, tag 44), pas
recopie de memoire. Chaque affirmation non triviale porte sa mesure en §7.

## 1. Lancement

```bash
cd /root/Dev/ps3base2          # ou ton worktree
tools/play.sh                  # construit si besoin, ouvre une fenetre, joue
```

`tools/play.sh` est le point d'entree utilisateur. Il est distinct de
`tools/run_world.sh`, qui est l'outil de **mesure** (Xvfb dedie, boucle de
captures, verdict mecanique) : `play.sh` ne capture rien et ne rend pas de
verdict, il ouvre une fenetre et te laisse jouer.

### Options

| Option | Effet |
|---|---|
| `--vfs DIR` | repertoire VFS (defaut : `<tree>/vfs`). C'est la que vivent les mondes sauvegardes. |
| `--fullscreen` | demarre en plein ecran (`GOWA_FULLSCREEN=1`). En jeu : `F11` ou `Alt+Entree`. |
| `--no-build` | lance le binaire existant au lieu de construire. |
| `--log FICHIER` | ou va le log du jeu (defaut : `/tmp/play-ps3base.log`). |
| `--build-dir DIR` | repertoire de build (defaut : `<tree>/build`). |
| `--display :N` | display X a utiliser (defaut : la valeur de `DISPLAY`). |
| `--xvfb` | pas de serveur X ? en demarre un prive, uniquement pour presenter. |
| `--seconds N` | quitte apres N secondes (defaut : jusqu'a la fermeture de la fenetre). |
| `--scale K` | echelle de la fenetre, 1..4 (`GOWA_RES_SCALE`). |
| `--no-pad` | n'attache pas la manette virtuelle. |
| `-- ARGS...` | tout ce qui suit `--` est passe au titre. |

Codes de sortie : `0` sortie normale, `1` crash du jeu, `2` environnement
impossible a preparer (pas de display, pas de build), `3` aucune fenetre
a apparu dans les 30 s.

### Pas de DISPLAY : ce qui se passe

Le runtime presente **via SDL3 dans une fenetre X11**. Il n'y a pas de chemin
DRM/KMS dedans. Sans display, `play.sh` s'arrete avec un message explicite et
le code 2, il ne laisse pas SDL afficher "Couldn't connect to display" :

```
$ env -u DISPLAY tools/play.sh ; echo $?
play: no usable X display.
  ... (chemin de sortie explique, avec tools/play.sh --xvfb)
2
```

Sur une machine avec un vrai serveur X (ton bureau), c'est le chemin normal :
tu te connectes a ta session, `DISPLAY` est deja pose, et `play.sh` ouvre la
fenetre sur le GPU. C'est la configuration pour laquelle le script est ecrit.

## 2. Table des touches

Lue dans `runtime/src/host_input.cpp`, fonction `key()` (branche POSIX/SDL), et
**verifiee** par un run reel : voir §7.

| Touche | Bouton PS3 | Octet pad | Usage en jeu |
|---|---|---|---|
| `z` ou `Espace` | Cross | `b[3] & 0x40` | valider, sauter |
| `x` | Circle | `b[3] & 0x20` | annuler, quitter |
| `a` | Square | `b[3] & 0x80` | |
| `s` | Triangle | `b[3] & 0x10` | inventaire |
| `i` / `k` | stick gauche avant / arriere | `b[7] = 0` / `255` | marcher |
| `j` / `l` | stick gauche gauche / droite | `b[6] = 0` / `255` | marcher |
| `r` / `f` | stick droit haut / bas | `b[4] = 255` / `0` | viser verticalement |
| `t` / `g` | stick droit gauche / droite | `b[5] = 0` / `255` | viser horizontalement |
| `1` | L2 | `b[3] & 0x01` | poser un bloc |
| `3` | R2 | `b[3] & 0x02` | casser un bloc |
| `Q` | L1 | `b[3] & 0x04` | **slot de hotbar : precedent** |
| `E` | R1 | `b[3] & 0x08` | **slot de hotbar : suivant** |
| `Retour arriere` | Select | `b[2] & 0x01` | |
| `Entree` | Start | `b[2] & 0x08` | menu pause |
| fleches | D-pad | `b[2] & 0x10/0x20/0x40/0x80` | navigation de menu |
| `F11` / `Alt+Entree` | -- | -- | bascule plein ecran (gere par la fenetre, pas par le pad) |

Deux details qui se mesurent mal de memoire :

* `z` fait **aussi** Cross sur Espace (`key(SDL_SCANCODE_Z) || key(SDL_SCANCODE_SPACE)`).
* Les sticks ecrivent une valeur pleine (0 ou 255), pas une valeur analogique :
  au clavier on marche a vitesse maximale, il n'y a pas de demi-vitesse.

### La hotbar se change avec L1/R1 (Q/E), pas avec le D-pad

C'est la rectification de l'utilisateur, et elle est juste : sur PS3 le slot se
change avec L1/R1, et le mapping clavier les expose en `Q` et `E`.

La piste 21 avait conclu "le D-pad ne change pas le slot dans cette build".
Cette conclusion venait du mauvais bouton ; elle est retractee pour ce qui est
du choix du slot. Le D-pad reste correct pour naviguer dans les menus, et c'est
a cela qu'il sert partout dans les captures de ce document.

## 3. Creer un monde

Chaque appui doit durer : `xdotool keydown X; sleep 0.25; xdotool keyup X`.
Jamais `xdotool key X` (0 ms, ~90 % des appuis perdus, mesure piste 21).

1. **Termes** : `z`.
2. **Dialogue d'autosave** ("This game has a level autosave feature") : `z`.
   Il peut se repeter et ignorer les appuis ; voir §5.
3. Menu principal -> **Play Game** : `z`.
4. **Start Game** : `z`. Tu arrives sur la liste des mondes.
5. **Create New World** est la premiere entree de la liste : `z`.
6. Formulaire : le curseur demarre sur **World Name**. Descendre jusqu'a
   **Create New World** avec `Down`. Le nombre d'appuis varie selon l'etat des
   autres champs -- **verifie a l'ecran** que la ligne Create New World est
   surlignee (fond bleu) avant de valider. Piege mesure : un appui de trop
   rouvre le champ Seed, et l'appui suivant ne cree rien.
7. `z` sur **Create New World**, puis `z` sur `OK` si un avertissement
   apparait. La generation prend **~250 s** ("Generating spawn area").

Le monde demarre en **Survie** par defaut (`Game Mode: Survival`).

## 4. Jouer

* **Casser** : viser le sol avec `g` (stick droit vers le bas), puis maintenir
  `3` (R2) ~2 s. Le HUD affiche `R2 Mine`.
* **Ramasser** : le bloc casse tombe en objet ; marcher dessus avec `i` le
  ramasse et remplit le slot 1 de la hotbar.
* **Poser** : `1` (L2). Le HUD affiche `L2 Place` quand la main tient un bloc.
* **Changer de slot de hotbar** : `Q` (L1) ou `E` (R1).
* **Inventaire** : `s` (Triangle).
* **Viser** : `r`/`f` (vertical), `t`/`g` (horizontal). Sans ces touches la
  camera ne tourne pas et L2/R2 n'ont aucune cible -- c'est le piege qui a fait
  conclure a tort "casser ne marche pas" alors que les octets du pad etaient
  parfaits.

## 5. Limites connues

* **Presentation par Xvfb dans ce conteneur.** Le GPU fait bien la
  rasterisation (RTX 4060, `/dev/dri/renderD128`, driver 590.48.01) et le log le
  confirme : `gpu: Vulkan presenter on NVIDIA GeForce RTX 4060 Laptop GPU`.
  Mais il n'y a ici **ni Xorg ni xinit**, et aucun moyen de presenter sur
  DRM/KMS depuis ce conteneur : la presentation du swapchain est logicielle.
  En jeu : ~24-30 fps. `play.sh` affiche le device retenu pour que ce soit
  visible, et n'utilise Xvfb que si on le lui demande (`--xvfb`).
* **Pas de manette physique.** `/dev/input` et `/dev/uinput` n'existent pas ici.
  `play.sh` attache donc une **manette virtuelle SDL**
  (`SDL_AttachVirtualJoystick`) si aucune manette physique n'est presente ; le
  jeu la voit comme un vrai pad et le mapping est le meme code
  (`poll_pad` -> `SDL_GetGamepadButton` -> octets pad). Sur une machine avec une
  manette branchee, la virtuelle n'est pas attachee du tout.
* **Dialogue d'autosave qui boucle.** Il peut ignorer indefiniment les appuis.
  Signe de diagnostic : `grep -c 'sys_vm_memory_map ->' <log>` vaut 0 -- aucun
  monde n'a ete cree, donc rien en aval n'est teste.
* **Crash `f_0064c044`** apres plusieurs minutes, dans `(4J) Server`,
  `lr=0x64c0fc` : course entre deux threads de sauvegarde qui ecrivent le meme
  mot du meme noeud (piste 33). Observe sur ce worktree a `t=365 s` pendant la
  generation d'un monde.
* **Chargement d'un monde existant** : crashe encore (`-2` du callback fichier
  apres `METADATA`/`THUMB`, avant `GAMEDATA`). Passer par **Create New World**.

## 6. Ce que `play.sh` ne fait pas

* Il ne cree pas de monde a ta place : aucun pilote automatique, aucun
  `GOWA_AUTOPAD`. La creation reste manuelle, comme sur console.
* Il ne force rien : pas de `PS3BASE_FORCE`.
* Il ne capture pas d'ecran. Pour mesurer (captures + verdict mecanique),
  c'est `tools/run_world.sh`.

## 7. Verification par mesure (worktree 44)

Ce qui a ete reellement observe, avec la commande et la ligne de log.

### 7.1 `play.sh` lance le jeu en fenetre sur le GPU reel

```
$ env -u DISPLAY tools/play.sh --xvfb --no-build --seconds 2400
play: no X server found; started Xvfb on :73 (the GPU rasterises, presentation is software)
play: vfs    /root/Dev/ps3base2-wt44/vfs
play: GPU    NVIDIA GeForce RTX 4060 Laptop GPU, swapchain 1280x720
play: playing (pid 1141326, window 6291502). Close the window, or Ctrl-C here, to stop.
```

La fenetre est capturee avec
`import -window $(DISPLAY=:73 xdotool search --name Minecraft | tail -1)`. Voir
`docs/play-44/window.png`.

### 7.2 Le chemin SDL clavier arrive jusqu'a `cellPadGetData`

`PS3BASE_INPUT_LOG=1` journalise chaque evenement recu par le thread fenetre, avant
la conversion en octets. `PS3BASE_PAD_LOG=1` journalise les octets que le titre voit
dans `cellPadGetData`. Les deux ensemble montrent la chaine complete :

```
input: key down scancode 29 (Z) t=53.707        <- SDL_EVENT_KEY_DOWN -> host::note_key
pad: frame 1880 bytes 00 7c 00 40 80 80 80 80    <- b[3]=0x40 = Cross dans cellPadGetData
```

La chaine est : `SDL_EVENT_KEY_DOWN` -> `host::note_key(scancode, true)`
(`presenter.cpp`) -> latch de 5 trames -> `key()` (`host_input.cpp`) ->
`poll_pad` -> `fill()` -> `cellPadGetData` (`hle/cellPad.cpp`).

### 7.3 La manette virtuelle repond, par le meme chemin qu'une vraie

`PS3BASE_VPAD_SCRIPT` pilote la manette virtuelle **par l'API SDL** (pas par les
octets pad, pas par `fill()`) : chaque appui est un
`SDL_SetJoystickVirtualButton` suivi de `SDL_UpdateJoysticks`, et c'est
`SDL_GetGamepadButton` dans `poll_pad` qui le relit. C'est donc exactement le
chemin d'une manette physique.

```
input: virtual gamepad attached: ps3base virtual pad (id 3)
input: vpad button 0 down t=146.229              <- SDL_GAMEPAD_BUTTON_SOUTH
pad: frame 6830 bytes 00 7c 00 40 80 80 80 80    <- b[3]=0x40 = Cross
input: vpad axis 1 = -32767 (raw -32767) t=166.226   <- stick gauche en avant
pad: frame 7747 bytes 00 7c 00 03 80 80 80 01        <- b[7]=0x01 = stick gauche
```

Le jeu voit la manette : `input: gamepad connected: ps3base virtual pad`, et
`SDL_GetGamepads()` la renvoie comme n'importe quel pad.

### 7.4 Un bug trouve par la mesure, et corrige

La premiere version ecrivait `0` sur les axes de gachette pour dire "relache".
`SDL_SetJoystickVirtualAxis` ecrit des axes **joystick bruts**, et
`SDL_joystick.h` le dit explicitement : "a trigger at rest would have the value of
`SDL_JOYSTICK_AXIS_MIN`". Ecrire 0 laissait donc L2 et R2 a mi-course, `poll_pad`
les lisait appuyees (seuil `>30` sur 255), et les octets montraient
`b[3] = 0x03` (L2+R2 enfonces) **sans qu'aucune touche ne soit touchee**.

Corrige par une conversion explicite (0 -> `SDL_JOYSTICK_AXIS_MIN`, `v` ->
`v*2-32768`). Apres correction : `input: vpad axis 4 = 0 (raw -32768)` et plus
aucun `b[3] = 0x03` au repos. Sans `PS3BASE_PAD_LOG=1` ce bug ne se voyait pas.

### 7.5 La hotbar se change avec Q/E (L1/R1) -- confirme, D-pad non

Test dans un monde reel (Survie, HUD avec coeurs et hotbar), sur un display prive
`:73`. La selection est mesuree sur les pixels : le slot selectionne est le seul
entoure d'un liseré clair (colonnes x=274..357 pour le slot 1, pas 72 px).

| Envoi | Slot apres |
|---|---|
| depart | 3 |
| `E` (R1) appui de 20 ms | 4 |
| `E` (R1) appui de 20 ms | 5 |
| `E` (R1) appui de 20 ms | 6 |
| `E` x5 de plus | 7, 8, 1, 2, 3 (boucle 8 -> 1) |
| `Q` (L1) appui de 20 ms | 2 |
| `Q` (L1) x3 de plus | 1, 8, 7 |
| `Down` x3 (D-pad) | **inchange** |

Donc : **L1/R1 changent bien le slot**, d'un cran par appui, dans les deux sens,
avec bouclage 1..9 ; et **le D-pad ne le change pas** -- ce qui confirme la
rectification de l'utilisateur et retire la conclusion de la piste 21, qui avait
teste le mauvais bouton.

**Duree d'appui : ce n'est pas un detail.** La duree de maintien change le nombre
de crans, parce que le titre repete tant que R1 est tenu -- et l'auto-repeat X
n'y est pour rien (verifie : `xset -r`, meme comportement) :

| Maintien de `E` | Crans parcourus |
|---|---|
| 20 ms | **1** |
| 100 ms | 1 a 2 |
| 300 ms | 2 a 3 |
| 1000 ms | 3 a 5 |

A retenir pour un humain : un **appui bref** par slot. Pour un script, 20 ms est la
duree fiable. La duree depend du framerate (~10-25 fps ici), donc sur une machine
plus rapide un appui plus court suffit.

### 7.6 Ce qui n'a pas pu etre prouve ici

* **Plein ecran** : `--fullscreen` pose `GOWA_FULLSCREEN=1` et la fenetre est
  creee avec `SDL_WINDOW_FULLSCREEN` ; `F11`/`Alt+Entree` bascule en jeu
  (`presenter.cpp`). Sous Xvfb sans gestionnaire de fenetres il n'y a pas de
  vraie notion de plein ecran : le drapeau est pose, l'effet n'est pas
  observable ici. Sur un vrai serveur X il l'est.
* **Presentation DRM/KMS** : impossible depuis ce conteneur, voir §5.
* **Manette physique** : aucun `/dev/input`, voir §5.

## 8. Preuves sur disque (`docs/play-44/`)

| Fichier | Ce qu'il montre |
|---|---|
| `world.png` | le jeu en fenetre, monde charge (HUD, hotbar) apres creation depuis `play.sh` |
| `hotbar-l1r1-vs-dpad.png` | l'epreuve de la hotbar : 6 bandes successives (la premiere est l'etat de depart depuis `world.png`). Le liseré est sur le slot 1, passe a 3 puis 4 sous `E` (R1), revient a 3 puis 1 sous `Q` (L1), et ne bouge plus du slot 1 sous `Down` x3 (D-pad). |
| `form-create-new-world.png` | le formulaire avec `Create New World` surligne -- l'etat a verifier avant de valider |
| `world-generation.png` | l'ecran de generation (~250 s), pour reconnaitre l'attente normale d'un blocage |
| `window-real-display.png` | `play.sh` sur un display **deja present** (`DISPLAY=:91`) : aucun Xvfb lance par le script, fenetre sur le GPU. C'est le chemin d'un vrai bureau. |
| `evidence.txt` | extraits de log bruts, non retouches, pour chaque affirmation de §7 |
