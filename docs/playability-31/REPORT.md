# Piste 31 -- Poser un bloc, en mode Survie

Branche `explore/31`, worktree `/root/Dev/ps3base2-wt31`, VFS `vfs-31`, tag `31`.
Titre : Minecraft BLES01976. Build RelWithDebInfo clang++, `-j 8`, Xvfb `:41`
(derive par `tools/run_world.sh`). Budget 2400 s ; le run a tourne 1998 s puis a
crashe sur le defaut connu `f_0064c044` -- voir section 8, ce n'est pas la piste.

**Resultat : la pose est PROUVEE.** Sequence complete casse -> objet au sol ->
ramassage -> hotbar -> selection -> pose, en Survie, sans aucun forcage.

## 1. Antecedents (grep -Rn ... refs/)

| Trouvaille | Source (path:line) | S'applique ici ? |
|---|---|---|
| Table nom -> bit du pad, `l2`/`r2` presents | `refs/jdc-ps3recomp/libs/input/cellPad.c:128` | oui -- confirme que L2/R2 sont deux bits distincts du meme mot, donc mesurer l'octet suffit a identifier le bouton |
| `CELL_PAD_CTRL_L2 = 1 << 8`, `R2 = 1 << 9` | `refs/jdc-ps3recomp/libs/input/cellPad.h:35-36` | oui -- dans notre `cellPad.cpp` L2 est bien `b[3]|=0x01` et R2 `b[3]|=0x02` |
| `trigger_l2 = (btns & CELL_PAD_CTRL_L2) ? 255 : 0` | `refs/jdc-ps3recomp/libs/input/cellPad.c:693` | oui -- la pression analogique accompagne le bit ; notre `press()` fait deja `b[18]/b[19]=0xFF` |
| `hotbar` | **aucun match dans refs/** | rien a emprunter : la hotbar est du jeu, pas du runtime |
| `pickup` / `pickup_delay` | **aucun match dans refs/** (seuls des docs d'UI "hud one-off" citent le mot) | pareil, cote titre |
| Un port PS3 de Minecraft dans `refs/` | `refs/jdc-ps3recomp/CHANGELOG.md:522` -- "Minecraft: 0 -> 345 NIDs across 30 modules" | non -- ce port a servi de test de chargement d'imports, pas de reference de gameplay |

Aucun voisin ne documente le ramassage d'objet ni la hotbar : ce sont des
comportements du titre, pas du runtime. La seule partie outillee est le pad, et
les octets mesures ci-dessous sont conformes a la reference.

## 2. Hypothese

La piste 21 a montre qu'en **Creative** la hotbar reste vide (pas d'onglet
Creative dans cette build, R2 ne met pas le bloc casse en main). Hypothese : en
**Survie**, casser un bloc fait apparaitre un **objet ramassable** au sol, le
ramassage remplit la hotbar, et L2 pose alors reellement le bloc.

**Confirmee**, et c'est la difference de mode qui debloque la pose.

## 3. Sequence exacte qui a marche

Tout au clavier, evenements X reels (`xdotool keydown; sleep 0.15; keyup`),
`PS3BASE_ALLOW_UNFOCUSED_INPUT=1` (autorisation de lire le clavier sur un
serveur X sans WM, aucune injection), aucune touche passee par `input.cmd`.

| # | Action | Touche | Ce qu'on voit |
|---|---|---|---|
| 1 | TERMS OF USE -> Accept | `z` (Cross) | menu principal |
| 2 | autosave -> OK | `z` | "Play Game" |
| 3 | Play Game | `z` | Start Game / Create New World |
| 4 | Create New World | `z` | formulaire, **Game Mode: Survival** deja affiche |
| 5 | descendre | `Down` x6 | le curseur passe Difficulty... puis **Create New World** (verifie a l'ecran) |
| 6 | valider | `z` | "Initializing server", "Generating spawn area" |
| 7 | attendre ~250 s | -- | **le monde apparait**, coeurs + faim + hotbar vide (Survie) |
| 8 | viser le sol | `g` (stick droit vers le bas) | reticule sur l'herbe |
| 9 | **casser** | `3` (R2) maintenu 2,5 s | trou de 1 bloc, terre exposee, HUD "R2 Mine" |
| 10 | avancer sur l'objet | `i` (stick gauche avant) 1,2 s | **terre dans le slot 1**, HUD passe a "L2 Place / R2 Mine" |
| 11 | regarder le sol | `g` | sol d'herbe devant soi |
| 12 | **poser** | `1` (L2) 0,4 s | **bloc de terre pose devant le reticule**, slot 1 vide, HUD "R2 Mine" |

Detail utile : quand la main est vide, le HUD affiche `Crafting / Inventory /
R2 Mine`. Des qu'un bloc est en main il affiche `Crafting / Inventory / L2 Place /
R2 Mine` -- c'est un indicateur d'etat fiable, lu avant chaque etape.

## 4. Mesures brutes

### Octets du pad (`PS3BASE_PAD_LOG=1`)

```
L2 (touche 1)  : pad: frame 30776 bytes 00 7c 00 01 80 80 80 80   b[3]=0x01  (21 trames)
R2 (touche 3)  : pad: frame 31559 bytes 00 7c 00 02 80 80 80 80   b[3]=0x02  (59 trames)
L2 2e appui    : pad: frame 35280 bytes 00 7c 00 01 80 80 80 80   b[3]=0x01  (14 trames)
D-pad droite   : pad: frame 36940 bytes 00 7c 20 00 80 80 80 80   b[2]=0x20  (10 trames)
D-pad gauche   : pad: frame 36982 bytes 00 7c 80 00 80 80 80 80   b[2]=0x80  (9 trames)
```

Donc **L2 est bien recu (b[3]=0x01)**, comme demande, et c'est bien lui qui pose :
l'appui de 21 trames a 682,9 s precede l'apparition du bloc, l'appui de 14 trames
a 876,5 s est le second.

### Difference pixel avant / apres (captures identiques en cadrage)

```
region            taille     pixels changes
bloc pose         210x170    25835 / 35700  = 72.4 %
slot 1 hotbar      85x75      1499 /  6375  = 23.5 %
reste hotbar      590x75      4507 / 44250  = 10.2 %
sol devant        350x150       101 / 52500  =  0.2 %
```

Le sol ne bouge pas (0,2 % : le vent dans l'herbe), le bloc et le slot 1
changent massivement. C'est bien un objet qui apparait, pas un deplacement de
camera.

### Ramassage : ce qui est mesure, ce qui ne l'est pas

Mesure : apres `i` (avance 1,2 s vers le trou), le slot 1 contient de la terre et
le HUD passe a "L2 Place". La capture `pickup-items.png` (regarder autour apres
un second R2) montre **des petits cubes de terre flottant au sol**, un au pied du
joueur, un a quelques blocs, un plus loin : ce sont les objets laches.

Non mesure : je n'ai pas isole "ramassage automatique au contact" d'un eventuel
effet d'aimantation. Les deux produisent le meme resultat et je n'ai pas
d'instrumentation pour les distinguer. **Le fait etabli est : casser en Survie
cree un objet, marcher dessus le met dans la hotbar.**

### R3 (pick block) -- non concluant ici

R3 n'a pas ete necessaire : la hotbar s'est remplie par ramassage. Je ne peux
donc pas dire si R3 aurait fonctionne en Survie ; la piste 21 a mesure qu'il est
recu et interprete mais n'ajoute rien en Creative.

## 5. Resultat negatif mesure -- le D-pad ne change PAS de slot

Demande explicite de verifier le mapping avant de conclure. Mesure :

* `Right` x3 et `Left` x3 en jeu (b[2]=0x20 / 0x80 bien presents dans le log) ;
* captures `dp-r1..r3`, `dp-l1..l3` : **la hotbar est identique au pixel**, le
  cadre de selection ne bouge pas d'un slot.

Donc **le D-pad gauche/droite ne selectionne pas de slot dans cette build** (au
moins tant qu'un seul slot est rempli ; la selection reste sur le slot 1).
Et les touches `1`/`3` du clavier ne sont **pas** des slots 1 et 3 : dans
`runtime/src/host_input.cpp` elles sont mappees sur L2/R2 (`b[3] 0x01 / 0x02`).
Il n'existe donc **aucun binding clavier pour choisir un slot** ; ce n'etait pas
bloquant ici parce que le ramassage tombe dans le slot 1, deja selectionne.

## 6. Feuille trouvee

Aucune modification du runtime n'a ete necessaire : le chemin d'entree etait deja
correct (latch clavier + stick droit, piste 21). Le deblocage est **un choix de
mode de jeu**, pas un correctif de code.

**Diff : aucun.** `git diff` sur `explore/31` ne touche ni `runtime/` ni
`tools/` ; ce commit n'ajoute que `docs/playability-31/`.

## 7. Pourquoi le Creative echouait et le Survie reussit

| | Creative (piste 21) | Survie (piste 31) |
|---|---|---|
| hotbar au depart | vide | vide |
| casser un bloc (R2) | cavite, **rien** ne tombe | cavite **+ objet au sol** |
| ramassage | impossible (pas d'objet) | automatique au contact |
| hotbar apres | toujours vide | slot 1 rempli |
| L2 | recu, sans effet (main vide) | **pose le bloc** |
| onglets Creative | absents sur cette build | sans objet |

C'est la seule difference : en Survie le bloc casse devient un objet.

## 8. Preuves en captures

Chemins absolus (les apercus ne s'affichent qu'ainsi) :

| Preuve | Image | Constat |
|---|---|---|
| monde en Survie atteint | ![monde](/root/Dev/ps3base2/docs/playability-31/world-survival.png) | coeurs, faim, hotbar vide, HUD "R2 Mine" (aucun "Creative") |
| bloc casse | ![trou](/root/Dev/ps3base2/docs/playability-31/mine-hole.png) | trou de 1 bloc, terre exposee |
| objets au sol | ![objets](/root/Dev/ps3base2/docs/playability-31/pickup-items.png) | petits cubes de terre flottants |
| zoom objets | ![zoom](/root/Dev/ps3base2/docs/playability-31/zoom-drop.png) | 3 cubes distincts, dont un au pied du joueur |
| hotbar remplie | ![hotbar](/root/Dev/ps3base2/docs/playability-31/hotbar-has-dirt.png) | slot 1 = terre, HUD "L2 Place / R2 Mine" |
| **AVANT la pose** | ![avant](/root/Dev/ps3base2/docs/playability-31/place-before.png) | terre en main (slot 1), sol nu devant, HUD "L2 Place" |
| **APRES la pose** | ![apres](/root/Dev/ps3base2/docs/playability-31/place-after.png) | **bloc de terre pose devant le reticule**, slot 1 vide, HUD "R2 Mine" |
| zoom avant/apres | ![zoombloc](/root/Dev/ps3base2/docs/playability-31/zoom-block.png) | cote a cote, meme cadrage |
| zoom slot 1 | ![zoomhotbar](/root/Dev/ps3base2/docs/playability-31/zoom-hotbar.png) | slot plein -> slot vide apres la pose |

Deux captures **independantes de mes commandes**, prises par la boucle de capture
de `run_world.sh` toutes les 30 s (elles ne dependent donc pas de mon cadrage ni
de mon timing), corroborent les deux precedentes aux memes instants :

| Capture automatique | Image | Constat |
|---|---|---|
| cap-029, juste avant L2 | ![cap029](/root/Dev/ps3base2/docs/playability-31/auto-cap-029-before.png) | terre dans le slot 1, sol devant, HUD "L2 Place" |
| cap-033, ~1 min apres L2 | ![cap033](/root/Dev/ps3base2/docs/playability-31/auto-cap-033-after.png) | bloc de terre pose, slot 1 vide, HUD "R2 Mine" |

Les images `zoom-*` sont des recadrages x2/x4 des captures `place-*` et
`pickup-*`, sans retouche : elles servent a lire le bloc et le slot sans zoomer a
la main.

Le run : budget 2400 s, flips jusqu'a ~49300, VFS `vfs-31`. **Il a fini en
crash a t=1998 s** (exit 139), soit ~980 s APRES la derniere capture de preuve --
la preuve n'est donc pas affectee, mais je ne peux pas ecrire "0 crash" :

```
E 1998.035179 [883286] crash: signal 11 at 0x5e76c7183ecb (exe+0x2c68ecb)
                      fault 0x750377830014 (guest 0x63830014) thread (4J) Server 0x1000020
```

C'est exactement la forme du defaut deja documente dans
`docs/findings/f-0064c044-list-walk.md` : un pointeur de liste corrompu, lu
comme un noeud, sur la thread `(4J) Server`, avec une adresse de faute
differente a chaque run (ici `0x63830014`). Le dernier octet de pad
du log date de `t=1635 s`, soit **363 s avant le crash**, et j'etais alors
inactif (attente entre deux captures) : le defaut n'est pas declenche par la
pose, il apparait sur la thread serveur au fil du temps de jeu et releve de la
piste 33.

## 9. Forcages utilises

**Aucun.** Pas de `PS3BASE_FORCE`, pas de `GOWA_AUTOPAD`, pas de
`PS3BASE_INPUT_CMD`. Seul reglage non par defaut :
`PS3BASE_ALLOW_UNFOCUSED_INPUT=1` (lire le clavier sur un serveur X sans
gestionnaire de fenetres ; n'injecte aucune entree) et `PS3BASE_PAD_LOG=1`
(trace des octets du pad, lecture seule).
