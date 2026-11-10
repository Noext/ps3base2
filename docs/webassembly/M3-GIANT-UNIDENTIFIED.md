# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| Ni linker, ni inlining, ni source géant | rounds 144-146 | oui |
| Les plus gros **objets** sont runtime (`r_memory.o` 8,5 Mo) | mesure | oui — mais trompeur |
| **Le runtime seul ne compile qu'en 23 ko** | mesure | **oui — écarté** |

# Le runtime n'est pas la source

## La mesure

| | valeur |
|---|---|
| `r_memory.o` (objet) | **8 525 385 o** |
| `r_ppu_ops.o` (objet) | 8 394 484 o |
| **module liant ces trois plus `codewatch`** | **23 228 o**, 117 fonctions, max 5 369 o |

**Un objet de 8,5 Mo produit 23 ko de code.** Les 8,5 Mo sont des **symboles de debug**, pas du code.

## Ce que ça élimine

| hypothèse | statut |
|---|---|
| la géante vient du runtime | **éliminée** |
| la géante vient des gros objets | **éliminée** (le plus gros objet n'ajoute rien au code) |

## Ce que ça laisse

La géante vient **du code lifté** — mais aucune source ne fait 1,3 Mo, et `-fno-inline` ne la change pas.

## Ce que je n'ai pas pu faire

Le link isolé des objets liftés **échoue** (1 erreur). Je n'ai donc pas identifié l'objet porteur par
bissection.

## Bilan de la recherche

| hypothèse | test | résultat |
|---|---|---|
| fusion par wasm-ld | option inexistante | éliminée |
| inlining `mc_070` | `-fno-inline` | éliminée |
| source liftée géante | mesure | éliminée (max 13 ko) |
| runtime | link isolé | éliminée (23 ko) |
| nom mal attribué | alignement corrigé | éliminée (nom correct) |

**Cinq hypothèses, cinq éliminées.** La fonction reste non identifiée.

## Ce que je recommande

Ne pas poursuivre : le module passe le critère (**49,6 Mo**), une seule fonction sur 48 918 dépasse, et le
prompt demande de **rapporter** dans ce cas plutôt que de s'acharner.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui — 49,6 Mo** |
| plus grosse fonction sous 1 Mo | **non** — 1 316 960 o sur 48 918 fonctions |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |