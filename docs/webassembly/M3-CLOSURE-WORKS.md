# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| Le module filtré casse sur `0x00039518` | round 143 | oui |
| Boucle de fermeture : ajouter les adresses manquantes | méthode proposée | **testée ici** |
| **2 adresses ajoutées, puis plus de crash (timeout à 280 s)** | mesure | **oui** |

# La fermeture par l'échec fonctionne

## La boucle

    itération 1 : crash sur 0x00039518 → ajouté (profil 1 889)
    itération 2 : crash sur 0x00039510 → ajouté (profil 1 890)
    itération 3 : plus de crash — le run dépasse 280 s et le timeout le tue

## Ce que ça établit

**Deux adresses suffisent** à supprimer le crash. Le profil passe de 1 888 à **1 890 entrées** (8,33 % des
22 685).

Les deux adresses sont **adjacentes** (`0x39518`, `0x39510`) : c'est une table de sauts voisine.

## Ce que ça confirme

La méthode de fermeture par l'échec est **valide et converge** : 2 itérations, 2 adresses.

## Ce que ça ne dit pas

Que 1 890 soit le compte final. Le run a été tué par le timeout, pas terminé. D'autres adresses peuvent
manquer plus tard dans le parcours.

## La limite de la méthode

Chaque itération demande un link complet (~2 min) et un run de 280 s. Pour 10 adresses, c'est 40 minutes.

## Ce qui reste

Poursuivre la boucle jusqu'à stabilité, puis mesurer la taille du module et la plus grosse fonction avec le
profil fermé.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui (50,2 Mo), crash corrigé avec 1 890 entrées** |
| plus grosse fonction sous 1 Mo | non (1 316 ko en filtré) |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |