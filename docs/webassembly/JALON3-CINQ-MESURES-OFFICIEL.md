# Jalon 3 : les cinq mesures, sur le build officiel

## Les criteres du prompt

> Le module .wasm se construit ; rendre sa taille, le temps de compilation total, et le pic RSS.
> Rendre le nombre de fonctions produites, et la plus grosse fonction generee en octets.
> Sous Node, le module boote et le log montre le titre atteignant le menu principal.
> Seuils d'alerte : module > ~80 Mo ou fonction > ~1 Mo, s'arreter et rapporter.

## Les mesures

| critere | seuil | mesure | verdict |
|---|---|---|---|
| taille du module | < 80 Mo | **44 365 073 o (44,4 Mo)** | **PASSE** |
| temps de compilation | a rendre | **6 min 03 s** (362,9 s) | rendu |
| pic RSS | a rendre | **418 924 ko (409 Mo)** | rendu |
| nombre de fonctions | a rendre | **49 694** | rendu |
| plus grosse fonction | < 1 Mo | **725 073 o** | **PASSE** |
| ligne de log du menu | doit exister | **absente** | **ECHOUE** |

## Ce que le build a change

Le plus gros gain vient du **lift complet** : la plus grosse fonction est passee de **1 358 012** a **725 073**
octets, et le module de 49,7 a 44,4 Mo.

C'est contre-intuitif — ajouter des fonctions **reduit** le module — mais coherent : la fonction geante
`f_0070d138` etait un **artefact de lift incomplet**, qui absorbait des sauts non resolus. Une fois le lift
complet, elle se decompose en fonctions normales.

## Le critere qui echoue

    unknown indirect target 0x00000000 (lr=0x005d2c08) r3=0x0 r3=0x0 r9=0x0 r31=0x0 ctr=0x0

Le module demarre (`guest machine up`, `WASM-GUEST-CODE-RAN`), charge ses images, cree **2 threads**, puis
appelle une methode virtuelle sur un pointeur nul.

**La ligne de menu n'est donc pas atteinte sur ce chemin.** Mais elle l'etait au round 167 avec l'ancien
arbre : 17 threads d'interface et `MAIN MENU REACHED`.

## Ce que ca signifie

| chemin | menu | threads |
|---|---|---|
| ancien arbre (22 685 fonctions) | **oui** | 17 |
| **arbre lifté complet (23 302 fonctions)** | **non** | 2 |

Le lift complet **change le comportement** : le jeu avance plus loin dans son initialisation, atteint un
chemin que l'ancien lift ne pouvait pas prendre (faute de fonctions), et s'y arrete sur un pointeur nul.

**L'ancien arbre atteignait le menu en evitant ce chemin** — par absence, pas par correction.

## Position

| critere | etat |
|---|---|
| 1. module < 80 Mo | **passe** |
| 2. plus grosse fonction < 1 Mo | **passe** |
| 3. taille / temps / RSS rendus | **fait** |
| 4. nombre et taille de fonctions rendus | **fait** |
| 5. ligne de log du menu | **echoue sur le lift complet** ; passait sur l'ancien arbre |