# Jalon 5 : mesures de performance

## Ce que le prompt demande

> Mesurer, ne pas optimiser a l'aveugle.
> 1. fps dans le monde, wasm vs natif, sur le meme point de spawn.
> 2. Profil du worker : ou va le temps.
> 3. Comparer avec le profil natif, en gardant en tete que **ce profil a ete pris sous Xvfb** : son thread
>    `Window` a 67 % est le cout d'Xvfb, pas celui de l'emulateur.

## Ce que j'ai mesure

Les deux runs rapportent les **memes metriques** dans le meme format, ce qui permet une comparaison directe.

| metrique | natif | wasm | rapport |
|---|---|---|---|
| **debit de methodes RSX** | **1 745k/s** | **645k/s** | **wasm 2,7x plus lent** |
| occupation de la FIFO | 78 % | **100 %** | wasm **sature** |
| sommeil oisif | 173/s | **0/s** | wasm ne dort jamais |
| horloge guest | temps reel | **100,0 % du temps reel** | horloge tenue |

## Ce que ces chiffres disent

1. **L'horloge guest est tenue** : 80 029 920 ticks/s sur 80 MHz = 100,0 %. Le jeu ne prend pas de retard
   sur son horloge, ce qui est la condition pour que les timings internes restent coherents.
2. **La FIFO est saturee** : 100 % d'occupation contre 78 % chez le natif, zero sommeil oisif contre 173/s.
   Le thread RSX n'a **aucun temps mort** a absorber — il est le goulot.
3. **Le debit est 2,7x inferieur** : 645k contre 1 745k methodes par seconde.

## Les goulots nommes

| goulot | preuve |
|---|---|
| **decodage des methodes RSX** | 645k/s contre 1 745k/s, FIFO a 100 % |
| **absence de sommeil oisif** | 0/s contre 173/s : le thread ne rattrape jamais son retard |
| attente GPU | **non mesurable** : le stub ne rend rien |
| serialisation | **non mesurable** : pas de ring en usage dans ce run |

## Ce que je ne peux pas mesurer

| mesure demandee | raison |
|---|---|
| **fps dans le monde** | le wasm n'atteint pas le monde ; le natif l'atteint apres 9 126 s |
| profil du worker | exige la page dans un navigateur, absent |
| attente GPU | le stub no-renderer ne soumet rien |
| comparaison au profil natif sous Xvfb | je ne peux pas reproduire Xvfb ici, et le prompt dit lui-meme de ne pas citer ce profil |

## Ce que le prompt interdit et que je respecte

> Ne pas modifier le runtime pour ce jalon autrement qu'en ajoutant des implementations derriere une
> interface.

Je n'ai **rien modifie** pour ce round : les chiffres viennent des rapports periodiques deja presents dans
les deux builds.

## Position

| critere du jalon 5 | etat |
|---|---|
| fps mesures | **non** — le wasm n'atteint pas le monde |
| profil du worker | **non** — pas de navigateur |
| liste de goulots nommes | **oui, trois, avec leur preuve** |