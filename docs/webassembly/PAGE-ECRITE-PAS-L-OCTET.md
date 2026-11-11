# La page est ecrite, mais pas l'octet qu'on lit

## La mesure

Un probe dans `rsv::store_range` — le point unique de **toutes** les ecritures guest — filtre sur la plage
`0x00d70000`-`0x00d73000` :

    [d7store] ea=0x00d729e8 size=4 v=0x00000000
    [d7store] ea=0x00d729f8 size=4 v=0xffffffff
    [d7store] ea=0x00d72a00 size=4 v=0x12000000
    [d7store] ea=0x00d72a08 size=4 v=0x002b0095
    ... 21 ecritures au total

## Ce que ca etablit

| fait | valeur |
|---|---|
| premiere adresse ecrite | **`0x00d729e8`** |
| derniere adresse ecrite | `0x00d72a58` |
| ecritures a `0x00d72800` | **0** |
| ecart entre le champ lu et la zone ecrite | **504 octets** (`0x1f8`) |

**La page est reellement ecrite** — ce n'est donc pas une zone morte. Mais la structure ecrite commence a
`0x00d729e8`, et le champ lu est a `0x00d72800`, **avant** elle.

## Ce que ca signifie

`0x00d72800` et `0x00d729e8` sont dans la **meme page** mais dans **deux structures differentes** :

| structure | debut | contenu |
|---|---|---|
| celle qu'on lit | `0x00d72800` | **jamais ecrite** |
| celle qui est ecrite | `0x00d729e8` | 21 mots, dont `0x002b0095` (un code d'operation) |

Le jeu lit un pointeur dans une structure qu'il **n'a jamais remplie**, alors qu'il en remplit une autre 504
octets plus loin.

## Les hypotheses que ca laisse

| hypothese | plausibilite |
|---|---|
| le lift a manque un bloc qui ecrit `0x00d72800` | possible — la decouverte indirecte le dirait |
| la structure est remplie par un DMA RSX | possible — `0x00d72800` est sous `0x10000000` |
| le jeu attend une allocation a une adresse plus basse | **non** — la page est mappee, mesure du round 208 |
| c'est un bug du jeu invisible sur console | peu probable — le natif fonctionne |

## Ce qui rend cette mesure decisive

C'est la **premiere fois** qu'une ecriture est observee dans cette page. Les rounds 208 et 219 concluaient
« aucun code n'ecrit ce champ » — c'etait vrai **pour le champ**, mais la page entiere n'etait pas morte pour
autant. La distinction n'avait pas ete faite.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |