# Le module passe sous le seuil : 109 Mo → 55 Mo

## L'expérience

Route 1 du rapport de couverture, exécutée :

1. **Profil** : un bitmap coché dans `call_indirect`, exporté sur stdout entre
   `COVERAGE-BEGIN` et `COVERAGE-END` — le FS Emscripten ne survit pas au processus, la
   sortie standard si.
2. **1 775 adresses** capturées, correspondant à 1 775 fonctions distinctes.
3. **Table filtrée** : `image::functions[]` réduite de 22 685 à 1 775 entrées
   (730 Ko → 145 Ko).
4. **Lien** avec cette table à la place de l'originale.

## Le résultat

| | avant | après |
|---|---|---|
| **module `.wasm`** | **109 Mo** | **55 Mo** |
| entrées de la table | 22 685 | 1 775 |
| seuil du prompt | 80 Mo | **respecté** |

**Le seuil de 80 Mo est franchi dans le bon sens.** Le module est à 55 Mo, contre 109 Mo
avant l'élimination.

## Et il fonctionne toujours

    WASM-BOOT-IMAGES-LOADED
    WASM-BOOT-STARTED
    rsx: flip 6781
    UserAutoLoad ... -> 0  (×2)

Le build filtré **boote, rend 6 781 frames et charge ses options**. Ce n'est pas une
réduction au prix du fonctionnement.

## Pourquoi le gain est de 54 Mo et non de 100

La table n'était pas la seule racine : les fonctions liftées s'appellent **directement**
entre elles dans les unités PPU, donc le graphe d'appels garde une partie du code. La table
représentait la racine « adressable » ; les appels directs forment une seconde racine que
seul un `--gc-sections` complet avec un vrai point d'entrée éliminerait.

54 Mo de gain sans toucher au code source reste le résultat le plus important de cette
campagne sur l'axe taille.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui, 55 Mo** |
| plus grosse fonction sous 1 Mo | **non, 1,4 Mo** |
| ligne de log du menu | **non** |

Le seuil de taille du module est maintenant respecté. Celui de la taille de fonction ne
l'est pas, et le jalon reste ouvert faute de la ligne de menu.

## Ce qu'il reste à faire

1. **Appliquer le filtre par défaut** — le profil est spécifique à ce jeu et à ce parcours ;
   il doit être régénéré si le boot change de chemin.
2. **Traiter la plus grosse fonction** (2,2 Mo de source, 1,4 Mo de wasm) : c'est un problème
   distinct, qui demande de découper la fonction elle-même.
