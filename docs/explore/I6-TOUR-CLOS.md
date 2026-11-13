# I6 — Tour clos : ce qui est livre, ce qui reste

## Les quatre livrables demandes

| livrable | etat |
|---|---|
| ligne du menu | **NON** |
| capture du menu dans Chromium | **NON** |
| tableau de validation des shaders | **OUI** : 11/11 naga, 11/11 Chromium, 0 rejete |
| comparaison WebGPU natif / Vulkan | **OUI** : RMS 69,51 ; controle 0,0000 |

## Les huit pistes, ce que chacune a livre

| piste | livrable mesure |
|---|---|
| H1 | cause du blocage menu : `(4J) Basic Event Handler` jamais cree ; `cellPadGetData` 0 contre 2549 |
| H2 | **backend WebGPU natif** (wgpu-native 29.0.1.1, 19 Mo, 8,4 s), 8/8 SPIR-V, RMS 69,51 vs Vulkan |
| H3 | **emetteur WGSL** (956 l., zero GLSL/SPIR-V), **11/11 naga et Chromium, 0 rejete** |
| H4 | **transport des draws** : 56 183 enregistrements, 3,51 Go, 2 compteurs independants concordants, `OWNERSHIP page=1 worker=0` |
| H5 | **table 27 formats** RSX->WebGPU, texture du logo rendue, 2 hash FNV-1a egaux au natif |
| H6 | script de capture unique importe (pas reecrit), 3 pieges re-mesures, blocage navigateur trouve |
| H7 | **3 pistes de vitesse chiffrees** : `-O2` 0 % en regime, `store_range` 25 % du CPU, SIMD 0 % |
| V | C1/C2/C3/C4 verts, C5 rouge sur rdr **preexistant** |

## Corrections faites cette session au-dela des livrables

### Le module boote et flippe dans Chromium

    rsx: flip 1741  |  guest clock 100,1 % du temps reel

Quatre causes : pool pthread 4->24 + PROXY_TO_PTHREAD, COOP/COEP absents de `http.server`,
`import.meta.url` dans `worker.js`, absence de `MODULARIZE`/`EXPORT_ES6`.

### `refs/` repare

Le repertoire n'existait pas et son lien pointait sur du vide : **tous les `grep -R refs/` de la
session ont rendu zero**, lus a tort comme « aucun antecedent ». Repare (rpcs3, flow, saintsrow2,
allstars-ps3, midway-ps3).

### Le critere de menu corrige deux fois

`thread_options_load` retire des corroborateurs (cree 42 s avant le menu) ; le verdict n'est plus
evalue a la seule iteration 100.

## Les deux blocages qui restent, avec leur adresse

### 1. `(4J) Basic Event Handler`, entry 0x00bc4820

    cellPadGetData : natif 2549, wasm 0
    wasm 8 threads, natif 12+
    wait-loop lr=0x2221fc, octet 0xCE5AA0 remis a 0 par f_0021d850 (ce thread)

Le site d'appel du pad (`0x222274`) est **en aval** de la boucle d'attente. Tant que ce thread n'est
pas cree, aucune entree ne peut atteindre le jeu, quelle que soit la source (fichier, script integre,
Gamepad, clavier).

### 2. `PS3BASE_NO_RENDERER=1`

`grep -c rsx_renderer game.js` = 0. Le module flip mais ne dessine pas. Le hook de readback
s'installe (`hook: true`) et lit du vide. **Aucune capture d'image du jeu n'est possible tant que ce
drapeau est la.**

## L'ordre logique de la suite

1. Retirer `PS3BASE_NO_RENDERER` et cabler le backend WebGPU (H2) sur le chemin de rendu — sans cela
   aucune image, donc aucune capture, donc le Jalon 4 reste ouvert ;
2. cabler les shaders WGSL de H3 sur les pipelines WebGPU (ils sont valides mais pas branches) ;
3. trouver pourquoi `(4J) Basic Event Handler` n'est pas cree (8 threads contre 12+), ce qui debloque
   `cellPadGetData` puis le menu ;
4. alors seulement, la ligne du menu et la capture demandees.

## Choix de vitesse recommande, sur les chiffres de H7

`rsv::store_range` est le **seul** gain reel : 25,0 % du CPU du thread PPU, boot -48 %, flips +37 %,
pour ~40 lignes en CAS. `-O2` ne gagne rien en regime et fait passer la plus grosse fonction de
737 751 a **1 391 941 octets**, ce qui casse le critere « < 1 Mo ». SIMD : 0 %.