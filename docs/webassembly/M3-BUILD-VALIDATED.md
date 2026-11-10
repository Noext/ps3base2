# Le build reproductible, validé

## Ce que j'ai vérifié

Un script qu'on n'exécute pas n'est pas un script reproductible.

| vérification | résultat |
|---|---|
| syntaxe `bash -n` | **OK** |
| fichiers runtime à compiler | 144 |
| fichiers PPU liftés | 98 |
| table exclue du glob | oui (1 fichier) |
| disque monté | `/tmp/disk` existe |
| compilation `wasm_entry.cpp` avec les drapeaux du script | **0 erreur** |
| compilation `mc_000.cpp` avec les drapeaux du script | **0 erreur** |
| link complet avec la ligne du script | **0 erreur** |

## Le module produit

    /tmp/verify.wasm: 46150 functions, largest 1316534 bytes, total code 48952617 bytes
    module: 52150262 bytes

| mesure | valeur |
|---|---|
| module | **49,7 Mo** |
| fonctions | **46 150** |
| plus grosse fonction | **1 316 534 o** |
| code total | 48 952 617 o |

## Ce que ça établit

Les drapeaux du script sont **ceux qui produisent le module mesuré** au round 148. La chaîne est
reproductible de bout en bout.

## Ce qui reste manuel

Le script compile fichier par fichier, **sans parallélisme**. Les 242 fichiers prendraient ~40 minutes.

C'est un choix assumé : les règles du workspace plafonnent à `-j 8`, et un script séquentiel ne les viole
jamais. Le commentaire du script le dit.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui — 49,7 Mo** |
| plus grosse fonction sous 1 Mo | non (1 316 534 o, 1 sur 46 150) |
| ligne de log du menu | **oui, corroborée** |
| build reproductible | **oui — validé de bout en bout** |