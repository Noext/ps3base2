# PISTE G — clear WebGPU pilote par le flux du jeu (etat final)

Worktree : /root/Dev/explore-G (branche explore/G). Ce fichier est ma section, distincte de
`RESULTS.md` (ecrit par l'orchestrateur) : je ne reecris pas son fichier, il ne reecrit pas le mien.

Build mesure : /tmp/out-G2/game.wasm, 45 421 895 octets, md5 db5cb177d0c6e4174896476dce693c0f.
Flags = build_wasm.sh + `-mllvm -wasm-use-legacy-eh=false` (correctif J, sinon Chromium refuse le module).
Harness : docs/explore/G/gpu-clear/ (page, capture, header invite, script de build, serveur).

## 1. Antecedents (grep refs/ d'abord)

| Finding | Source (path:line) | Applies here? |
|---|---|---|
| `cellGcmGetLabelAddress` rend `base + index*16` dans la memoire invite ; les labels doivent etre lisibles ET ecrits par le jeu | refs/jdc-ps3recomp/libs/video/cellGcmSys.c:375-376 ; docs/RSX_GRAPHICS.md:76 | **oui** — le runtime alloue la fenetre et le guest y lit (mesure §3) |
| Une release de semaphore ecrit **4 octets** dans le label, offset = OCTETS dans la fenetre | refs/rpcs3/rpcs3/Emu/RSX/NV47/HW/nv47_sync.hpp:14-62 ; lv2/rsx.cpp du runtime | **oui** — offset 0xff0 -> slot 255, valeur 1 puis 2 |
| La release back-end 0x1D70 permute les canaux (0xAARRGGBB -> 0x00RRGGBB) | refs/rpcs3/.../NV47/HW/nv4097.cpp:828-830 | **oui** — decode verbatim dans le pont |
| `NV4097_CLEAR_SURFACE` = 0x1D94, `SET_COLOR_CLEAR_VALUE` = 0x1D90 | refs/rpcs3/rpcs3/Emu/RSX/gcm_enums.h:713-715 | **oui** — le pont les decode ; **aucun 0x1D94** n'est encore emis par le flux wasm (mesure §4) |
| Le module doit etre lie sans EH legacy pour se charger dans Chromium | piste J, reverifie §2 | **oui** — module refuse sinon |

## 2. Le module se charge dans le vrai Chromium (reverification du correctif J)

    $ /root/Dev/explore-F/tools/probe-F/ehcheck.mjs http://127.0.0.1:8109/eh.html
    CONSOLE error Failed to load resource: the server responded with a status of 404 (File not found)
    RESULT EHOK bytes=45421895

Avant le flag : `CompileError: module uses a mix of legacy and new exception handling instructions`.
Apres : `EHOK`. Le module de CE worktree (out-G2) compile donc dans Chromium.

## 3. Ce que le guest ecrit vraiment : label[255] alterne 1 et 2

    $ cd /tmp/G/runh && node --experimental-wasm-exnref game.js > guestFINAL.log
    I   1.535332 [    2] gcmfifo: reader armed: context=0x00e00000 control=0x00e00040 ring=0x40100000 size=0xfa000 put=get=0x1000
    I   1.541397 [    2] gcmfifo: probe: ctx=0x00e00000 mapped=1 labels=0x01000000 mapped=1 label255 mapped=1
    I   1.541864 [    2] gcmfifo: first label release: back-end addr=0x01000ff0 0x00000000 -> 0x00000001
    I   1.542094 [    2] wb: ring record kind=2 method=0x1d70 arg=0x00010000 src_io=0x1008 color=0x00000000 z=0x00000000
    I   1.541975 [    2] gcmfifo: label[255] <- 0x00000001 (back-end)
    I   1.542183 [    2] gcmfifo: label[255] <- 0x00000002 (back-end)
    I   2.737225 [    2] gcmfifo: label[255] <- 0x00000001 (back-end)
    I   2.737783 [    2] gcmfifo: label[255] <- 0x00000002 (back-end)
    ...
    $ grep -c 'label\[255\] <-' guestFINAL.log
    3336
    $ grep -o 't=[0-9]*s pc=0x[0-9a-f]*' guestFINAL.log | tail -2
    t=117s pc=0x002dc20c
    t=118s pc=0x002dc20c

3336 ecritures de label[255] en ~50 s de flux decode, valeur 1 puis 2 en alternance, PC du guest dans la
boucle de frame `0x2dc20c` (plus dans l'attente label[255] `0x2da5b4`).

## 4. Aucun CLEAR_SURFACE (0x1D94) dans le flux wasm — mesure, pas supposition

Le pont decode le flux et compte les methods. Histogramme des methods vus dans la fenetre FIFO sur un run
complet (sonde `/tmp/G/dump-probe.js`, dump brut de la fenetre du guest) :

    DEC begin=0x40101000 cur=0x40101bec end=0x401f9ffc clears=0 pushed=... packets=...
    DEC nonzero=... highest=... 0x1D94 headers=0 0x1D90=0 0x6C headers=... 0x1D70=...

Et le decodeur structure du runtime, sur le meme flux :

    W  wb: walk stopped at ea 0x40101614 (guest current 0x401017d8, begin 0x40101000):
       00000002 00040064 00000010 0004006c ffffffff 0004e924 8000010f 00040064 ...

Autrement dit : le flux wasm contient des SEMAPHORE_RELEASE (0x1D70), des acquires (0x68), des writes de
surface (0x200..), mais **zero** 0x1D94. Le natif, lui, emet son premier clear a t=8,67 s
(`cmd 0x00041d94`, mask 3) — oracle docs/explore/I. Le wasm n'a pas encore atteint ce code.

## 5. Le rendu WebGPU : render pass qui clear en vert, pilote par la valeur du guest

Page : `docs/explore/G/gpu-clear/gpu-clear.html`. Adaptateur mesure :

    [1.13s] console.log WebGPU adapter {"vendor":"nvidia","architecture":"lovelace","format":"rgba8unorm"}

La couleur du clear est une fonction pure d'UNE valeur ecrite par le guest : slot 255 de la fenetre de
labels (EA invite `0x01000ff0`), que le runtime ecrit en decodant les paquets SEMAPHORE_RELEASE (0x1D70)
du flux. Aucun timer ne fabrique de frame : la passe est soumise parce que le decodeur a vu la valeur
changer.

    [3.26s] READBACK gpu-000.png 1280x720 format=rgba8unorm guestValue=0x2 labelSeq=11 clearValue=(0.02,0.72,0.375,1)
    [6.30s] READBACK gpu-001.png 1280x720 format=rgba8unorm guestValue=0x2 labelSeq=1  clearValue=(0.02,0.72,0.125,1)
    [9.49s] READBACK gpu-002.png 1280x720 format=rgba8unorm guestValue=0x2 labelSeq=11 clearValue=(0.02,0.72,0.375,1)

Pixels verifies dans les deux PNG (PIL, image entiere uniforme) :

    clear-green-5184-32-labelSeq1.png   (1280, 720)  uniform True  centre (5, 184, 32)
    clear-green-5184-96-labelSeq11.png  (1280, 720)  uniform True  centre (5, 184, 96)

Deux captures a deux instants differents donnent deux verts differents : la couleur suit le flux, elle
n'est pas constante.

## 6. Deux defauts mesures dans le harnais de capture (a savoir pour F/G/H)

1. `page.screenshot()` ne voit AUCUN canvas dans ce mode headless : un canvas 2D rempli en bleu
   (0,128,255) sort **noir**. Preuve : `img/compositor-screenshot-black-canvas.png` (contient le HUD HTML,
   canvas noir). La capture fiable est un `copyTextureToBuffer` du render pass -> PNG.
2. La texture du canvas ne se relit PAS apres presentation (le copy rend des zeros). Il faut rendre la
   meme valeur deux fois : une fois a l'ecran, une fois dans une texture offscreen que la lecture copie.

## 7. Limite honnete

Le livrable de la piste — un render pass qui clear en vert, pilote par une commande venue du flux, visible
dans une capture — est atteint, mais la commande qui le pilote est **SEMAPHORE_RELEASE (0x1D70) ecrivant
label[255]**, pas CLEAR_SURFACE (0x1D94). Le 0x1D94 n'existe pas encore dans le flux wasm (§4) ; le chemin
de code du clear est ecrit, cable au ring et identique, mais il ne peut pas se declencher tant que le guest
n'emet pas ce paquet. Le point ouvert de la piste est donc exactement : faire atteindre au guest wasm le
code qui emet 0x1D94 (le natif y est a t=8,67 s).
