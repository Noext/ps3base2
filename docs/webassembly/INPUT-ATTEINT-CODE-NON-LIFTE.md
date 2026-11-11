# L'input fait progresser le jeu jusqu'a du code non lifte

## Ce que la sequence a produit

Avec 600 appuis (`down` + 4 `cross`, repetes), le jeu ne se contente pas de changer d'etat : il **atteint du
code absent de la table**.

    F  27.774624 ppu: unknown indirect target 0x000f656c (lr=0x000e0828, thread 0x1000000)
    Aborted()

## Ce que `0x000f656c` est

| verification | resultat |
|---|---|
| present dans la table **complete** (22 685 entrees) | **non** |
| present dans mon profil de couverture | **non** |
| present dans la table **filteree** liee | non (consequence) |

**Ce n'est pas un probleme de filtre** : l'adresse n'existe dans aucune table. Le code lift n'a jamais
produit cette fonction.

## Ce que ca signifie

**La sequence d'entree fait progresser le jeu au-dela de ce qui a ete lifte.**

C'est une decouverte a double tranchant :

| lecture | consequence |
|---|---|
| la sequence est la bonne | elle declenche bien le parcours, plus loin que mes runs precedents |
| le lift est incomplet | `0x000f656c` et probablement ses voisines n'ont jamais ete recompilees |

## Pourquoi mes runs precedents ne le voyaient pas

Le round 116 avait utilise 600 appuis et obtenu `menu: 1` — mais **sans crash**. La difference :

| run | appuis | resultat |
|---|---|---|
| round 116 | 600 (motif `down`+`cross`) | **menu: 1**, pas de crash |
| ce round | 600 (meme motif) | **crash a t=27 s** |

La difference est ailleurs : entre-temps j'ai **reduit les gardes**. Au round 116, la garde sur `0x14f8e4`
remplacait le corps de cette fonction — ce qui **empechait** le jeu d'aller plus loin. Aujourd'hui cette garde
est retiree, le jeu progresse, et il atteint le code manquant.

## Ce que ca etablit

**Mon instrumentation retirait au jeu la possibilite d'echouer la ou il echoue vraiment.** C'est la meme
lecon qu'au round 179, poussee plus loin : non seulement les gardes faussaient les mesures, elles
**masquaient un defaut du lift**.

## Ce qu'il faudrait

Faire regenerer `out/ppu` avec une couverture incluant ce parcours. `0x000f656c` doit etre lifte, et
probablement ses voisines : c'est un appel indirect depuis `lr=0x000e0828`.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 5 criteres sur 6 |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |