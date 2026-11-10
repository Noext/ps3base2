# La fonction géante est identifiée : f_0070d138

## La cause du décalage, enfin

Le module contient **90 fonctions importées**, qui occupent les index **0 à 89** de l'espace des fonctions.
Les corps commencent à l'index **90**.

    import funcs: 90   declared: 49018   bodies: 49018   names: 49108
    90 + 49018 = 49108  ✓

Le nom correct d'un corps d'index `i` est donc `names[i + 90]`.

## La mesure

| | valeur |
|---|---|
| plus grand corps | **1 358 012 o**, index **11 945** |
| nom à l'index 11 945 | `f_006fe930` — **faux** |
| **nom à l'index 11 945 + 90 = 12 035** | **`f_0070d138_body`** |

## La confirmation par la source

| | valeur |
|---|---|
| fichier | `out/ppu/mc_071.cpp` |
| octets de source | **2 266 097** |
| labels | **2 083** |
| `goto` | **2 509** |
| instructions | 51 815 |
| **taille dans le module** | **1 358 012 o** |

Une source de 2,27 Mo produisant 1,36 Mo de code : **cohérent**.

## Ce que ça règle

**C'était ma toute première identification, au round 139.** Elle était juste, et je l'ai abandonnée aux
rounds 144-147 en cherchant dans un seul fichier, puis en corrigeant un décalage que j'avais mal quantifié.

| round | conclusion | statut |
|---|---|---|
| **139** | **`f_0070d138`, 1,4 Mo** | **CORRECT** |
| 144 | « pas liée en table filtrée » | correct pour la table filtrée seulement |
| 146 | « nom mal attribué, +90 » | correct, mais je n'ai pas appliqué le décalage |
| 154 | « artefact, 8 964 o » | **faux** — j'avais inversé le décalage |
| **155** | **confirmé : 1 358 012 o** | **définitif** |

## Ce que ça donne

| critère | seuil | mesuré | verdict |
|---|---|---|---|
| module | 80 Mo | **49,7 Mo** | **PASSÉ** |
| plus grosse fonction | 1 Mo | **1 358 012 o** | **échoue — 1 fonction** |

## Ce que le prompt demande ici

> Si le module dépasse ~80 Mo **ou une fonction dépasse ~1 Mo de code, s'arrêter et rapporter** avant de
> continuer.

C'est la situation : je rapporte, avec la fonction **nommée** et sa **source** identifiée.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui — 49,7 Mo** |
| plus grosse fonction sous 1 Mo | **non — `f_0070d138`, 1 358 012 o, source 2,27 Mo** |
| ligne de log du menu | **oui, corroborée** |