# La decouverte indirecte ne trouve rien : le lift n'a pas de trou

## Ce que j'ai lance

Le build avec `-DPS3BASE_DISCOVERY_FILE`, qui enregistre toute cible d'appel indirect que le lift n'a pas
produite, puis s'arrete proprement (code 3).

    discovery lines: 0
    aucun fichier /tmp/discovery234.txt

**Zero cible manquante.**

## Ce que ca etablit

| fait | valeur |
|---|---|
| appels indirects vers une fonction non liftee | **aucun** |
| le lift est complet sur ce chemin | **oui** |

C'est un resultat **negatif mais decisif** : la piste du round 233 — « les trois champs sont ecrits par du code
non lifte » — est **fermee**. Il n'y a pas de code manquant a lifter.

## Ce qui reste comme explication

Si le lift est complet et que `begin`/`end`/`current` ne sont pas ecrits, alors :

| explication | statut |
|---|---|
| du code non lifte les ecrit | **ecarte** — decouverte vide |
| une branche conditionnelle n'est pas prise | **plausible** — c'est ce que la trace du round 224 suggerait |
| le jeu ne les ecrit jamais, et le firmware doit le faire | **plausible** — mais le firmware ne le fait pas non plus |

## L'etat du run avec discovery

`videoimp: 0` — le define de decouverte casse l'init video, comme les gardes. Le fichier de seed n'est jamais
ecrit, ce qui est **coherent avec « aucune decouverte »** : le runtime n'a pas eu a s'arreter.

## Ce que ce round a coute

Un round pour fermer une piste. C'est le prix d'une hypothese non testee depuis 47 rounds (la decouverte n'avait
pas ete relancee depuis le round 187), et le resultat est net.

## Ce qu'il faudrait maintenant

Revenir a la trace du round 224 : elle montrait 48 decisions de branchement dans `f_33000cc0`, dont une sequence
`bo=16 bi=0` repetee 7 fois (une boucle sur une chaine) et six `bo=4 bi=30` (tests de valeur nulle).

Le chemin qui mene au `return` d'erreur est **dans ces 48 decisions**, et je n'ai pas encore correle leur ordre
avec les `return` du source.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 737 751 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |