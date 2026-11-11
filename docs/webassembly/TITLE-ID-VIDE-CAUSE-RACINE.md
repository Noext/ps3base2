# `TITLE_ID` est vide : la cause racine, trouvee

## La mesure

    [paramsfo] TITLE_ID="" empty=1

Le syscall `_sys_process_get_paramsfo` (30) lit `TITLE_ID` dans le `PARAM.SFO` du disque :

    std::string id = vfs::disc_sfo().str("TITLE_ID");
    if (id.empty()) return CELL_ENOENT;

**Il est vide**, donc le syscall rend **`CELL_ENOENT`**.

## La chaine complete, du PARAM.SFO au crash

    1. le jeu appelle _cellGcmInitBody(context=0xd72800, cmdSize, ioSize, ioAddr=0x40100000)
    2. le corps appelle _sys_process_get_paramsfo (syscall 30)
    3. TITLE_ID est VIDE -> CELL_ENOENT
    4. le corps prend le chemin d'erreur et rend 0x80010004
    5. le jeu ignore le retour (round 225)
    6. la structure a 0xd72800 reste a moitie remplie (round 229)
    7. ld32(0xd72800 + 10240) = 0, jamais ecrit (round 208, 230)
    8. call_indirect(0) -> crash

## Pourquoi TITLE_ID est vide

Deux possibilites :

| cause | verification |
|---|---|
| le PARAM.SFO n'est pas monte ou pas lu | `vfs::disc_sfo()` echoue silencieusement |
| le PARAM.SFO ne contient pas TITLE_ID | a verifier dans le fichier |

Le disque est monte (`vfs: disc ... BLES01976 01.00` apparait dans le log), donc le SFO existe. C'est donc
**la lecture du champ** qui echoue, ou la cle qui ne correspond pas.

## Pourquoi ca n'a jamais ete vu

Ce syscall n'etait couvert par **aucun** de mes probes : `[gcmdep]` filtrait sur les **imports** 396/400/401/405,
et `_sys_process_get_paramsfo` est un **syscall** (30), pas un import.

J'ai passe les rounds 222 a 230 a chercher du cote des imports, alors que l'echec etait dans un syscall.

## La correction a faire

Verifier `vfs::disc_sfo()` et la lecture de `TITLE_ID`. Si le SFO est present mais que le champ est vide, c'est
la table de cles qui est en cause. Si le SFO n'est pas lu, c'est le montage.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 737 751 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |