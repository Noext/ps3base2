# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le diagnostic « check_interrupt n'est pas appelé ou le timer ne satisfait jamais » était **déjà écrit** dans le code | `runtime/src/spu/thread.cpp:141-147` | **oui** — le probe `spuint` imprime les valeurs |
| `interrupt_pending()` sort immédiatement si `!interrupts_enabled` | `runtime/src/spu/thread.cpp:138` | **oui — c'est la réponse** |
| Aucun code n'écrit `0x30028500` | `docs/webassembly/M3-NO-WRITER.md` (round 20) | oui — confirmé ici |
| Le natif parque avec `events 0x20` (EV_TM), le wasm avec `0x0` | `runtime/src/spu/thread.cpp:141` | oui |

# La porte, mesurée

En activant `PS3BASE_SPU_WAIT_PROBE` (le probe existait déjà, non utilisé) :

    spuint: slot 0 ie=0 mask 0x400 events 0x0 count 0 dec_frozen 0
            dec_value 1146405999 elapsed 2347200 raddr 0x00000000
    spuint: slot 0 ie=0 mask 0x400 events 0x0 count 0 dec_frozen 0
            dec_value 1146405999 elapsed 2347200 raddr 0x00000000
    spuwait: slot 0 'MinecraftCellSpursKernel0' pc 0x011ac event_mask 0x400
             events 0x0 raddr 0x30028500 insns 21504   (répété à l'identique)

## Les valeurs qui décident

| champ | valeur | conséquence |
|---|---|---|
| `ie` | **0** | `interrupt_pending()` retourne `false` immédiatement (`thread.cpp:138`) |
| `events` | **0x0** | aucune interruption collectée |
| `dec_value` | 1 146 405 999 | le seuil du décrémenteur |
| `elapsed` | 2 347 200 | **500× plus petit** — le timer ne satisfera jamais dans cette fenêtre |
| `dec_frozen` | 0 | le décrémenteur court, mais trop lentement |
| `raddr` | **0x30028500** | la zone de travail du taskset SPURS |
| `insns` | **21 504, figé** | le SPU ne progresse plus du tout |

## Ce que ça établit

Le SPU est dans `block()`, à `pc 0x011ac`, en attente d'un prédicat. Les trois portes possibles
sont toutes fermées :

1. **`ie = 0`** : les interruptions sont désactivées, donc `interrupt_pending()` ne peut rien
   retourner, quelle que soit la condition.
2. **`events = 0`** : rien n'a été collecté.
3. **Le décrémenteur** : `elapsed` (2,3 M) est 500 fois plus petit que `dec_value` (1,15 Md) —
   même avec `ie = 1`, il ne déclencherait pas.

Le SPU attend donc la seule chose qui reste : que la ligne mémoire à **`raddr = 0x30028500`**
change, ce qui lèverait `EV_LR`.

**Et aucun code n'écrit `0x30028500`** — constat déjà documenté au round 20
(`M3-NO-WRITER.md`), et confirmé ici par l'immobilité de `insns`.

## Pourquoi le natif diffère

Le natif parque avec `events 0x20` (EV_TM, le décrémenteur). Chez lui, le timer a donc fini par
satisfaire — après **287 milliards d'instructions**. Chez le wasm, `insns` est figé à **21 504**
avant que le décrémenteur n'atteigne son seuil.

L'écart de 93 millions vient de là : **le wasm n'a pas le temps d'atteindre le seuil du
décrémenteur avant de se parquer**, et une fois parqué sur `0x30028500` sans écrivain, il ne
repart jamais.

## L'hypothèse que ça suggère

`dec_value = 1 146 405 999` est très grand. Si cette valeur est mal calculée — par exemple parce
que le décrémenteur est initialisé avec une fréquence fausse — le seuil serait inatteignable.

C'est testable : comparer `dec_value` et la fréquence du timebase côté wasm avec ce que le natif
utilise. Mais je ne l'ai pas fait ce round.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **utilise un probe qui existait déjà dans le code** et obtient
les valeurs de la porte qui expliquent l'écart de 93 millions : le SPU se parque avant que le
décrémenteur n'atteigne son seuil, et `ie = 0` ferme la seule autre sortie.
