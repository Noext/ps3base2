# Le callback est installe ; il pointe sur un mot nul

## Ce que la mesure montre

    [d7store] ea=0x00d7280c size=4 v=0x78f2bc00   -> 0x00bcf278  (le jeu)
    [d7store] ea=0x00d7280c size=4 v=0xf09f1f40   -> 0x401f9ff0  (mon stub)

**`+12` recoit deux ecritures** : celle du jeu (un mot dans `mc`) puis la mienne (dans le FIFO).

## Ce que le crash attend

    ctx.gpr[9] = ld32(r31 + 12)      // callback = 0x401f9ff0
    ctx.gpr[0] = ld32(gpr[9] + 0)    // <- lit 0
    ctx.ctr = gpr[0]
    call_indirect(ctx, base, ctx.ctr)   // cible 0

Le premier mot du callback doit etre une **adresse de code**. J'y ai ecrit `0`, ce qui est exactement la cible
nulle qui fait crasher.

## Ce qu'il faut y mettre

Une fonction liftée qui **compte les commandes RSX** et rend la main — c'est la definition meme du stub que le
prompt demande (point 6 : « le chemin d'affichage est remplace par un stub qui compte les commandes RSX »).

Contrainte : ce doit etre une adresse de **code guest**, pas une fonction hote. Le runtime a une table
d'entrees (`image::table`) ; il faut y ajouter une entree et l'ecrire dans le callback.

## Ce que ce round a etabli

| fait | valeur |
|---|---|
| le crash est `ld32(callback+0)` | **mesure**, `lr=0x5d2c08` |
| `callback` est ecrit par le jeu a `0x00bcf278` | `[d7store]` |
| le jeu n'initialise jamais `0x00bcf278` | **aucune ecriture** dans cette plage |
| mon stub ecrit un callback dans le FIFO | oui, `0x401f9ff0` |
| ce callback contient 0 | **oui** — c'est la cible nulle |

## Ce qui reste

Ecrire une adresse de code valide dans `callback+0`. Deux voies :

1. **une entree de table** pointant vers un corps lifté qui compte et rend la main ;
2. **faire du callback un OPD** vers une fonction existante qui convient.

La premiere est propre, la seconde est plus rapide a essayer.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 737 751 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |