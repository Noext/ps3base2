# Le champ est nul, mais la page EST mappee : mon diagnostic du round 206 etait faux

## La mesure

Un probe au point de defaillance, sur l'adresse exacte que l'appelant lit :

    [read-d72800] page_mapped=1 value=0x00000000

| fait | valeur |
|---|---|
| la page de `0x00d72800` est-elle mappee ? | **oui** |
| la valeur lue | **0** |

## Ce que ca invalide

| affirmation du round 206 | statut |
|---|---|
| « la region `0x00d70000` n'est pas mappee » | **faux** — elle l'est |
| « le backend memoire masque un acces invalide » | **faux pour ce cas** — l'acces est valide |
| « le `calloc` rend toute adresse mappee » | **faux** — `g_pagemap` est un vrai tableau, seules les pages allouees sont marquees |

Le pagemap est reel : `uint8_t g_pagemap[0x100000]`, marque par `block_covers_locked` a l'allocation. Ma
description du backend au round 206 etait une simplification fausse.

## Ce que le fait etablit

La page existe. Le champ a l'offset 10240 de la structure vaut **zero**, et :

| verification | resultat |
|---|---|
| ecritures `st32(..., rX + 10240)` dans les 100 fichiers | **0** |
| occurrence de `0xd72800` en ecriture | **0** |
| occurrence de `0xd70000` (sauf la lecture) | **0** |

**Aucun code lifte n'ecrit ce champ.** Ce n'est pas un probleme d'allocation, c'est du **contenu manquant** :
une structure que le jeu remplit par un chemin non lifté, ou par un transfert SPU/RSX.

## Les trois lectures possibles

| lecture | comment la trancher |
|---|---|
| le champ est ecrit par du code non lifte | la decouverte indirecte le revelerait |
| le champ est rempli par un DMA RSX | chercher si `0x00d70000` est dans une plage envoyee au GCM |
| le champ est rempli par une tache SPU | chercher une ecriture SPU vers cette adresse |

## Ce que le controle ajoute quand meme

Le `[UNMAPPED]` du round 207 reste utile : il nommera toute cible **reellement** non mappee. Il n'a rien
trouve d'autre que `0x00000000`, ce qui est un resultat : il n'y a **pas** de liste d'allocations manquantes.

## Ma troisieme retractation sur ce meme blocage

| round | conclusion | statut |
|---|---|---|
| 196 | region non mappee | **faux** — mesure du round 208 |
| 206 | le backend cache tout acces invalide | **faux** — le pagemap est reel |
| 208 | champ nul dans une page mappee | **mesure directe** |

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 44,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |