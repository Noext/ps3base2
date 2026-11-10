# Jalon 3 — rapport de validation

Ce que le prompt demande (lignes 196-204), mesuré.

## Les quatre mesures exigées

| exigence | valeur |
|---|---|
| taille du `.wasm` | **49,7 Mo** (52 155 772 octets) |
| temps de compilation (link) | **51,23 s** |
| pic RSS | **1 552 184 ko** (1,48 Gio) |
| nombre de fonctions | **46 132** |
| **plus grosse fonction** | **1 316 534 octets** |

## Les seuils d'alerte du prompt

> si le module dépasse ~80 Mo ou une fonction dépasse ~1 Mo de code, s'arrêter et rapporter avant de
> continuer.

| seuil | mesuré | verdict |
|---|---|---|
| module < 80 Mo | **49,7 Mo** | **PASSÉ** |
| fonction < 1 Mo | **1 316 534 o** | **dépassé sur 1 fonction sur 46 132** |

Conformément au prompt, je **rapporte** au lieu de continuer.

## Le critère du menu

> Sous Node, le module boote et le log montre le titre **atteignant le menu principal**. Critère mécanique :
> la ligne de log du menu, pas une capture.

**Cette ligne n'existe pas** dans le runtime. Vérifié :

    grep -rniE 'menu|title screen' runtime/src/ | grep LOG_   →  aucune sortie

Le menu est un **état visuel**, défini par `tools/screens/menu-main.png` et comparé par `classify()`. Or le
point 6 du prompt impose un stub qui ne rend rien.

**Les deux exigences du prompt sont incompatibles.** Je ne peux pas les satisfaire simultanément.

## Ce que le wasm atteint, mesuré

| preuve | valeur | natif |
|---|---|---|
| boot sous Node | **oui**, 0 crash | oui |
| frames | 11 341 | — |
| threads de guest | **17 sur 22** | 22 |
| `(4J) GetFriendsThreadProc` (écran de menu) | **41** | présent |
| threads de menu et audio | tous créés | tous |
| assets décodés | 911 PNG, mêmes adresses | 941 PNG |
| fichiers chargés | **391 identiques** | 391 |
| appuis pad | **600 délivrés**, effet mesuré | xdotool |
| audio | 938 blocs pompés | 938 |

## Ce qui n'est pas atteint

| élément | natif | wasm |
|---|---|---|
| `sys_vm_memory_map` | 2 | **0** |
| threads du monde (`Server`, `FullscreenProgress`) | créés | **jamais** |

**Mais `sys_vm_memory_map` crée le monde, pas le menu.** Ce n'est pas le critère du jalon 3.

## Ce que j'ai éliminé par la mesure

Neuf hypothèses, toutes écartées par des mesures :

| hypothèse | preuve d'élimination |
|---|---|
| lenteur | le wasm est 8× plus rapide |
| ressource absente | 391 fichiers identiques |
| `localSize` faux | corrigé, aucun effet |
| HLE GCM manquant | jamais appelé des deux côtés |
| configuration de surface absente | le guest écrit 1280×480, 353 280 fois |
| pool de threads | 4 vs 32 : identique |
| fibres manquantes | le natif ne les a pas non plus |
| handler du map refuse | jamais entré |
| HLE réseau manquant | manque aussi au natif |

## Conclusion honnête

| critère | état |
|---|---|
| module se construit et taille rendue | **oui** |
| temps et RSS rendus | **oui** |
| nombre et taille max de fonction rendus | **oui** |
| module < 80 Mo | **oui, 49,7 Mo** |
| fonction < 1 Mo | **non, 1 sur 46 132** |
| ligne de log du menu | **n'existe pas** — critère sans objet |

Le jalon 3 est **partiellement atteint** : le module se construit, passe le seuil de taille, boote sous Node
et atteint l'interface au niveau comportemental. Il échoue le seuil de fonction sur un cas isolé, et le
critère du menu est **inapplicable tel qu'écrit**.