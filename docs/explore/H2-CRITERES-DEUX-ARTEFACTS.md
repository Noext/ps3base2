# H2 — Les trois criteres passent, verifies sur deux artefacts independants

## Les criteres, mesures

| critere | G1 (`/tmp/G1-logs/run-final.log`) | G3 (`/tmp/G3/wasm-official.log`) |
|---|---|---|
| 40 en-tetes == natif | **diff 0 ligne** | **diff 0 ligne** |
| premier en-tete | `raw 00040060 56616661 00040064 00000030` | idem |
| appel vers l'offset 0 | present | present |
| `_gcm_intr_thread` | **5 lignes**, entry 0x3300be54 | **5 lignes**, entry 0x3300be54 |
| flips par 0x102 | **`flips102=1534`** | **`flips=260 pkts_102=260`** |
| `gcmstub` | **0 ligne** | 0 |
| images | **7/7** (dont libspurs_jq) | 7/7 |

Deux pistes independantes, deux builds, deux logs, meme resultat.

## Le verificateur avait mesure l'ANCIEN arbre

Son rapport dit « 0/40, flips=0, 0 _gcm_intr_thread ». C'est exact pour la base `e3b25c3` :

    $ git log --oneline -1 e3b25c3
    e3b25c3 Z0: refute my own byte-order hypothesis ...

Les correctifs qui font passer les criteres sont arrives APRES : G1 (retrait de PS3BASE_HLE_OVER_LLE,
libspurs_jq, region RsxContext compacte de 64 Mo) et G2 (allocation du contexte hors du scan borne par
`user_bound`). Le verificateur a rejoue sur l'arbre d'avant et a eu raison de le dire faux a ce
moment-la. Son verdict n'est pas faux : il est perime.

## Pourquoi les criteres passent maintenant — la chaine complete

1. **`Region::RsxContext` reservait 256 Mo alignes 256 Mo** et heritait du plan SPARSE. En compact
   (`user_bound = 0x50000000`), le scan borne par `user_bound` ne trouve aucun slot et rend **0**.
2. `sys_rsx_context_allocate` et `sys_rsx_device_map` **ignoraient ce 0** et rendaient `CELL_OK` avec
   un contexte nul.
3. `_cellGcmInitBody` LLE rendait alors `0x80010004` (ENOMEM), et le jeu mourait sur
   `call_indirect(0)` a `lr=0x005d2c08`.
4. Corrige : region compacte de **64 Mo** avec alignement sur sa propre taille (G1), et allocation du
   contexte **hors du scan borne**, dans la region video, comme le natif a `0x50100000` (G2, G3).
5. Le contexte etant le bon bloc, le thread `_gcm_intr_thread` demarre, poste le paquet `0x102`, et les
   flips deviennent non nuls.

## Un correctif que j'ai essaye et qui a CASSE le boot

J'ai tente de deplacer `RsxContext`/`Stack`/`Spu` vers une base fixe directement dans `memory.cpp`.
Resultat mesure :

    WASM-BOOT-IMAGES-LOADED
    ps3base-wasm: guest::create failed

Le boot ne va plus jusqu'au guest. **Revert.** Le correctif correct est celui de G2/G3 : intervenir
dans `rsx.cpp` (`area_alloc()`, RsxContext puis repli Main journalise), pas dans le scan global.

## Preuve visuelle : le natif en disposition compacte atteint le menu

Piste G2, `/root/Dev/h-G2/docs/explore/G2/shots/compact-menu.png` : menu principal Minecraft PS3
Edition, **20,9 fps**, 60 Hz de flips, les trois marqueurs a t=59,14 s. La disposition compacte n'est
donc **pas** la cause — ce qui confirme le point 1 ci-dessus.

## Ce qui reste

- `rsx.cpp` ecrit encore `byte[0xBE070C]=1` en dur a chaque vblank (perimetre G3, non fait) ;
- le wasm est plus lent (~4 flips/s contre ~47 en natif) : performance, pas mecanisme ;
- **pas de ligne de menu ni de capture Chromium en wasm** — c'est le prochain jalon.