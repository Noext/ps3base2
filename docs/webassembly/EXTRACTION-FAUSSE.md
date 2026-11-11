# Mon extraction de fonction etait fausse depuis plusieurs rounds

## L'erreur

J'extrayais le corps d'une fonction avec :

    j = s.find('\nstatic void ', i + 10)

Cette borne ne trouve **pas** la fonction suivante quand elle est declaree `void f_xxx(` (sans `static`), ce
qui est le cas des wrappers. Le corps extrait etait donc **tronque**, et j'en ai tire des conclusions fausses
:

| round | conclusion tiree | statut |
|---|---|---|
| 224 | « le corps ne contient aucun appel d'import » | **faux** — il en contient 4 |
| 231 | « le corps ne fait qu'un `st32` » | **faux** — extraction tronquee |

## La correction

    nxt = re.search(r'\n(static )?void f_[0-9a-f]+\(', s[i+10:])

Avec cette borne, `f_33000cc0` fait **5 591 octets, 4 imports, 5 appels** — conforme a ce que `[gcmdep]`
montrait depuis le round 223.

## Ce que les mesures precedentes gardent de valide

Les mesures **runtime** restent valides : elles viennent de probes executes, pas de lecture de source.

| mesure | source | valide |
|---|---|---|
| `0x80010004` au retour | probe | **oui** |
| 4 imports, tous reussis | probe | **oui** |
| structure a moitie remplie | probe | **oui** |
| une seule ecriture, `callback` | probe | **oui** |

Ce qui etait faux, c'est ce que je croyais lire **dans le source**, pas ce que les probes mesuraient.

## Ce que ca change pour la suite

Je dois **relire** `f_33000cc0` avec la bonne borne avant toute nouvelle conclusion sur ce qu'il fait. Le
`st32` unique que j'avais releve n'etait qu'une fraction du corps reel.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 737 751 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |