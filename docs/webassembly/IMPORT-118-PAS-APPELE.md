# Import 118 n'est jamais appele : mon attribution du round 199 est fausse

## Ce que j'ai suppose au round 199

La pile montrait `frame[0] called 0x3300126c from lr 0x002da504`, et j'ai lu dans `mc_028.cpp` :

    ctx.lr = 0x2da504; ppu::call_import(ctx, base, 118);

J'en ai conclu que l'import 118 (`cellGcmGetTiledPitchSize`) etait la source du pointeur nul.

## Ce que la mesure dit

J'ai instrumente **les deux** : le HLE de `cellGcmGetTiledPitchSize` et le retour de l'import 118.

| probe | occurrences |
|---|---|
| `[pitch-probe]` dans le HLE | **0** |
| `[imp118]` au retour de l'import | **0** |
| la chaine dans le `.wasm` | **presente** |

**Ni le HLE ni l'import ne sont appeles.** Le `.wasm` contient bien les deux chaines, donc le code est lie ;
c'est le **chemin d'execution** qui ne les atteint pas.

## Ce que ca signifie

Mon raisonnement au round 199 etait : "le lr est 0x2da504, or a 0x2da504 le source appelle l'import 118, donc
c'est l'import 118". C'est **faux** pour une raison simple : le `lr` enregistre au moment du crash est
**0x5d2c08** (dans `f_005d2b44`), pas `0x2da504`.

`0x2da504` etait le `lr` de **frame[0]**, c'est-a-dire le retour de l'appel **dans ma pile circulaire**, qui
n'est pas la meme chose : ma pile n'enregistre pas tous les appels, seulement ceux passes par `fn(ctx, base)`
dans le dispatcher. Les appels directs entre fonctions liftées ne la traversent pas.

## La troisieme erreur d'instrumentation du round

| erreur | cause |
|---|---|
| round 197 : probe `ld32` absent des TU `mc_*` | `inline` dans un header |
| round 199 : pile incomplete | seuls les appels passant par le dispatcher sont enregistres |
| round 200 : attribution import 118 | confondu le `lr` du crash et celui d'une frame |

Les trois ont la meme racine : **je lis un instrument sans verifier ce qu'il couvre**.

## Ce qui reste vrai du round 199

- Les quatre frames nommees sont exactes (elles ont bien ete appelees).
- Le chemin traverse `mc_028` -> `mc_020` -> `mc_059` -> liblv2 -> libgcm_sys.
- Le crash est `call_indirect(0)` dans `f_005d2b44`, avec `r31=r3=0`.

## La mesure qui manquait

Un **vrai** backtrace : instrumenter `f_005d2b44` par une garde d'entree (elle, fonctionne — c'est une entree
de fonction) et imprimer le `lr` **au moment de l'entree**, pas au moment du crash.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |