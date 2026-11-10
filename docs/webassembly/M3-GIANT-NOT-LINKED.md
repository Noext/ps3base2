# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| Table filtrée → 50,2 Mo, plus grosse fonction 1 316 105 o | round 143 | oui |
| `f_0070d138` fait 2 266 097 octets de source | round 139 | oui
| **`f_0070d138` n'est NI dans le profil NI dans la table filtrée** | mesure | **oui — la surprise** |

# La fonction géante n'est même pas liée

## La vérification

| question | réponse |
|---|---|
| `0x70d138` dans le profil (1 890) | **non** |
| entrée dans la table complète | **oui** |
| entrée dans la table filtrée | **non** |

## Ce que ça signifie

**`f_0070d138` n'est jamais exécutée** et **n'est pas liée** dans le module filtré.

La fonction de 1 316 105 octets du module filtré est donc **une autre fonction**, ou une **fusion** produite
par wasm-ld.

## Ce que ça corrige

Le round 139 attribuait la taille de 1,4 Mo à `f_0070d138`. Dans la table **complète**, c'est plausible.

Dans la table **filtrée**, c'est **impossible** : elle n'y est pas.

## Ce qu'il faut mesurer

Le nom de la fonction de 1 316 ko dans le module filtré. wasm-ld fusionne les fonctions équivalentes ; il
faut la table des noms (`name` section) pour l'identifier.

## Pourquoi c'est important

Si c'est une **fusion**, le remède n'est pas de découper `f_0070d138` mais d'**empêcher la fusion**
(`-Wl,--no-merge-data-segments` ou équivalent, ou retirer les noms).

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui (50,2 Mo)** |
| plus grosse fonction sous 1 Mo | non (1 316 ko), **mais ce n'est pas f_0070d138** |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |