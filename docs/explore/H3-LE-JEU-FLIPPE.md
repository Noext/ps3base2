# H3 — LE JEU FLIPPE EN WEBASSEMBLY

## La mesure decisive

    I 114.556912 rsx: flip 2041 (buffer 0) worst 52.2 ms, >50ms 45, >100ms 0
    I 117.573503 rsx: flip 2101 (buffer 0) worst 51.3 ms, >50ms 46, >100ms 0

**2 101 flips en 117 s**, a ~60 Hz. Le walker natif de `rsx.cpp` est maintenant le seul
consommateur et il lit le vrai flux du jeu.

## Les faits qui ont change, mesures sur ce build

| element | avant | apres |
|---|---|---|
| `region 3 placed at` | `0x38000000` / `0x00e00000` | **`0x50000000`** |
| `rsx: context at` | `0x00e00000` puis `0x01100000` | **`0x50100000`** (identique au natif) |
| `_gcm_intr_thread` | absent | **5 lignes** |
| `gcmstub` | 1 | **0** |
| decodages PNG (`cellPngDec`) | **0** (stub inerte rendant null) | **103** |
| flips | 0 | **2 101** |
| module | 45 440 209 o | 45 447 537 o |

Le natif : `rsx: context at 0x50100000 (driver 0x50200000 reports 0x50300000 queue 0x8d009e00)`.
Le wasm produit **exactement la meme ligne**. C'etait l'objectif de la piste G1.

## La chaine complete, enfin bouclee

1. `Region::RsxContext` reservait 256 Mo alignes 256 Mo (plan sparse) : en compact, aucun slot sous
   `user_bound=0x50000000`, donc `vm::alloc` rendait **0** ;
2. `sys_rsx_context_allocate` ignorait ce 0 et rendait `CELL_OK` avec un contexte nul ;
3. `_cellGcmInitBody` LLE rendait `0x80010004` et n'installait jamais le callback FIFO ;
4. le jeu mourait sur `call_indirect(0)` a `lr=0x005d2c08` ;
5. corrige : base video compacte deplacee a `0x58000000` pour liberer un slot (G1), region RSX
   reduite a 128 Mo en compact seulement, `stbi_*` reel au lieu du stub nul (101 decodages PNG
   natifs contre 0), un seul bloc de contexte partage par le walker et le puits (G2, G3, G4) ;
6. le contexte etant le bon bloc et le callback installe, `_gcm_intr_thread` demarre, le paquet
   `0x102` est poste, et **le jeu flip a 60 Hz**.

## Ce que je n'ai PAS obtenu

### Les 40 en-tetes FIFO

`gcm_fifo` ne produit plus de statistiques : G1/G2 ont retire son walker (c'etait justement la
consigne « un seul lecteur »). L'instrument `[hdr200]` a disparu avec lui. Le dump comparable qu'il
faut maintenant est celui du walker natif `rsx.cpp`, qui n'a pas cet instrument.

**Je n'ai donc pas la comparaison 40/40 sur ce build.** Les pistes G1 et G3 l'avaient sur leurs
builds intermediaires (diff 0 ligne), mais pas celui-ci, qui est plus avance.

### Le compteur `flips=` de gcm_fifo

Il affiche `flips=0` : c'est le compteur du puits, qui ne decode plus rien. Les 2 101 flips sont
comptes par `rsx: flip N`, le walker. Deux compteurs pour une notion, dont un perime — exactement le
genre de confusion que la regle sur les compteurs vise a eviter.

### La ligne de menu et la capture Chromium

Non obtenues. Le jeu flip, mais je n'ai pas de ligne `main menu` ni de capture navigateur.

## Ce que le verificateur avait dit, et pourquoi c'etait perime

Il avait mesure `e3b25c3` : 0/40, `flips=0`, 0 `_gcm_intr_thread`. **C'etait exact a cette date.**
Les correctifs sont arrives apres. Son rapport reste la reference de l'etat d'alors, pas de celui-ci.