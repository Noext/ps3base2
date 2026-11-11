# Tous les symboles sont definis ; `unreachable` persiste

## Ce que j'ai fait

J'ai fourni des definitions inertes pour les **16 symboles non definis** que le lien remplacait par des
trampolines abortifs. Apres trois passes de correction de signatures (les noms mangles donnent les types exacts),
le lien ne signale plus **aucun** symbole manquant :

    undefined=0

Et le `RuntimeError: unreachable` **persiste**.

## Ce que ca etablit

| hypothese | statut |
|---|---|
| le `unreachable` vient d'un trampoline de symbole manquant | **ecarte** |
| les 16 symboles etaient la cause | **non** |

C'est un resultat negatif utile : la piste documentee dans `docs/webassembly` (« les symboles manquants
produisent des `unreachable` nus ») **ne s'applique pas ici**.

## Ce qui reste a explorer

Le boot s'arrete apres le demarrage des threads SPURS. Trois pistes, aucune essayee :

| piste | cout |
|---|---|
| le bloc SPU `0x848` appelle `0x337fd480`, hors de la LS | trace `op_brsl` |
| un `ppu::syscall` non implemente rend un code que le jeu traite en erreur | liste des syscalls |
| l'interpreteur SPU de secours s'arrete sur une instruction inconnue | log `spujob` |

## Ce que le round a produit de durable

**16 definitions inertes** dans le fichier de stubs : elles ne changent pas le comportement, mais elles
suppriment une source d'`unreachable` nus pour toutes les analyses futures. Le fichier est maintenant complet.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; blocage PPU resolu (round 241), blocage SPU ensuite |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |