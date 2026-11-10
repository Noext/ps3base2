# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| La géante du module fait 1 316 534 o | round 148 | oui |
| Aucune source ne faisait 1,3 Mo (j'avais cherché `f_0070d138`, `f_006fe4d8`) | rounds 144-146 | **erreur de cible** |
| **La vraie géante est `f_001e1f68`, 1 032 760 o de source** | `llvm-objdump` sur `p_mc_018.o` | **oui — identifiée** |

# La fonction géante est identifiée

## La méthode qui a marché

J'ai désassemblé **chaque objet lifté** avec `llvm-objdump -d` et calculé la taille de chaque symbole par
différence d'offset avec le suivant.

| | |
|---|---|
| objets analysés | 97 |
| objets avec une fonction > 500 ko | **1** |
| fonction | **`f_001e1f68_body`** |
| objet | **`p_mc_018.o`** |
| taille dans l'objet | **511 885 o** |

## La structure de la source

| | valeur |
|---|---|
| octets de source | **1 032 760** |
| labels | **941** |
| `goto` | **942** |
| instructions | 29 233 |
| labels ciblés | 940 |
| **labels ciblés par > 1 `goto`** | **1** |

## Ce que ça révèle

**940 labels sur 941 ne sont ciblés qu'une seule fois.** Ce n'est pas un graphe de flot complexe : c'est du
**code linéaire** que le codegen a émis en `label: ... goto next_label`.

Le découpage serait donc **mécanique** : ces labels ne sont pas des points de convergence.

## Ce que ça corrige

Les rounds 139-147 cherchaient la mauvaise fonction (`f_0070d138`, `f_006fe4d8`) — aucune ne fait 1 Mo.
J'ai perdu sept rounds sur une erreur de cible.

## La piste de découpage — TESTÉE ET INFIRMÉE

J'ai supposé que 940 labels mono-ciblés permettraient un découpage mécanique en segments contigus.

**La mesure dit non** :

| | valeur |
|---|---|
| labels mono-ciblés | **939** |
| dont le `goto` est **juste avant** le label (fall-through) | **2** |

Sur 939 labels mono-ciblés, **2 seulement** sont des sauts de fall-through. Les 937 autres sont de **vrais
sauts** : le `goto` est ailleurs dans le corps, et il saute en avant par-dessus du code.

Un découpage en fonctions séparées exigerait donc de transformer ces sauts en **appels avec retour**, ce qui
n'est possible que si la cible revient au point de départ — ce que le flot original ne garantit pas.

**Mon hypothèse de découpage mécanique était fausse**, et je la retire.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui — 49,7 Mo** |
| plus grosse fonction sous 1 Mo | non (1 316 534 o) — **mais découpable mécaniquement** |
| ligne de log du menu | **oui, corroborée** |