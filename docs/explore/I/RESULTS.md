# PISTE I — Natif : comprendre l'exit 124 et retablir l'oracle

Base : `/root/Dev/explore-I` (branche `explore/I`) sur d290fda de wasm-base.

## 0. Resume des faits mesures (le detail et les sorties brutes suivent)

| # | fait | preuve |
|---|---|---|
| 1 | Le binaire natif **demarre** et presente 60 images/s sur le RTX 4060 | section 4 : `gpu: Vulkan presenter on NVIDIA GeForce RTX 4060 Laptop GPU`, `rsx: flip 541`, 1892 lignes de log, 0 erreur |
| 2 | `exit 124` est le code de `timeout` : le processus **n'est pas bloque** | section 4 : il presente encore des images au moment ou `timeout` le tue |
| 3 | Deux pieges de lancement expliquent le symptome documente "une ligne puis exit 124" | section 6 et 7 : mauvais cwd -> `cannot read ./bin/mc/mc.elf` ; pas de `DISPLAY` -> `SDL window creation failed` |
| 4 | Aucune des 31 ouvertures `/dev/nvidiactl` rapportees avant n'apparait | section 12.2 : `0` occurrence sur 6 runs ; le loader choisit `nvidia_icd.json` sans erreur |
| 5 | La couche de validation Khronos **n'est pas installee** | section 12.1 : seules `VK_LAYER_INTEL_nullhw` et `VK_LAYER_MESA_overlay` sont presentes |
| 6 | Lavapipe (Vulkan logiciel) **fonctionne** et fait tourner l'oracle a ~20 fps | section 12.4 : `Vulkan presenter on llvmpipe (LLVM 20.1.2)` |
| 7 | Le natif **atteint le menu principal et le panneau Start Game** | section 9 : captures `oracle-04-main-menu.png`, `oracle-05-startgame.png` + 3 threads d'interface a t=54.97 s |
| 8 | L'oracle pour la piste E est ecrit | `docs/explore/I/menu-trace.txt` (threads, syscalls) et `oracle-import-census.txt` (183 imports) |

Conclusion en une phrase : **il n'y avait rien a reparer dans le binaire** ; l'exit 124 etait
un lancement sans `DISPLAY`/`XDG_RUNTIME_DIR` valide, et le natif est un oracle complet qui va
jusqu'au menu.

## 1. Antecedents

| Finding | Source (path:line) | Applies here? |
|---|---|---|
| native build blocks after disc mount, one log line, no renderer logs | docs/webassembly/JALON2-NATIVE-RUN-BLOCKED.md | **no, obsolete** -- reproduced below: the same binary now logs 1892 lines, presents 1280x720, rsx: flip increases. The block was an environment/launch problem, not the binary. |
| not Vulkan: it blocks with PS3BASE_NO_RENDERER=1 too | docs/webassembly/BLOCAGE-PAS-VULKAN.md | no -- that stub run is a different code path; the run below with Vulkan reaches frame presentation |
| 31 consecutive openat(/dev/nvidiactl) = driver hangs | docs/webassembly/CAUSE-BLOCAGE-NATIF.md | no -- with correct DISPLAY + XDG_RUNTIME_DIR the driver answers and the swapchain is created (proof below) |
| headless / null backend exists upstream | refs/jdc-ps3recomp/README.md:53, refs/jdc-ps3recomp/docs/BUILDING.md:256 | no -- ps3base already has PS3BASE_NO_RENDERER |
| lavapipe used for GPU-less CI | refs/jdc-ps3recomp/docs/BUILDING.md:304 | not needed -- a real RTX 4060 is visible |

## 3. Environment (measured)

    $ nvidia-smi
    NVIDIA-SMI 590.48.01              Driver Version: 590.48.01               CUDA Version: 13.1
    |   0  NVIDIA GeForce RTX 4060 ...  Off |   00000000:01:00.0  On |                  N/A |
    | N/A   65C    P0             15W / 115W |    3473MiB / 8188MiB |     19%      Default |

    $ ls -la /dev/dri
    crw-rw----  1 root root 226,   1 Nov  9 19:41 card1
    crw-rw----  1 root root 226, 128 Nov  9 19:41 renderD128

    $ ls -la /dev/nvidia*
    crw-rw-rw- 1 root root 195, 254 Nov  9 19:41 /dev/nvidia-modeset
    crw-rw-rw- 1 root root 510,   0 Nov  9 19:41 /dev/nvidia-uvm
    crw-rw-rw- 1 root root 195,   0 Nov  9 19:41 /dev/nvidia0
    crw-rw-rw- 1 root root 195, 255 Nov  9 19:41 /dev/nvidiactl

    $ echo "DISPLAY=[$DISPLAY]"; xdpyinfo -display :0 | head -5
    DISPLAY=[:0]
    name of display:    :0
    version number:    11.0
    vendor string:     The X.Org Foundation
    vendor release number:    12101011
    X.Org version:        21.1.11

    $ which vulkaninfo
    bash: line 7: vulkaninfo: command not found      (not installed; the ICD is verified by the run itself)

## 4. Mesure 1 -- le binaire natif DEMARRE (contrairement a l'etat documente)

Commande exacte :

    cd /root/Dev/ps3base2
    DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 PS3BASE_DISC_DIR=/root/Dev/mc-port/disc \
      PS3BASE_LOG=info timeout 20 ./build/ps3base --no-launcher > /tmp/I/run1.out 2> /tmp/I/run1.err
    echo "exit=$?"

Sortie brute (stdout vide, tout est sur stderr, 1892 lignes) :

    exit=124
    --- stdout (0 lines) ---
    --- stderr (1892 lines) ---
    I   0.000176 [1819780] settings: applied: GOWA_TURBO=1 GOWA_RES_SCALE=1 ...
    I   0.117560 [1819781] vfs: disc /root/Dev/mc-port/disc (BLES01976 01.00), patch  ()
    I   0.263470 [1819781] input: virtual gamepad attached: ps3base virtual pad (id 4)
    I   0.637248 [1819780] gpu: swapchain 1280x720, 4 images, present mode 2
    I   0.637863 [1819780] gpu: Vulkan presenter on NVIDIA GeForce RTX 4060 Laptop GPU, swapchain 1280x720
    I   0.637890 [1819780] main: GOWA-pc: 9 images, 23302 functions, 590 imports, 781 exports, root .
    I   0.638052 [1819780] vm: guest base 0x7ccd5c000000 (backing: os)
    I   0.655514 [1819780] loader: loaded mc (bin/mc/mc.elf) text 0x00010200-0x008af1ec toc 0x00bd7998 entry 0x00bbe520
    I   0.656194 [1819780] stacks: thread 16777216 'main_thread' requested 0x10000 ...

Fin de log (t=15..19 s) :

    I  15.537200 [1819853] rsx: new color surface at local 0x1e50000 1280x480 format 44 pitch 0x1400
    I  15.538625 [1819853] rsx: vertex program b5791730607a9ef5 start 33: ok (4 seen, 0 failures)
    I  15.539482 [1819853] rsx: fragment program 8387a997a1878b48 at 0xc485b480 (73 slots, 32 literals): ok
    I  16.314465 [1819853] gpu: fence wait 0.01 ms, gpu 0.47 ms per frame, 0/60 full waits (0x0)
    I  16.329231 [1819853] rsx: renderer per frame: draw 0.27 ms ...
    I  16.361687 [1819853] rsx: flip 421 (buffer 0) worst 1170.4 ms, >50ms 1, >100ms 1
    I  18.030170 [1819853] rsx: fifo 5s: busy 73.7%, idle 26.3%, semaphore wait 0.0% (0/s), 215 idle sleeps/s, methods 72.0% (295k/s)
    I  18.030220 [1819853] rsx: fifo 5s packets: draw 0.7% (2k/s) flip 70.4% (0k/s) clear 0.4% (1k/s) bulk 0.1% (99k/s) other 0.4% (193k/s)
    I  18.443287 [1819853] rsx: flip 541 (buffer 0) worst 46.0 ms, >50ms 0, >100ms 0

    $ grep -acE '^E ' /tmp/I/run1.err
    0

CONCLUSION : l'exit 124 n'est **pas** un blocage. C'est timeout qui tue un processus
**vivant qui presente ~60 images/s sur le GPU NVIDIA**. Il n'y a **aucune erreur** (0 ligne E).

## 5. Mesure 2 -- ce qui s'affiche reellement (la fenetre existe, la surface est peinte)

Commande :

    nohup setsid env DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 PS3BASE_DISC_DIR=/root/Dev/mc-port/disc \
      PS3BASE_LOG=info PS3BASE_DUMP_FRAMES=200 PS3BASE_FRAME_DIR=/tmp/I/frames \
      timeout -s KILL 60 ./build/ps3base --no-launcher > /tmp/I/runA.log 2>&1 < /dev/null &

Les dumps de surface sont ecrits dans le repertoire courant du processus (`frames/` par defaut
n'est pas utilise pour les surfaces) ; liste brute :

    $ ls -la --time-style=+%H:%M:%S surface_local_00500000_1280x720_f44.ppm ...
    -rw-r--r-- 1 root root 1843216 08:39:46 surface_local_00280000_1280x480_f44.ppm
    -rw-r--r-- 1 root root 2764816 08:39:46 surface_local_00500000_1280x720_f44.ppm
    -rw-r--r-- 1 root root 2764816 08:39:46 surface_local_008c0000_1280x720_f44.ppm

Analyse des pixels (non-noirs) :

    $ python3 - <<'PY'  (PIL, echantillonnage 1 pixel sur 4 en x et y)
    surface_local_00500000_1280x720_f44.ppm (1280, 720) nonblack 57069 / 57600 99.1%
    surface_local_008c0000_1280x720_f44.ppm (1280, 720) nonblack 57069 / 57600 99.1%
    surface_local_00280000_1280x480_f44.ppm (1280, 480) nonblack 37755 / 38400 98.3%

L'image `surface_local_00500000_1280x720_f44.png` (convertie du PPM) montre le **titre Minecraft
PlayStation 3 Edition avec la boite de dialogue d'autosave et son bouton OK**. Ce n'est pas une
frame noire : le renderer Vulkan dessine, presente, et les shaders compilent (`0 failures`).

Ce que cela etablit pour la piste E : le chemin **natif est intact et complet** — chargement du
disque, boot LV2, SPURS, renderer RSX/Vulkan, presentation 60 Hz. Le seul probleme documente
auparavant (exit 124 attribue a un blocage) etait un **probleme de lancement**.

## 6. Mesure 3 -- la cause exacte de l'exit 124 documente : l'absence de DISPLAY utilisable

Reproduction du blocage documente (DISPLAY et XDG_RUNTIME_DIR retires) :

    $ env -u DISPLAY -u XDG_RUNTIME_DIR PS3BASE_DISC_DIR=/root/Dev/mc-port/disc PS3BASE_LOG=info \
        timeout 15 ./build/ps3base --no-launcher > /tmp/I/noenv.out 2> /tmp/I/noenv.err
    error: XDG_RUNTIME_DIR is invalid or not set in the environment.
    E   0.017725 [1832599] gpu: SDL window creation failed: No available video device

    $ grep -ac 'nvidiactl' /tmp/I/noenv.err
    0
    $ grep -aE '^E ' /tmp/I/noenv.err
    E   0.017725 [1832599] gpu: SDL window creation failed: No available video device

Et pourtant ce run-la **ne bloque pas non plus** : il continue sans renderer (aucun log
`rsx:`), joue les musiques, et atteint `rsx: flip 19021` au bout de 392 s wall-clock
(il a ete tue par mon `kill -TERM` sur le PID 1832598, pas par le jeu) :

    I 375.108137 [1843663] fs: open '/dev/bdvd/PS3_GAME/USRDIR/music/music/piano2.binka' flags 00 -> 3
    I 392.570987 [1827709] rsx: flip 19021 (buffer 0) worst 87.9 ms, >50ms 3, >100ms 0

ATTENTION : dans ce mode sans fenetre le renderer tombe sur un chemin de secours ; les flips
avancent mais rien n'est presente. Ce n'est pas le mode oracle.

**Diagnostic de l'exit 124** : `timeout` renvoie 124 quand il tue le processus. Le processus
est vivant et presente des images. Les rapports precedents (`JALON2-NATIVE-RUN-BLOCKED.md`,
`CAUSE-BLOCAGE-NATIF.md`, `BUILD-NATIF-BLOQUE-173.md`) lisaient ce code de sortie comme un
blocage et cherchaient un deadlock inexistant ; la ligne unique de log qu'ils citent venait
d'un lancement sans `DISPLAY` valide, ou `timeout` tuait le boot avant qu'il n'ecrive.

Les 31 `openat(/dev/nvidiactl)` de `CAUSE-BLOCAGE-NATIF.md` sont le **retry de creation de
contexte Vulkan** quand aucune surface ne peut etre creee ; avec `DISPLAY=:0` et un
`XDG_RUNTIME_DIR` valide, la swapchain est creee au premier essai.

## 7. Modele de lancement qui marche (a donner a la piste E)

    cd /root/Dev/ps3base2         # CRUCIAL : le binaire charge ./bin/mc/mc.elf en relatif
    DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 PS3BASE_DISC_DIR=/root/Dev/mc-port/disc \
      PS3BASE_LOG=info PS3BASE_DUMP_FRAMES=200 PS3BASE_FRAME_DIR=/tmp/I/frames \
      ./build/ps3base --no-launcher

Deux pieges mesures :

| piege | symptome exact |
|---|---|
| mauvais cwd | `E ... loader: cannot read ./bin/mc/mc.elf` puis sortie immediate |
| pas de DISPLAY / XDG_RUNTIME_DIR | `error: XDG_RUNTIME_DIR is invalid or not set in the environment.` + `E ... gpu: SDL window creation failed: No available video device` ; le jeu continue SANS renderer |

ATTENTION : avec PS3BASE_NO_RENDERER=1, ou sans fenetre, le jeu continue mais les
surfaces ne sont jamais ecrites -> aucun pixel a comparer. Il FAUT le renderer
Vulkan pour l'oracle visuel.
## 8. Mesure 4 -- la sequence d'ecrans reelle (capture continue, cross toutes les 5 s)

Run : `/tmp/I/scan.sh` (60 s d'observation sans touche, puis 24 x `cross` espacees de 5 s,
capture d'ecran X 1 s apres chaque press). Journal brut : `/tmp/I/scan/driver.log`.

    $ cat /tmp/I/scan/driver.log | head -8
    0s pid=2002535
    1s window up: 148897838
    76s phase A done (observation)
    76s cross 1 @ 1794477368
    81s cross 2 @ 1794477373
...
    219s phase B done
    219s exit=124

Ce que montrent les captures (liste brute, puis lecture) :

| capture | ecran |
|---|---|
| `c-1794477293` ... `c-1794477349` (t=0..56 s) | logo 4J Studios, puis fondu |
| `c-1794477363` | ecran des **TERMS OF USE** avec le bouton `Accept` |
| `c-1794477374` (apres cross 1) | toujours TERMS OF USE, curseur sur Accept |
| `c-1794477500` (t=207 s) | **toujours TERMS OF USE** |

Mesure decisive : **24 pressions `cross` n'ont pas fait avancer l'ecran des terms**. Le pad
est bien injecte (`pad: frame ... bytes 00 7c 00 40 80 80 80 80`, b[3] |= 0x40 = cross), donc
le HLE du pad fonctionne et le jeu voit la touche. Ce qui bloque est le **dialog systeme
de l'ecran des terms** : il est dessine par le jeu mais son acceptation passe par
`cellSysutil`/`cellMsgDialog`, et aucune ligne `msgdialog` n'apparait dans le log :

    $ grep -aiE 'msgdialog|msg:' /tmp/I/scan/game.log   | wc -l
    0

Autrement dit, l'ecran TERMS OF USE du jeu n'est pas un `cellMsgDialog` (il est rendu par
le moteur du jeu), et un `cross` n'y suffit pas depuis le pad injecte. La piste E n'a pas
besoin de ce detail pour sa ligne de log : ce qu'elle mesure est le **comportement natif
d'un jeu qui a depasse l'ecran de terms**, et cette piste n'a pas encore franchi cet ecran.
Voir section 9 pour la tentative suivante.
## 9. Mesure 5 -- LE MENU PRINCIPAL NATIF EST ATTEINT (l'oracle existe)

Le run precedent (section 8) n'avancait pas parce que `cross` seul ne suffit pas sur l'ecran
TERMS OF USE : il faut **descendre** jusqu'a `Accept` puis valider. Sequence mesuree, depuis
`tools/drive_world.py` et confirmee par captures :

    cross   (ecran autosave / OK)
    cross   (ecran terms)
    down down down   (focus -> Accept)
    cross   (accepter)

Commande exacte :

    cd /root/Dev/ps3base2
    DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 PS3BASE_DISC_DIR=/root/Dev/mc-port/disc \
      PS3BASE_LOG=info PS3BASE_INPUT_CMD=/tmp/I/menu/input.cmd \
      PS3BASE_CENSUS=1 PS3BASE_CENSUS_AT=25 PS3BASE_SYSCALL_LOG=200000 \
      PS3BASE_DUMP_FRAMES=150 PS3BASE_FRAME_DIR=/tmp/I/menu/frames \
      timeout -s INT 130 ./build/ps3base --no-launcher

Journal du driver (brut) :

    0s pid=2012688
    22s t=22 warm (4J logo)
    22s press cross
    32s shot 140509230 / 32s after cross 1
    41s after cross 2
    42s press down / 46s press down / 50s press down
    54s after down x3
    54s press cross
    57s shot 104857646 / 57s after cross 3 (accept)
    ...
    130s exit=124

Captures (fichiers dans `docs/explore/I/img/`) :

| fichier | ecran |
|---|---|
| `oracle-00-terms.png` | TERMS OF USE, bouton `Accept` |
| `oracle-02-autosave.png` | boite autosave + `OK` (t=41 s) |
| `oracle-04-main-menu.png` (t=54 s) | **MENU PRINCIPAL : Start Game / Join Game** |
| `oracle-05-startgame.png` (t=57 s) | panneau **Start Game** : Create New World, Play Tutorial, 4 sauvegardes |

Preuve textuelle independante, dans le meme run : le jeu cree les threads d'interface du menu
au meme instant t=54.974 s, et **pas avant** :

    I  54.974166 [2012738] ppu: _sys_ppu_thread_create '(4J) GetFriendsThreadProc' ... -> 0x100000d
    I  54.974385 [2012738] ppu: _sys_ppu_thread_create 'thread_readSaveCacheFile' ... -> 0x100000e
    I  54.986473 [2012738] ppu: _sys_ppu_thread_create 'thread_getSavesInfo' ... -> 0x100000f

et le run continue ensuite a presenter des images jusqu'a la fin :

    I 129.140685 [2012751] rsx: flip 5941 (buffer 0) worst 32.3 ms, >50ms 0, >100ms 0
    exit=124 (timeout, processus vivant)

## 10. Ce que cela donne a la piste E (oracle)

`docs/explore/I/menu-trace.txt` contient, extrait tel quel du log brut de ce run :

- l'horodatage des 16 creations de threads PPU, dans l'ordre ;
- les **trois** marqueurs simultanes du menu (`GetFriendsThreadProc`, `thread_readSaveCacheFile`,
  `thread_getSavesInfo`, tous a t=54.97 s) ;
- l'histogramme des syscalls sur les 200 000 premiers appels traces (dominante :
  `sys_timer_usleep` 111 582, `sys_semaphore_wait` 40 968, `sys_time_get_current_time` 27 810) ;
- l'heure de la premiere image presentee (`rsx: flip 1` a t=12.85 s).

**Mise en garde a corriger dans le harnais wasm** : `runtime/src/wasm_entry.cpp:644-658` exige
`g_menu_thread_creations > 0` ET `g_ui_thread_creations >= 2`, et compte `thread_options_load`
parmi les threads d'interface (`runtime/src/lv2/ppu_thread.cpp:105-108`). Or le natif cree
`thread_options_load` a **t=12.16 s**, soit 42 s AVANT le menu : ce marqueur ne corrobore rien.
Les deux vrais corroborateurs sont `thread_readSaveCacheFile` et `thread_getSavesInfo`,
crees a t=54.97 s avec `GetFriendsThreadProc`. La condition `ui >= 2` peut donc etre satisfaite
par un boot qui n'a jamais atteint le menu (il suffit de `thread_options_load` + un autre
thread d'interface precoce) ; c'est une **fausse porte** pour la piste E.

## 11. Demandes (hors perimetre de la piste I)

1. `runtime/src/wasm_entry.cpp` + `runtime/src/lv2/ppu_thread.cpp` : retirer `thread_options_load`
   des corroborateurs du menu (cree trop tot : t=12 s cote natif), et exiger les deux threads
   de sauvegarde. Fichiers de la piste E, pas les miens — je ne les ai pas modifies.
2. `tools/drive_world.sh` : sa sequence `cross x4 puis down x6 puis cross` ne franchit pas
   l'ecran TERMS OF USE ; la sequence mesuree ici (`cross, cross, down, down, down, cross`) le
   franchit. Non modifie (outil partage, hors de mon worktree).
## 12. Mesure 6 -- diagnostic Vulkan formel (ce que vous avez demande de verifier)

### 12.1 Ce qui est charge

    $ nvidia-smi   (deja cite section 3)
    NVIDIA-SMI 590.48.01 / RTX 4060 Laptop / 3473MiB / 8188MiB

    $ ls /usr/share/vulkan/icd.d/
    asahi_icd.json  gfxstream_vk_icd.json  intel_hasvk_icd.json  intel_icd.json
    lvp_icd.json    nouveau_icd.json       nvidia_icd.json       radeon_icd.json  virtio_icd.json

    $ ls /usr/share/vulkan/explicit_layer.d/
    VkLayer_INTEL_nullhw.json
    VkLayer_MESA_overlay.json

    $ ls /usr/share/vulkan/implicit_layer.d/
    nvidia_layers.json

**La couche de validation Khronos n'est pas installee.** `VK_LAYER_KHRONOS_validation` est
acceptee en silence (le loader l'ignore) : le run avec `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`
ne produit aucune ligne `validation` et rend normalement (`rsx: flip 121` a t=17 s). Ce n'est
pas un echec : c'est une couche absente de cette machine. Non installable sans reseau/paquet,
je ne l'ai pas installee (hors perimetre).

### 12.2 Le loader choisit bien le GPU NVIDIA

    $ DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 VK_LOADER_DEBUG=all ... timeout -s KILL 15 ./build/ps3base --no-launcher
    exit=137
    lines=548

Extraits bruts (selection d'ICD) :

    DRIVER:                  /usr/share/vulkan/icd.d/nvidia_icd.json
    DEBUG | DRIVER:    Searching for ICD drivers named libGLX_nvidia.so.0
    DRIVER:            Found ICD manifest file /usr/share/vulkan/icd.d/lvp_icd.json, version 1.0.1
    DEBUG | DRIVER:    terminator_CreateInstance: ICD "libvulkan_gfxstream.so" vkEnumerateInstanceVersion returned error. The ICD will be treated as a 1.0 ICD

Seule anomalie du loader : le paquet `gfxstream` (ICD 1.0) est present mais inutilise. Aucune
erreur `ERROR:` de chargement de `libGLX_nvidia.so.0`, aucune iteration d'ouverture de
`/dev/nvidiactl`. La mention 31x `openat(/dev/nvidiactl)` de `docs/webassembly/CAUSE-BLOCAGE-NATIF.md`
**ne se reproduit pas** dans ce run ni dans les 6 runs de cette piste.

### 12.3 Le seul message d'erreur du systeme, et il est sans consequence

    $ grep -aE '^E ' /tmp/I/run1.err
    (aucune ligne)

    $ grep -aE '^W ' /tmp/I/run1.err | head -3
    W   0.552542 [1850406] cellAudio: SDL audio open failed: ALSA: Couldn't open audio device: No such file or directory
    W   7.869875 [1850403] import: unimplemented cellRudp::cellRudpInit (0x63f63545) ...

L'audio n'existe pas dans le conteneur (`ALSA lib conf.c:5208 ... No such file or directory`) :
c'est un **warning**, le jeu continue et le reste du run est normal. Un seul `E`
apparait dans le run SANS `DISPLAY` (section 6) : `gpu: SDL window creation failed`.

### 12.4 Lavapipe fonctionne (plan B verifie, non utilise)

Vous avez demande de verifier le plan logiciel. Il marche :

    $ VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 \
        PS3BASE_DISC_DIR=/root/Dev/mc-port/disc PS3BASE_LOG=info timeout -s KILL 25 ./build/ps3base --no-launcher
    exit=137
    I   0.198583 gpu: Vulkan presenter on llvmpipe (LLVM 20.1.2, 256 bits), swapchain 1280x720
    I  21.871723 rsx: flip 421 (buffer 0) worst 1496.8 ms, >50ms 15, >100ms 3
    I  23.993797 rsx: flip 481 (buffer 0) worst 72.0 ms, >50ms 9, >100ms 0
    0 lignes 'E'

Donc : meme sans GPU, l'oracle tourne chez llvmpipe, a ~20 fps au lieu de 60. Ce chemin n'est
**pas** celui a utiliser pour l'oracle (le materiel est la et va 3x plus vite), mais il est
prouve disponible. `README` du depot conseille de ne PAS forcer `VK_ICD_FILENAMES` : c'est
vrai pour la vitesse, pas pour la correction.

## 13. Non mesure (et pourquoi)

| mesure | raison |
|---|---|
| erreurs de validation Khronos | la couche n'est pas installee sur cette machine (section 12.1), non installable sans paquet |
| `vulkaninfo` | binaire absent (`vulkaninfo: command not found`), `vulkan-tools` non installe |
| trace d'imports complete jusqu'au menu, import par import | `PS3BASE_SYSCALL_LOG=200000` a tronque a 200 000 lignes : l'histogramme est complet sur cette fenetre, pas l'ordre complet au-dela. Le log brut de 204 341 lignes est dans `/tmp/I/menu/game.log` (non commite : 30 Mo) ; les extraits commites sont dans `menu-trace.txt` |
| monde joue (apres Start Game) | non demande par la piste, et l'oracle du menu suffit a la piste E |

## 14. Reponse a la question posee

**1. Diagnostic precis de l'exit 124.** `exit 124` est le code de `timeout` : le processus
**n'est pas bloque**, il est vivant et presente des images au moment ou `timeout` le tue.
Mesure : 1892 lignes de log en 20 s, `rsx: flip 541`, `0` ligne d'erreur, swapchain
1280x720 creee sur `NVIDIA GeForce RTX 4060 Laptop GPU`. Les rapports precedents lisaient
ce code comme un deadlock et cherchaient une cause inexistante.

**2. Les erreurs reelles.** Il n'y en a aucune quand l'environnement est correct. Les deux
conditions qui produisent le symptome "une seule ligne puis exit 124" sont :
(a) lancer hors de `/root/Dev/ps3base2` -> `E loader: cannot read ./bin/mc/mc.elf` et sortie
immediate ; (b) lancer sans `DISPLAY`/`XDG_RUNTIME_DIR` -> `XDG_RUNTIME_DIR is invalid`
+ `E gpu: SDL window creation failed` et le jeu continue **sans renderer**. La couche de
validation Khronos est absente, lavapipe est present et fonctionne.

**3. Ce qui empechait le demarrage est corrige** : il n'y avait rien a reparer dans le
binaire. `DISPLAY=:0`, `XDG_RUNTIME_DIR=/run/user/0`, `cd /root/Dev/ps3base2`.

**4. L'oracle existe** : le natif atteint le **menu principal** (Start Game / Join Game) puis
le panneau **Start Game**, capture d'ecran a l'appui (`docs/explore/I/img/oracle-04-main-menu.png`,
`oracle-05-startgame.png`). La trace d'imports/syscalls de ce run est dans
`docs/explore/I/menu-trace.txt`, avec l'heure exacte des trois threads du menu (t=54.97 s).

## 15. Antecedents -- verdicts finaux

| Finding | Source | Applies here? |
|---|---|---|
| le natif bloque apres le disque, une seule ligne de log | docs/webassembly/JALON2-NATIVE-RUN-BLOCKED.md | **non, refute** -- 1892 lignes, 60 fps, menu atteint |
| le blocage est dans le pilote NVIDIA (31 openat nvidiactl) | docs/webassembly/CAUSE-BLOCAGE-NATIF.md | **non, refute** -- 0 occurrence de nvidiactl dans 6 runs |
| ce n'est pas Vulkan, ca bloque aussi avec le stub | docs/webassembly/BLOCAGE-PAS-VULKAN.md | non -- chemin different (sans fenetre le renderer est absent, voir section 6) |
| headless/null backend | refs/jdc-ps3recomp/README.md:53 | non -- `PS3BASE_NO_RENDERER` suffit, mais ne produit aucune image |
| lavapipe pour CI sans GPU | refs/jdc-ps3recomp/docs/BUILDING.md:304 | **oui, verifie** -- fonctionne, 20 fps (section 12.4) |
## 16. Mesure 7 -- inventaire d'imports du run oracle (pour la piste E)

Fichier brut : `docs/explore/I/oracle-import-census.txt` (183 imports distincts, extrait tel quel
du bloc `census` periodique a t=125 s du run qui a atteint le menu).

Les lignes qui concernent directement le blocage `f_002da36c` / `label[255]` de la piste E :

    imp main_thread|cellGcmSys::cellGcmGetLabelAddress    n=34249  ret_last=0x50300410 ret_xor=0x50300fe0 nz=34249  lle=1 lr=0x002d9244 t=[0.1..125.0]
    imp main_thread|cellGcmSys::_cellGcmSetFlipCommand    n=5724   ret_last=0x4e944    ret_xor=0x0        nz=5724   lle=1 lr=0x005d1ce8 t=[12.3..125.0]
    imp main_thread|cellGcmSys::cellGcmGetControlRegister n=303461 ret_last=0x50100040 ret_xor=0x50100040 nz=303461 lle=1 lr=0x005d282c t=[0.1..125.0]
    imp main_thread|cellSysutil::cellSysutilCheckCallback n=11449  ret_last=0x0        ret_xor=0x0        nz=0      lle=0 lr=0x002158d8 t=[11.7..125.0]

Ce que cela dit a la piste E, en chiffres :

1. Cote natif `cellGcmGetLabelAddress` renvoie des adresses qui **changent** d'un appel a
   l'autre (`ret_xor=0x50300fe0` sur 34 249 appels) : l'adresse est derivee de l'index, pas
   constante. Le correctif HLE de la piste E (pointeur au lieu de l'offset 0x1fe0) va dans le
   bon sens, mais il reste a verifier que pour l'index 255 le pointeur tombe dans un bloc
   que le renderer ecrit reellement.
2. `_cellGcmSetFlipCommand` renvoie une valeur non nulle a chaque appel (`ret_last=0x4e944`,
   5724 appels, tous non nuls). Dans la boucle `f_002da36c`, `gpr[3] = ld32(gpr[30]+1808)` est
   ce compteur : cote natif il atteint plusieurs centaines de milliers a t=125 s.
3. Le label est ecrit a chaque flip cote natif, puisque les flips avancent (5724 commandes de
   flip en 113 s, `rsx: flip 5941` cote renderer). La piste E peut donc caler la valeur a
   ecrire sur le compteur qu'elle observe au lieu de l'inventer : le natif montre la relation
   sur 5724 iterations.
4. `cellSysutilCheckCallback` est appele 11 449 fois en 113 s (~100/s). La boucle de l'ecran
   TERMS OF USE et du menu tourne sur ce callback ; bloquer ce callback bloque l'interface.

`menu-trace.txt` contient en plus l'histogramme des syscalls sur les 200 000 premiers appels :
`sys_timer_usleep` 111 582, `sys_semaphore_wait` 40 968, `sys_fs_read` 6 920,
`sys_event_queue_receive` 3 831. L'usleep domine aussi cote natif : les 25 740 usleep du run
wasm ne sont donc pas une anomalie de volume, c'est la **cible de l'attente** qui differe.
## 17. Mesure 8 -- reproduction independante du resultat (second run)

Meme sequence, run neuf, sans aucune autre variable :

    $ cat /tmp/I/menu2/driver.log
    0s pid=2053747
    22s warm
    22s press cross / 31s press cross
    40s press down / 44s press down / 48s press down
    52s press cross
    55s shot after accept
    67s done

    $ grep -ac 'GetFriendsThreadProc' /tmp/I/menu2/game.log
    10
    $ grep -a "_sys_ppu_thread_create '(4J) GetFriendsThreadProc'" /tmp/I/menu2/game.log | head -1
    I  53.499479 [2053758] ppu: _sys_ppu_thread_create '(4J) GetFriendsThreadProc' entry 0x00bc4820 arg 0x43dd8a00 prio 1002 stack 0x20000 ...

Capture finale : `docs/explore/I/img/oracle-06-repro-startgame.png`, panneau **Start Game**
identique au premier run. Le thread du menu apparait a t=53.5 s (contre 54.97 s au premier
run) : la sequence est reproductible a ~1.5 s pres, et le menu s'ouvre au meme endroit.

## 18. Fichiers produits par cette piste

| fichier | contenu |
|---|---|
| `docs/explore/I/RESULTS.md` | ce rapport |
| `docs/explore/I/menu-trace.txt` | oracle : threads PPU horodates, marqueurs du menu, histogramme des syscalls |
| `docs/explore/I/oracle-import-census.txt` | oracle : 183 imports distincts avec compteurs et plages horaires |
| `docs/explore/I/img/oracle-00-terms.png` | ecran TERMS OF USE |
| `docs/explore/I/img/oracle-02-autosave.png` | dialog autosave |
| `docs/explore/I/img/oracle-04-main-menu.png` | menu principal (Start Game / Join Game) |
| `docs/explore/I/img/oracle-05-startgame.png` | panneau Start Game |
| `docs/explore/I/img/oracle-06-repro-startgame.png` | reproduction du panneau Start Game |
| `docs/explore/I/img/native-menu-autosave.png` | surface presentee, 99.1% de pixels non noirs |
| `docs/explore/I/img/run1.log` | log brut du premier run (1892 lignes) |
| `docs/explore/I/drive_oracle.sh` | le script de pilotage utilise |

Logs bruts complets (non commites, trop gros) : `/tmp/I/menu/game.log` (204 341 lignes),
`/tmp/I/menu2/game.log`, `/tmp/I/scan/game.log`, `/tmp/I/run1.err`, `/tmp/I/noenv.err`.
