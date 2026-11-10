# La mesure de la plus grosse fonction n'est pas fiable

## Les trois chiffres

| source | fonction nommée | taille | source de cette fonction |
|---|---|---|---|
| objet `llvm-objdump` | `f_001e1f68_body` | **364 746 o** | 1 032 760 o |
| section `name`, index corrigé | `f_006fe930` | **1 358 012 o** | **2 043 o** |
| position réelle dans la section code | inconnue | **1 358 012 o** | — |

## Ce qui ne s'accorde pas

Une fonction de **2 043 octets de source** ne peut pas produire **1 358 012 octets de code**.

Et la fonction de **1 032 760 octets de source** (`f_001e1f68`) ne fait que **364 746 o** dans son objet.

## Ce que je peux affirmer

1. **La section code du module contient une fonction de 1 358 012 octets** — ce chiffre est mesuré sur le
   binaire par un décodeur LEB128, sans heuristique.
2. **Je ne sais pas laquelle.** Le nom et la taille ne s'apparient pas de façon cohérente.

## Ce que je ne peux pas affirmer

- que `f_006fe930` soit la géante ;
- que `f_001e1f68` soit la géante ;
- que la géante soit du code lifté.

## Ce qui reste solide

| critère | valeur | comment |
|---|---|---|
| module | **49,7 Mo** | mesuré sur le fichier |
| nombre de fonctions | **49 018** | section code |
| **plus grosse fonction** | **1 358 012 o** | section code, position inconnue |
| ligne de log du menu | **émise** | corroborée par 3 threads |

## La conclusion honnête

Le seuil « fonction < 1 Mo » **n'est pas démontré respecté**. La section code contient bien une fonction de
1,36 Mo.

Ce que je ne sais pas, c'est **laquelle** — et mes deux tentatives d'appariement nom/taille donnent des
résultats incohérents avec les sources correspondantes.

C'est la **neuvième** erreur d'instrument de cette campagne, et la deuxième consécutive sur ce même sujet.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui — 49,7 Mo** |
| plus grosse fonction sous 1 Mo | **non démontré** — 1 358 012 o dans la section code |
| ligne de log du menu | **oui, corroborée** |