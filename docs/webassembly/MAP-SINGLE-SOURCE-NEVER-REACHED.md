# Le syscall 300 a une seule source, et elle n'est jamais atteinte

## La methode

Le round 124 avait pose des gardes sur cinq fonctions qui referencent la constante 300, et en avait conclu que
le code etait atteint. C'etait une lecture trop grossiere : referencer la constante n'est pas emettre le
syscall.

J'ai cherche la forme exacte que produit le codegen :

    ctx.gpr[11] = 0 + 300;   puis   ctx.cia = <addr>; ppu::syscall(ctx, base);

## Le resultat

Une seule fonction dans tout le code lifte emet ce syscall : f_0021c42c, dans out/ppu/mc_020.cpp.

## Ce que la garde montre

    [mapemit] : AUCUNE occurrence
    menu: 1

f_0021c42c n'est jamais atteinte. Le jeu ne va jamais jusqu'a la fonction qui cree le monde.

## Ce que ca corrige

| round | conclusion | statut |
|---|---|---|
| 124 | le code du map est atteint | faux -- faux positif de recherche par constante |
| 125 | le handler n'est jamais entre | confirme et explique |

Le handler n'est pas entre parce que la fonction qui l'appelle n'est jamais executee.

## La chaine complete, etablie

    le jeu atteint le menu (17 threads d'interface, GetFriendsThreadProc)
      -> il n'execute jamais f_0021c42c
        -> le syscall 300 n'est jamais emis
          -> le handler n'est jamais entre
            -> aucune zone memoire n'est mappee
              -> les threads du monde ne sont jamais crees

## Ce que ca ne dit pas

Pourquoi f_0021c42c n'est pas atteinte. C'est un appel indirect ou conditionnel en amont, non localise.

## Ce que ca donne

Un point d'entree unique et nomme : 0x0021c42c. Toute recherche future doit partir de la.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 5 criteres sur 6 |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |