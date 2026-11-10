# Le filtre de table ne suffit pas

## La contradiction mesurée

| | valeur |
|---|---|
| objet de table lié | **231 379 o** → table **filtrée** |
| fonctions dans le module | **49 018** |
| fonctions de la table complète | 22 685 |

La table est filtrée à 1 890 entrées, mais le module contient **49 018 fonctions** — et même plus que la
table complète.

## Pourquoi

**La table ne fait que référencer.** Les objets `p_mc_*.o` contiennent le code de toutes les fonctions, et
`wasm-ld` les garde parce qu'elles sont **exportées** (visibles) même si la table filtrée ne les pointe plus.

Le filtre retire les entrées de la table, donc le **dispatch indirect** ne les atteint plus — mais leur
**code reste dans le binaire**.

## Ce que ça explique

Pourquoi le module filtré ne perd que 92 → 50 Mo au lieu de tomber à 7 Mo :

| | taille |
|---|---|
| module complet | 91,9 Mo |
| module « filtré » | **49,7 Mo** |
| attendu si le code était réellement retiré | ~7 Mo |

Le gain vient de la table plus petite et du **code non référencé** que le linker élimine *partiellement*,
pas d'une élimination complète.

## La mesure qui le prouve

| module | table | fonctions |
|---|---|---|
| `ps3g.wasm` | complète | **49 018** |
| `ps3fg.wasm` | **filtrée (231 ko)** | **49 018** |

**Même nombre de fonctions.** Le filtre ne change pas le code lié.

## Ce qu'il faudrait

`-Wl,--gc-sections` combiné à `-ffunction-sections`, ou ne compiler **que** les fichiers `mc_*.cpp` des
fonctions retenues. Emscripten n'applique pas `--gc-sections` par défaut ici.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui — 49,7 Mo** |
| plus grosse fonction sous 1 Mo | non — 1 358 012 o (`f_0070d138`) |
| ligne de log du menu | **oui, corroborée** |
| élimination de code mort | **partielle — la table seule ne suffit pas** |

---

# --gc-sections ne change rien non plus

## La mesure

| module | fonctions | code total |
|---|---|---|
| sans `--gc-sections` | **46 150** | 48 952 617 o |
| **avec `--gc-sections`** | **46 150** | **48 952 617 o** |

**Identique.** L'option est acceptée et n'a aucun effet.

## Pourquoi

`--gc-sections` élimine les sections non référencées. Ici, les fonctions sont **exportées** : `wasm-ld` les
considère comme des points d'entrée possibles et ne peut pas les retirer.

Emscripten exporte ces symboles parce que la table de fonctions a besoin d'eux pour le dispatch indirect —
même quand la table filtrée ne les indexe plus.

## Ce qui marcherait

**Ne pas compiler les fichiers qui ne contiennent que des fonctions non retenues.** C'est un changement au
niveau du build, pas du link : compiler seulement les `mc_*.cpp` dont au moins une fonction est dans le profil.

C'est faisable — le profil donne les adresses, les adresses donnent les fichiers — mais il faut une carte
adresse → fichier, que la table ne fournit pas directement.

## Ce que ça clôt

| piste | verdict |
|---|---|
| filtre de table | réduit la table, **pas le code** |
| `-ffunction-sections` | aucun effet |
| `--gc-sections` | **aucun effet** |
| ne compiler que les fichiers utiles | **faisable, non implémenté** |

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui — 49,7 Mo** |
| plus grosse fonction sous 1 Mo | non — 1 358 012 o |
| ligne de log du menu | oui, corroborée |
| élimination de code mort | **non réalisée** — trois options testées, zéro effet |