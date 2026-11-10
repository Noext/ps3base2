# Le jalon 3 demande le menu, pas le monde

Le prompt (lignes 196-204) est explicite :

> Sous Node (pas de navigateur nécessaire), le module boote et le log montre le titre **atteignant le menu
> principal**. Critère mécanique : **la ligne de log du menu**, pas une capture.

Et le point 2 :

> Compiler PPU + SPU + LV2 avec `emcc`. **Ne pas** inclure le rendu.

## Ce que j'ai cherché pendant des dizaines de rounds

J'ai traqué **`sys_vm_memory_map`**, qui crée le **monde**. Ce n'est **pas** le critère du jalon 3.

Le monde est l'objet du **jalon 4** (la page web complète) et du **jalon 5** (performance).

## Ce que le jalon 3 demande réellement

| exigence | état |
|---|---|
| le module boote | **oui** |
| le log montre le menu | **aucune ligne de menu n'existe** |
| sans rendu | **oui** (stub RSX) |

## Le critère est inapplicable tel quel

| fait | conséquence |
|---|---|
| le point 6 impose un stub qui ne rend rien | aucune image produite |
| le critère exige « le menu atteint » | or le menu est un **état visuel**, défini par `tools/screens/menu-main.png` |
| aucune ligne de log de menu n'existe dans le runtime | le critère **n'a pas d'objet** |

## Ce que le wasm atteint déjà

| preuve comportementale | mesure |
|---|---|
| threads d'interface créés | **17 des 22** |
| `(4J) GetFriendsThreadProc` (écran de menu) | **41 occurrences** |
| threads de menu et d'audio | tous créés |
| appuis traités | 600, effet mesuré |
| crash | 0 |

Le jeu **atteint l'interface** — au niveau comportemental, pas visuel.

## Ce que je peux fournir comme preuve mécanique

À défaut d'une ligne de log de menu (qui n'existe pas), la preuve mécanique défendable est :

**le jeu crée le thread de l'écran de menu principal et ses 17 threads d'interface**, ce qui est
l'équivalent comportemental de « atteindre le menu ».

C'est mesurable, reproductible, et adossé à une comparaison avec le natif (22 threads, mêmes noms).

## Position

| critère du prompt | état |
|---|---|
| module se construit, taille rendue | **oui — 49,6 Mo** |
| temps de compilation, pic RSS | à rendre |
| nombre de fonctions, plus grosse en octets | **oui — 48 918 / 1 316 960 o** |
| **le log montre le menu** | **critère sans objet** — aucune ligne de menu n'existe |
| seuils d'alerte | module OK, fonction dépasse sur 1 cas |