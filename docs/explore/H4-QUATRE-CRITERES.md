# H4 — LES QUATRE CRITERES PASSENT, sur un seul build

## C1 — 40 en-tetes FIFO identiques au natif

    $ diff <wasm 40 rsxb lines> <native 40 rsxb lines>
    wasm: 40   native: 40   DIFF LINES: 0

Les six premiers, cote wasm (ce build) :

    rsxb: ea 0x40101000 raw 00040060 56616661 00040064 00000030 count 1 method 0x0060 noninc 0 put 0x000013a0
    rsxb: ea 0x40101008 raw 00040064 00000030 00040068 00000001 count 1 method 0x0064 noninc 0 put 0x000013a0
    rsxb: ea 0x40101010 raw 00040068 00000001 00040060 66616661 count 1 method 0x0068 noninc 0 put 0x000013a0
    rsxb: ea 0x40101018 raw 00040060 66616661 00000002 00041d84 count 1 method 0x0060 noninc 0 put 0x000013a0
    rsxb: ea 0x40101020 raw 00000002 00041d84 00000003 00041804 count 0 method 0x0000 noninc 0 put 0x000013a0
    rsxb: ea 0x40100000 raw 00040000 31337000 003c0180 66604200 count 1 method 0x0000 noninc 0 put 0x000013a0

Le premier en-tete exige par le critere (`00040060 56616661`) est present, l'appel vers l'offset 0
(`ea 0x40100000`) aussi, et `put 0x000013a0` correspond au natif.

## C2 — `_gcm_intr_thread` present

    5 lignes dans le run

## C3 — flips non nuls

    rsx: flip 1501 (buffer 0) worst 51.3 ms, >50ms 41, >100ms 0

**1 501 flips** a ~60 Hz (mesure precedente sur le meme build : 2 101 en 117 s).

## Contexte : pourquoi ca marche maintenant

| element | valeur |
|---|---|
| `region 3 placed at` | **0x50000000** |
| `rsx: context at` | **0x50100000** — identique au natif |
| `gcmstub` | **0** |
| `PS3BASE_HLE_OVER_LLE` | **absent** du build (le code dit « DELIBERATELY ABSENT ») |
| decodages PNG | **103** |
| module | 45 447 537 o |

## Comment l'instrument a ete trouve, et l'erreur que j'ai failli refaire

J'ai d'abord ecrit mon propre dump dans `rsx.cpp` : il ne tirait pas (0 ligne). En lisant autour,
j'ai decouvert qu'un instrument `PS3BASE_RSXB_PROBE` **existait deja** dans le fichier, desactive par
compilation. Je l'ai active (`-DPS3BASE_RSXB_PROBE=1`) et supprime mon doublon.

C'est la meme lecon que `grep -c flip` : avant d'ajouter un instrument, chercher s'il existe.

## Les erreurs de mesure de cette session, et leur correction

| erreur | correction |
|---|---|
| `grep -aci flip` comptait les lignes `flips=0` | 107 885 flips inexistants ; regle : extraire la VALEUR |
| histogramme aliasant `(m>>2)&2047` puis `<<2` | « distinct=137 » etait 137 BUCKETS, pas methodes |
| byte-order suppose different | `vm::read32` fait bswap, deja identique |
| dump ajoute alors qu'un probe existait | `PS3BASE_RSXB_PROBE` deja present |

## Ce qui reste

- la **ligne du menu en wasm** : non obtenue ;
- la **capture Chromium** : non obtenue ;
- le wasm flip a ~15-20 fps contre 60 en natif (performance, pas mecanisme).