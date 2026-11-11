# Le stub HLE n'est pas atteint, malgre `ret=0x0`

## Ce que j'ai fait

1. Route `cellGcmSys` vers le HLE avant le LLE dans `resolve()` (`PS3BASE_HLE_OVER_LLE`).
2. Reecrit le HLE de `_cellGcmInitBody` pour qu'il **remplisse** le contexte (`begin`, `end`, `current`).

## Ce que la mesure montre

    [imp119] _cellGcmInitBody called ... r3=0x0 r4=0xfa000 ret=0x0

Le retour est **`0x0`** au lieu de `0x80010004` : **quelque chose a change**. Mais :

    [gcmargs] mem[0xd72800]=0x00000000 0x00000000 0x00000000 0x00bcf278
    aucun log 'cellGcmInitBody stub'
    aucun log d'avertissement
    unknown indirect: 1

**Le stub que j'ai ecrit n'est pas execute.** Ni la branche de succes (qui ecrirait `begin`), ni
l'avertissement (qui dirait pourquoi).

## Ce que ca implique

| fait | consequence |
|---|---|
| `ret=0x0` | un handler HLE **different** repond |
| pas de trace du mien | mon code n'est pas celui qui s'execute |
| `cellGcmSys.cpp:83` contient pourtant mon stub | il est compile ou pas ? |

L'explication la plus probable : **deux definitions de `_cellGcmInitBody`** existent, et celle qui
l'emporte n'est pas celle que j'ai editee. Le fichier a un `HLE_FUNC` a la ligne 83 et mon commentaire est
bien la — mais l'enregistrement dans la table HLE peut pointer ailleurs, ou un autre fichier definit le meme
nom.

## Ce que ce round a produit

| produit | statut |
|---|---|
| routage HLE-over-LLE | ecrit, effet visible sur le retour |
| stub qui remplit le contexte | **ecrit, non execute** |
| crash | **non resolu** |

## Ce qu'il faudrait au round suivant

Verifier combien de definitions de `_cellGcmInitBody` existent dans `runtime/src/`, et laquelle est
enregistree. C'est une recherche de quelques secondes que j'aurais du faire avant d'ecrire le stub.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 737 751 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |