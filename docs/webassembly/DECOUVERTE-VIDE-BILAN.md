# La decouverte indirecte : resultat negatif, et un run qui reste casse

## Le resultat de la piste

    discovery lines: 0
    aucun fichier de seed ecrit

**Zero cible d'appel indirect manquante.** Le lift est complet sur ce chemin.

| conclusion | statut |
|---|---|
| « les trois champs sont ecrits par du code non lifte » | **ferme** — il n'y a pas de code manquant |
| le lift a un trou sur ce chemin | **non** — la decouverte n'en trouve aucun |

C'est un resultat **negatif decisif** : il elimine la derniere piste « du code manque ».

## L'etat du run

Apres retrait du define de decouverte, le run reste casse :

    gcminit: 1
    videoimp: 0
    RuntimeError: unreachable

`gcminit` apparait mais l'init video ne demarre plus. Le `unreachable` vient d'un des **16 symboles non
definis** que le lien remplace par des trampolines abortifs.

## Ce que ce round a coute et rapporte

| | |
|---|---|
| coute | un round pour fermer une hypothese vieille de 47 rounds |
| rapporte | l'elimination definitive de « du code manque » |

## Ce qui reste, et c'est desormais etroit

Trois faits measures qui ne se reconcilient pas encore :

| fait | source |
|---|---|
| le lift est complet | decouverte vide, ce round |
| trois champs au-dela de `+0x08` sont ecrits, les trois premiers non | probe `[d7store]`, round 233 |
| le corps GCM rend `0x80010004` sans qu'aucun import n'echoue | probes rounds 221-223 |

L'explication la plus simple : **le firmver GCM lui-meme n'ecrit pas ces champs**, et c'est notre runtime qui
doit les fournir — parce que sur console ils le sont deja quand le corps s'execute. C'est-a-dire que le corps
attend un etat que notre chargeur ne met pas en place.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 737 751 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |