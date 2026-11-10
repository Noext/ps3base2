# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| Natif 16 ouvertures d'archive après t=100, wasm 0 | round 135 | oui |
| Hypothèse : lecture asynchrone bloquée | round 135 | **précisée ici** |
| **natif 8 fichiers musicaux, wasm 1 ; 391 autres fichiers identiques** | mesure | **oui — l'écart exact** |

# Ce qui diffère, exactement

## La mesure

| | natif | wasm |
|---|---|---|
| fichiers musicaux (`.binka`) | **8** | **1** |
| ouvertures de `MediaPS3.arc` | 49 | 34 |
| **autres fichiers** | **391** | **391** |

Manquants au wasm :

    calm1.binka  calm3.binka  hal1.binka  hal3.binka
    hal4.binka  nuance1.binka  piano1.binka

Le wasm a ouvert `hal2.binka` seulement.

## Ce que ça établit

**Les 391 autres fichiers sont identiques.** Le chargement des ressources est complet des deux côtés.

La seule différence est le **flux musical** et l'**archive média**.

## Ce que ça élimine

L'hypothèse « les ressources manquent » est **définitivement éliminée** : 391 fichiers identiques.

## Ce que ça signifie

La musique est **streamée** : un fichier à la fois, joué puis remplacé. Le natif en a joué 8 ; le wasm en a
joué 1 et s'est arrêté.

C'est cohérent avec un **thread audio** qui ne redemande pas de piste.

## La piste

Le système de musique est asynchrone : il demande une piste, la joue, puis en demande une autre. Si le wasm
ne redemande pas, c'est le même schéma que les 12 attentes de drapeau contre 219 207.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |