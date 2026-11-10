# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| Table filtrée → 50,2 Mo | round 143 | oui |
| Le module filtré **casse** à t=77 s | mesure | **oui** |
| `unknown indirect target 0x00039518` | `/tmp/filt.log` | **oui — l'adresse manquante** |

# Le profil est incomplet, comme au round 43

## La mesure

| | table complète | table filtrée |
|---|---|---|
| module | 91,9 Mo | **50,2 Mo** |
| threads | 29 | **21** |
| crash | non | **oui, à t=77 s** |
| `unknown indirect` | 0 | **1** |

L'adresse manquante : **`0x00039518`**, appelée depuis `lr=0x0003a84c`.

## Ce que ça confirme

**Le round 43 avait raison** : un profil est incomplet par nature. Le mien (1 888 entrées, avec les 600
appuis) ne couvre pas encore tout.

## Ce que ça révèle

Un **seul** appel indirect manquant suffit à arrêter le jeu. Le profil doit être **exhaustif** pour le
parcours, pas seulement représentatif.

## La méthode qui marche

La boucle est simple et automatisable :

    1. lier avec le profil courant
    2. lancer ; si `unknown indirect target 0xADDR`, ajouter 0xADDR au profil
    3. répéter jusqu'à ce qu'il n'y ait plus de crash

Chaque itération ajoute **exactement** les adresses nécessaires. C'est une fermeture transitive du profil
par l'échec.

## Ce que ça donne

Avec 1 888 entrées, on atteint t=77 s. Il faudra sans doute quelques dizaines d'adresses de plus pour
couvrir le parcours complet — et le module restera autour de 50-55 Mo, sous les 80.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui (50,2 Mo), mais le module plante** |
| plus grosse fonction sous 1 Mo | non (1 316 ko en filtré) |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |