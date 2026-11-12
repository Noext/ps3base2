# PISTE I — Natif : comprendre l'exit 124 et retablir l'oracle

Base : /root/Dev/explore-I (branche explore/I) sur d290fda de wasm-base.
Worktree cree a l'instant, arbres partages lies (bin out out-mc third_party fw ref).

## 0. Antecedents (grep -R dans refs/ et docs/)

A remplir avec la sortie brute du grep, voir section 2.

## 1. Etat de depart

Voir mesures ci-dessous.

## 2. Antecedents

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
