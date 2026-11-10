# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| Le nom extrait est `f_006fe4d8` | round 144 | douteux |
| Mon corps mesuré faisait 355 o | round 144 | **confirmé : 3 instructions** |
| **49 008 noms pour 48 918 fonctions** | mesure | **oui — le décalage** |

# Le nom est décalé, et aucune source n'approche 1,3 Mo

## Les faits

| élément | mesure |
|---|---|
| fonctions du code | 48 918 |
| noms dans la section `name` | **49 008** |
| imports | **0** |
| **écart** | **+90 noms** |

Le décalage de 90 expliquerait l'erreur d'attribution, mais l'offset ne se corrige pas simplement (0 import).

## Les tailles source

| fonction | source | labels | `goto` |
|---|---|---|---|
| `f_006fe4d8` | 355 o | 1 | 0 |
| `f_006fe150` | 12 961 o | 24 | 38 |

**Aucune ne fait 1,3 Mo.**

## Ce que ça établit

La fonction de 1 358 012 octets dans le `.wasm` **ne correspond à aucune source de cette taille**.

C'est donc du code **produit par le compilateur** — probablement une fonction C++ template ou inline
instanciée massivement, ou une fonction d'un runtime (`libc++`, gestion d'exceptions).

## Ce que ça retire

L'hypothèse « une fonction liftée géante dépasse le seuil » est **non démontrée**. Les plus grosses sources
liftées font ~13 ko.

La taille de 1,3 Mo vient d'ailleurs — peut-être une fonction d'**exception handling** générée par Emscripten.

## Ce qu'il faut mesurer

Identifier la fonction par son contenu, pas par un nom : chercher dans le `.wasm` les octets de la fonction
d'index 11905 et voir à quel module elle ressemble.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui (50,2 Mo)** |
| plus grosse fonction sous 1 Mo | non (1 316 ko) — **d'origine non liftée probable** |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |