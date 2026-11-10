# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| La plus grosse fonction s'appelle `f_006fe4d8`, 1 358 012 o | round 144 | oui |
| Son **corps source fait 355 octets**, 6 instructions | mesure | **oui — la contradiction** |
| Aucune option de fusion dans wasm-ld | round 144 | oui |

# Le nom ne correspond pas au corps

## Les deux mesures

| mesure | valeur |
|---|---|
| nom de la plus grosse fonction (section `name`) | `f_006fe4d8` |
| taille dans le `.wasm` | **1 358 012 o** |
| corps source de `f_006fe4d8_body` | **355 o**, 6 instructions |

**Une fonction de 6 instructions ne fait pas 1,3 Mo.**

## Ce que ça signifie

Le nom lu dans la section `name` **n'est pas fiable** pour identifier la fonction :

- soit wasm-ld **réutilise** un nom pour un agrégat ;
- soit l'analyse de la section `name` que j'ai faite **désaligne** les index.

Mon extraction associait taille et nom **par index de fonction**. Si les tailles et les noms ne sont pas
indexés pareil, tout est décalé.

## Ce que je dois corriger

`names.py` lit les tailles dans la section **code** (index séquentiel) et les noms dans la sous-section
**function names** (index explicites). Les deux devraient correspondre — mais je n'ai pas vérifié que la
sous-section 1 contient bien tous les index.

## Ce que ça retire

**Je ne sais pas quelle fonction fait 1 358 012 octets.** Le nom `f_006fe4d8` est douteux.

## Ce qui reste solide

- le module filtré fait **50,2 Mo** (mesuré sur le fichier) ;
- sa plus grosse fonction fait **1 316 105 o** (mesuré dans la section code) ;
- **le nom** de cette fonction reste à établir.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui (50,2 Mo)** |
| plus grosse fonction sous 1 Mo | non (1 316 ko) — **nom non établi** |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |