# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| La plus grosse fonction fait 1 316 105 o | round 143 | oui |
| Aucune source liftée n'approche cette taille | round 145 | oui |
| **Une seule fonction sur 48 918 dépasse 500 ko** | mesure | **oui — cas isolé** |

# Un cas isolé, pas un problème systémique

## La distribution

| | valeur |
|---|---|
| fonctions du code | 48 918 |
| fonctions > **500 ko** | **1** |
| fonctions > 1 Mo | **1** |
| octets de code totaux | 55 881 491 |

## Ce que ça établit

**Une seule fonction** dépasse le seuil. Les 48 917 autres sont sous 500 ko.

Ce n'est pas un problème de génération de code généralisé : c'est **un cas unique**.

## Ce que ça permet

Deux options concrètes, sans toucher au codegen :

1. **Accepter l'écart** : le module passe (50,2 Mo), une fonction sur 48 918 dépasse. Le prompt demande de
   « s'arrêter et rapporter » au-delà des seuils — c'est ce que je fais.
2. **Identifier la fonction** et la découper si elle vient du lift, ou la réduire si elle vient du runtime.

## Ce que je n'ai pas établi

L'origine de cette fonction. Mon attribution par nom est fausse (+90 décalage).

## Ce qui reste solide et suffisant

| critère | seuil | mesuré | verdict |
|---|---|---|---|
| module | 80 Mo | **50,2 Mo** | **PASSÉ** |
| plus grosse fonction | 1 Mo | 1 316 ko | **échoue, 1 fonction sur 48 918** |

## Ce que le prompt demande dans ce cas

« Si le module dépasse ~80 Mo ou une fonction ~1 Mo, s'arrêter et rapporter avant de continuer. »

C'est exactement la situation : je rapporte. Le module passe, une fonction non, et j'en connais la
distribution.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui — 50,2 Mo** |
| plus grosse fonction sous 1 Mo | non — 1 316 ko, **1 fonction sur 48 918** |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |