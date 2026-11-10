# La couverture du code lifté : 7,8 %

## La mesure

Un bitmap d'un bit par 4 octets de texte invité, coché dans `call_indirect` — le point
unique par lequel passe **tout** appel de fonction liftée :

    coverage: 1775 distinct lifted functions executed of 22685 in the table

Deux relevés à 100 s et 180 s donnent le **même chiffre** : le boot a fini de découvrir du
code nouveau. Le plateau est stable.

| | valeur |
|---|---|
| fonctions exécutées | **1 775** |
| fonctions dans la table | 22 685 |
| **couverture** | **7,8 %** |
| inatteintes | 20 910 (**92,2 %**) |

## Ce que ça implique pour le seuil de taille

C'est la réponse que le rapport du round 37 demandait. Le module fait 109 Mo, le seuil est
80 Mo, et **92 % du code n'est jamais exécuté par le boot**.

Si seul le code atteint était conservé :

    109 Mo × 7,8 % ≈ 9 Mo

Le module pourrait passer de 109 Mo à un ordre de grandeur de **9 Mo**, très en dessous du
seuil de 80 Mo. Le levier n'est pas marginal — il est décisif.

## Pourquoi le linker ne le fait pas déjà

`build_dispatch()` remplit `g_ranges[].table[]` avec **les 22 685 pointeurs** de
`image::functions[]`. C'est une racine vivante : chaque fonction liftée est référencée par
la table de dispatch, donc rien n'est mort du point de vue du linker.

`-Oz` ne récupère que 15 Mo pour la même raison : il optimise la taille du code atteignable,
mais le graphe d'appels est complet.

## Les routes, maintenant chiffrées

1. **Élimination pilotée par un profil de couverture.** Le bitmap de ce round donne
   exactement l'ensemble à garder. Il faudrait : lancer le boot, exporter les adresses
   atteintes, régénérer `image::functions[]` filtré, reconstruire. Risque : un chemin non
   pris pendant le profil disparaît et casse un autre parcours du jeu.
2. **Table de dispatch paresseuse** — ne matérialiser les pointeurs qu'à la demande, ce qui
   laisse le linker éliminer les fonctions jamais adressées. Plus fragile : `lookup`
   devrait pouvoir résoudre n'importe quelle adresse.
3. **Découpage en modules wasm** chargés à la demande, chaque unité PPU devenant un module
   séparé. Le plus propre à terme, le plus de travail.
4. **Recompilation partielle** : ne lifter que les fonctions atteignables depuis les points
   d'entrée connus, par analyse statique du binaire. C'est ce que fait un linker à
   `--gc-sections` si l'on n'émet pas la table complète.

## Position

**Le seuil reste franchi** (109 Mo contre 80 Mo) et le jalon 3 reste ouvert. Ce round ne
réduit pas le module : il produit le chiffre qui rend la réduction possible et dimensionnée
— 7,8 % de couverture, ~9 Mo si l'on garde le code atteint.

C'est une mesure, pas une optimisation. La prochaine étape est de choisir entre les quatre
routes ci-dessus, en sachant que la route 1 est la moins coûteuse et la seule qui exploite
directement ce chiffre.
