# Piste 21 -- Jouabilite : GPU reel et entrees qui atteignent le jeu

Branche `explore/21`, worktree `/root/Dev/ps3base2-wt21`, VFS `vfs-21`.
Titre : Minecraft BLES01976. Build RelWithDebInfo clang++, `-j 8`.

## 1. Antecedents (grep -Rn ... refs/)

| Trouvaille | Source (path:line) | S'applique ici ? |
|---|---|---|
| Garde de focus clavier identique a la notre (`SDL_GetKeyboardFocus() != NULL`) | `refs/jdc-ps3recomp/libs/input/cellPad.c:289` | oui -- meme forme, donc meme limite ; mais inoffensive seule (voir §3) |
| Clavier+souris mappes sur le pad pour un jeu FPS | `refs/tokisaki-port/patches/ps3recomp/0018-cellpad-keyboard-and-mouse-first-person.patch:44` | oui -- layout, mais rien sur la DUREE d'appui ni sur le focus sans WM |
| Stick gauche/droit pilotes par le clavier | `refs/tokisaki-port/patches/ps3recomp/0032-pad-cod-layout-direct-mouse.patch:138` | oui -- confirme que le stick droit doit avoir des bindings |
| Message d'erreur "window in the background" si pas de focus | `refs/soulbrandt/port/ui/src/mouse_sdl.cpp:115` | non -- diagnostic UI, pas la cause |
| `SDL_AttachVirtualJoystick` / `SDL_JOYSTICK_VIRTUAL` | **aucun match dans refs/** | personne ne l'a fait |
| `XTestFakeKeyEvent` / `XSetInputFocus` | **aucun match dans refs/** | personne ne l'a fait |
| `cellPadGetData` (API) | `refs/jdc-ps3recomp/libs/input/cellPad.c:1439` | oui -- reference d'implementation |

Aucun port de `refs/` ne traite la **duree d'appui** comme cause de perte d'entree.
C'est notre bug, pas un probleme connu ailleurs.

## 2. Hypothese initiale -- REFUTEE

Hypothese transmise : `host_input.cpp:202`

```c
if (SDL_GetKeyboardFocus() != static_cast<SDL_Window*>(w)) return;
```

serait LA raison pour laquelle `xdotool key` n'arrive jamais a `cellPadGetData`.

**Refutee par la mesure.** Sonde SDL3 autonome sous Xvfb sans gestionnaire de
fenetres : `SDL_GetKeyboardFocus()` renvoie bien notre fenetre des la creation.

```
t=0.000 WINDOW FOCUS_GAINED
t=0.000 GetKeyboardFocus==our_window -> 1
```

Sur le vrai jeu, display isole, `PS3BASE_PAD_LOG=1` :

```
pad: frame 2537 bytes 00 7c 40 00 80 80 80 80 t=56.77   (b[2]=0x40 = D-pad Down)
```

La garde **passait** deja. Le focus n'etait pas le blocage.

## 3. Le vrai mecanisme -- mesure

`poll_pad` lit `SDL_GetKeyboardState`, un **instantane**, une fois par trame
(~60 Hz). `xdotool key` emet press ET release dans la **meme milliseconde** :
l'appui tombe entre deux polls et n'est jamais observe.

Mesure quantitative, meme run, meme fenetre focalisee :

| Envoi | Tramnes pad actives | Verdict |
|---|---|---|
| `keydown` maintenu 500 ms | 25 | 100 % fiable |
| 10 x `xdotool key Down` (0 ms) | **6** | ~90 % des appuis perdus |
| 10 x keydown/keyup a 120 ms | 69 | 100 % fiable |

C'est ce qui explique le symptome documente ("la selection derive, le meme menu
se re-ouvre") : une partie des appuis passe, le reste disparait.

**Apres le correctif**, meme test, meme fenetre : **57 trames** pour 10 appuis
de 0 ms (5.7 par appui, 10/10 vus).

## 4. La feuille trouvee

`runtime/src/host_input.cpp`, fonction `key()` : elle ne consultait que
`SDL_GetKeyboardState`. Correctif -- un **latch d'evenement** alimente par le
thread fenetre :

* `host::note_key(scancode, down)` (`runtime/include/runtime/host.h`) ;
* `presenter.cpp` : `SDL_EVENT_KEY_DOWN` / `SDL_EVENT_KEY_UP` -> `note_key` ;
* `host_input.cpp` : tableau atomique par scancode, `kLatchFrames = 5` ;
  `key()` consulte SDL puis le latch, qui se decremente a chaque poll.
  Un release n'etend jamais le latch, donc une touche maintenue reste lue
  depuis SDL.

Deux corrections associees dans le meme chemin :

* `PS3BASE_ALLOW_UNFOCUSED_INPUT=1` leve la garde de focus. Necessaire sur un
  serveur X sans WM : le focus d'entree est `PointerRoot`, donc il suit la
  souris et **toute seconde instance du meme display le vole**. Mesure sur un
  display partage : `getwindowfocus` pointait sur la fenetre d'une autre piste
  pendant que la notre tournait. Desactive par defaut, donc un desktop n'est pas
  affecte.
* `R/F/T/G` pilotent le **stick droit** (`b[4]`/`b[5]`). Il n'existait
  **aucun** binding clavier pour la visee : la camera ne pouvait pas tourner,
  donc L2/R2 n'avaient aucune cible meme quand les octets pad etaient corrects.
  C'est un piege qui fait conclure a tort que l'entree ne marche pas.

## 5. Diff

Commit `abd6f96` sur `explore/21` :

```
runtime/include/runtime/host.h |   5 +++++
runtime/src/gpu/presenter.cpp  |   6 ++++++
runtime/src/host_input.cpp     |  42 ++++++++++++++++++++++++++++++++++++++++--
3 files changed, 51 insertions(+), 2 deletions(-)
```

## 6. Preuves en captures (`docs/playability-21/`)

Toutes obtenues **au clavier seul** (xdotool), sans `PS3BASE_INPUT_CMD`, sans
`GOWA_AUTOPAD`, sans aucun forcage. Le monde a ete atteint depuis le menu
principal : Play Game -> Start Game -> Create New World -> Game Mode cycle
Survival -> Creative -> OK -> generation -> monde jouable.

| Preuve | Captures | Constat |
|---|---|---|
| (a) deplacement | `a-move-before.png` / `a-move-after.png` | md5 differents ; la vue avance dans l'herbe (touche `i` = stick gauche) |
| (b) casser un bloc | `b-break-before.png` / `b-break-after.png` | bloc d'herbe plein sous le reticule -> **cavite sombre** au meme endroit, terre exposee au fond (R2 = "Mine", affiche par le HUD) |
| (c) poser un bloc | `c-place-empty-hotbar.png`, `c-pickblock-grassblock.png` | **NON PROUVE** -- voir §7 |
| (d) inventaire ouvert | `d-inventory.png` | modele du joueur, 4 emplacements d'armure, grille 9x3, hotbar, bouton Exit (Triangle) |
| monde | `world-creative-keyboard.png` | HUD + hotbar + "Creative" + terrain, atteint au clavier |

## 7. (c) Poser un bloc -- resultat negatif mesure

**Non prouve.** Ce n'est pas un echec du chemin d'entree : les octets arrivent.

```
L2 : pad: frame bytes 00 7c 00 01 ...   (b[3]=0x01 = L2)
R2 : pad: frame bytes 00 7c 00 02 ...   (b[3]=0x02 = R2)
R3 : pad: frame bytes 00 7c 04 00 ...   (b[2]=0x04 = R3)
```

Le blocage est cote **etat de jeu** : la hotbar du monde Creative est vide, et
poser exige un item en main. Trois voies essayees, toutes mesurees :

1. **R2 mine** fonctionne (prouve en (b)) mais en Creative le bloc casse n'est
   **pas** ajoute a la hotbar -- hotbar toujours vide apres plusieurs R2.
2. **R3 (pick block)** fonctionne : le jeu affiche bien l'infobulle
   "Grass Block / Sometimes produces wheat seeds when broken" -- donc R3 est
   recu et interprete. Mais il n'ajoute rien a la hotbar dans cette build.
3. **Inventaire** : cette version PS3 n'expose **aucun onglet Creative**
   (pas de "Building Blocks", pas de barre de categories). L'inventaire est une
   seule page de style survie, avec un seul item (une carte). Le curseur s'y
   deplace bien (i/k/j/l pilotent le pointeur, mesure), mais il n'y a aucun bloc
   a prendre.

Conclusion honnete : **3 preuves sur 4**. (c) est bloque par l'absence de blocs
en Creative sur cette build, pas par l'emulateur ni par l'entree.

## 8. GPU reel sans Xvfb -- limite du conteneur

**Impossible ici, et je ne fabrique pas de capture.** Mesure :

* `command -v Xorg` -> vide ; `command -v xinit` -> vide. Aucun serveur X reel
  n'est installe.
* `Xvfb :0` est le seul serveur X disponible.
* Le GPU est bien present et utilise : NVIDIA RTX 4060 Laptop, driver 590.48.01,
  `/dev/dri/card1` et `renderD128` presents.

Le rendu Vulkan passe donc par le GPU reel **a travers** Xvfb (presentation
logicielle du swapchain, rasterisation sur le GPU). Mesure : 434 flips a
22-198 fps selon la scene, ~24-30 fps en jeu. Sans Xorg installe, il n'existe
aucun moyen de presenter directement sur un DRM/KMS depuis ce conteneur.
`GOWA_HEADLESS` casse la presentation : non utilise.

## 9. Manette sans /dev/input

Aucun peripherique : `/dev/input` absent, `/dev/uinput` absent. Une vraie
manette est donc hors de portee.

L'API existe et est utilisable (`SDL_AttachVirtualJoystick` +
`SDL_SetJoystickVirtualButton`/`VirtualAxis`, confirmes dans les headers SDL3
installes). **Non implementee** : les deux chemins convergent deja au meme point
du code (`fill()` dans `cellPad.cpp`, apres `poll_pad`), et la preuve demandee
etait la jouabilite reelle, obtenue au clavier. Le chemin manette virtuelle
reste la voie propre pour un futur besoin de manette.

## 10. Forcages utilises

**Aucun.** Pas de `PS3BASE_FORCE`, pas de `GOWA_AUTOPAD`, pas d'`input.cmd`
pour les preuves (a), (b), (d) et pour atteindre le monde : uniquement
`xdotool`. `PS3BASE_ALLOW_UNFOCUSED_INPUT=1` est le seul reglage non par
defaut, et il ne fait qu'autoriser la lecture clavier sur un serveur sans WM --
il n'injecte aucune entree.

## 11. Crash observe (hors piste, signale a l'orchestrateur)

Charger un monde **pre-existant** depuis la liste Start Game -> signal 11
main_thread `exe+0xcb0821`, 16 ms apres un `pngdec DecodeData` 64x64 et
l'ouverture de `MediaPS3.arc`. La route "Create New World" ne le declenche pas.
Vu 2/2 sur ce chemin ; non reproduit par la piste 22 qui ne passe pas par la
liste. Sequence exacte transmise a l'orchestrateur.
