# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| Le mot `0xbe0704` est stable par palier | round 132 | oui |
| J'avais conclu « il change une fois » | round 132 | **incomplet — 3 transitions ici** |

# La capture des transitions

## La mesure

    [transition] t=119s 0xbe0704 1 -> 0
    [transition] t=199s 0xbe0704 0 -> 1
    [transition] t=239s 0xbe0704 1 -> 0

## Les intervalles

| intervalle | durée |
|---|---|
| t=0 → 119 | 119 s à 1 |
| t=119 → 199 | **80 s à 0** |
| t=199 → 239 | **40 s à 1** |

## Ce que ça établit

Le mot **bascule**, mais à l'échelle de **dizaines de secondes**. C'est un **état de jeu**, pas une horloge de
frame.

Et la boucle `f_002dc238` le lit : quand il vaut 1, la boucle tourne ; quand il vaut 0, elle sort.

## Ce que ça corrige

Le round 132 disait « il change une fois ». **Il change au moins trois fois** sur 240 s.

## Ce que ça signifie

Le jeu **alterne entre deux états**, à des intervalles de 40 à 80 s. C'est cohérent avec :

- des **phases** (chargement / attente / traitement) ;
- des **timeouts** (attente réseau, sauvegarde) qui expirent.

## Ce que je n'ai pas établi

Ce qui, dans le jeu, provoque chaque bascule. C'est corrélé, pas expliqué.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |