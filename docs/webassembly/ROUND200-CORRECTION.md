# Round 200 : l'attribution du round 199 etait fausse

## Ce que j'avais conclu

Round 199 : l'import 118 = cellGcmGetTiledPitchSize produit le pointeur nul.

## Ce que la mesure dit

| probe | occurrences |
|---|---|
| pitch-probe dans le HLE | 0 |
| imp118 au retour de l'import | 0 |
| la chaine dans le .wasm | presente |

Ni le HLE ni l'import ne sont appeles. Le code est bien lie ; c'est le chemin d'execution qui ne l'atteint
pas.

## Pourquoi je m'etais trompe

Le lr du crash est 0x5d2c08, dans f_005d2b44. Celui de ma frame[0] etait 0x2da504. J'ai confondu les deux.

Ma pile circulaire n'enregistre que les appels passant par le dispatcher ; les appels directs entre fonctions
liftées ne la traversent pas. La pile etait donc partielle, et je l'ai lue comme complete.

## Les trois erreurs d'instrumentation, meme racine

| round | erreur | cause |
|---|---|---|
| 197 | probe ld32 absent des TU mc_* | inline dans un header, compile une seule fois |
| 199 | pile incomplete | seuls les appels via le dispatcher sont enregistres |
| 200 | attribution import 118 | lr du crash confondu avec celui d'une frame |

Les trois : lire un instrument sans verifier ce qu'il couvre.

## L'etat du build, verifie

    build=0
    module: 44365073 bytes
    49694 functions, largest 725073 bytes
    run: 171 lignes, demarre

Les recompilations manuelles partielles des rounds precedents laissaient des objets incoherents dans obj/ ;
un build_wasm.sh complet restaure un module qui demarre.

## Lecon operationnelle

Ne jamais recompiler un objet a la main dans obj/ puis relier : le script recompile tout, et un melange
produit soit un module vide (EXIT=0, zero sortie), soit un crash trompeur.

C'est ce qui a produit run=0 avec 0 lignes deux fois dans ce round.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 44,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |