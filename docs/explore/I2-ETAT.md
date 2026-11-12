# I2 — Etat consolide du tour I

## Les quatre criteres : trois verts mesures par moi, un rouge preexistant

| critere | verdict | preuve |
|---|---|---|
| C1 40 en-tetes FIFO | **VERT** | `diff` des 40 lignes = 0 contre `/root/Dev/z1-evidence/native-500.txt`, 2 builds neufs |
| C2 `_gcm_intr_thread` | **VERT** | 5 lignes, contexte `0x50100000`, 0 ligne `gcmstub` |
| C3 flips par 0x102 | **VERT** | `rsx: flip` croit jusqu'a 1741/2221 ; sonde dediee : 1400 paquets 0x102 pour 1501 flips |
| C4 test C | **VERT** | 12 PASS / 0 FAIL avec le harnais de `origin/main` |
| C5 regress natif | **ROUGE sur rdr, preexistant** | voir ci-dessous |

## rdr n'est PAS une regression de cette session

Le commit natif `4a5a132` (21:10, arbre `/root/Dev/ps3base2`) est titre :

    rdr: the title screen, captured, and the chain that still stops it at 23 s

et son corps dit : « RED DEAD REDEMPTION with Press START is on screen for about 1.2 s at ~22 s ».
Le titre s'affiche donc, puis s'arrete a 23 s. Mesure du verificateur :

    rdr  FAIL  SPURS abort  flips=601  crashes=0  screen=socialclu

**rdr etait deja casse avant tout changement de cette session.** Les deux autres titres passent
(minecraft 6841, gta-sa 6301). C5 est donc « 2 verts sur 3, le rouge etant anterieur ».

## Un defaut de la porte de fusion, mesure par le verificateur

Appele par chemin absolu, `merge-tests.sh` resout son ROOT a cote de lui et affiche
`SKIP rsx_fifo_synth` **en vert** : le test C n'est pas rejoue et la porte dit PASS. Avec une commande
interne a l'arbre (`./tests/rsx_fifo_synth.sh . OUT`) il est rejoue pour de vrai (12/0).

Un `SKIP` compte comme un succes est le meme defaut de classe que `grep -c flip` : un compteur qui
compte autre chose que ce qu'on croit. A corriger : un SKIP ne doit pas rendre vert.

## Ce qui a ete livre ce tour

| piste | livrable mesure |
|---|---|
| H5 | **table 27 formats RSX->WebGPU** (16 Direct, 6 Feature, 5 Expand, 0 absent), texture du logo rendue en WebGPU avec preuve readback (67,8 % / 0,37 % de texels non noirs), 2 hash FNV-1a egaux au natif |
| H2 | inventaire wgpu/Dawn en cours |
| H3 | WGSL : demarre |
| H4 | transport des draws : demarre |
| H6 | capture : cherche l'instrument existant |
| H7 | vitesse : demarre |
| H1 | **bloque** : le jeu n'appelle jamais `cellPadGetData` |

## Le blocage de H1, nomme

    cellPadInit     : 1 appel
    cellPadGetData  : 0 appel   <- le jeu ne lit jamais le pad

Le jeu s'endort a `pc=0x003a18b0` (`usleep(50 ms)` en boucle, `lr=0x0021c18c`) apres 10 s d'activite
reelle. Tant que `cellPadGetData` n'est pas appele, aucun script d'entree ne peut le piloter.

## La regle refs/ etait morte, reparee

`wasm-base/refs` n'existait pas et `ps3base2/refs` pointait vers un repertoire vide : **tous les
`grep -R refs/` de cette session ont rendu zero**, et je l'ai lu comme « aucun antecedent ».
Repare avec les vrais ports (`port-flow`, `port-saintsrow2`, `port-allstars-ps3`, `midway-ps3`,
`rpcs3`). Premier antecedent exploitable : `refs/rpcs3/rpcs3/Emu/Cell/Modules/cellPad.cpp:695`.