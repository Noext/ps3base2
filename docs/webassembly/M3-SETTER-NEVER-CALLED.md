# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| 9 sites appellent `sys_event_flag_set` (87) dans tout le binaire lifté | `libsre_000.cpp` (3), `libsysmodule_000.cpp` (1), `mc_036.cpp` (5) | **oui** — l'ensemble complet |
| Les 3 sites de `libsre` sont dans `f_01211484`, `f_012116fc`, `f_01211854` | `out/ppu/libsre_000.cpp` | oui |
| La pose observée au round 53 venait de `lr=0x003a19e4` | `/tmp/fid2.log` | oui → `f_003a19bc_body` |

# Hypothèse 2 confirmée : le poseur des sites « système » n'est jamais atteint

J'ai posé un garde sur les **trois** sites `sys_event_flag_set` de `libsre` :

    0x12116dc  0x12117dc  0x1211aac

Résultat après un run complet : **aucune sortie `setsite`**. Les trois sites ne sont
**jamais atteints**.

## Où les poses viennent réellement

Les poses observées au round 53 ont ces `lr` :

| lr observé | fonction | fichier |
|---|---|---|
| `0x003a19e4` | `f_003a19bc_body` | `mc_036.cpp` |
| `0x003a1564` | `f_003a1500_body` | `mc_036.cpp` |
| `0x003a142c` | `f_003a138c_body` | `mc_036.cpp` |

**Toutes les poses réelles viennent de `mc_036.cpp`**, pas de `libsre`. Les sites de `libsre`
sont du code de bibliothèque qui ne tourne pas dans ce boot.

## Ce que ça change

La question « qui devrait poser `0x9800BB00` » a une réponse plus étroite : ce n'est pas un
site système de `libsre`, c'est **l'un des 5 sites de `mc_036`** — ou un chemin qui n'appelle
aucun des 9.

Or aucun de ces sites ne s'exécute avec `gpr[3] = 0x9800BB00`. Les trois poses observées
ciblent `0x98046C00`, `0x98057D00`, `0x98059D00`.

## Ce que la mesure établit

1. Le flag `0x9800BB00` est créé par `main_thread` (t=7,52 s).
2. Il est attendu par `(4J) Tile update` (t=7,55 s), mode AND, timeout infini.
3. **Aucun des 9 sites de pose du binaire ne le cible.**
4. Les 3 sites « système » (`libsre`) ne sont même pas atteints.

Donc : **le code guest n'appelle jamais `sys_event_flag_set` sur ce flag.** Ce n'est pas un
échec de pose, c'est une **absence d'appel** — le chemin qui devrait poser n'est pas emprunté.

## Deux lectures possibles

1. **Le jeu attend un signal produit par un chemin non atteint** — par exemple après une
   transition d'écran, ou par un thread dont la boucle ne démarre pas.
2. **Le flag n'est pas censé être posé par `sys_event_flag_set`** mais par un autre mécanisme
   (une écriture directe en mémoire par un autre processeur — les SPU peuvent écrire des
   sémaphores). Dans ce cas mon hypothèse d'un poseur PPU serait fausse.

La seconde mérite un test : chercher une écriture mémoire directe sur l'objet `0x9800BB00` par
un SPU. Mais l'objet est un handle alloué par `add_object`, pas une structure invitée
ordinaire — donc la première lecture reste la plus probable.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | oui, 56 Mo, vérifié à l'exécution |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | non |

Le jalon 3 reste ouvert. Ce round **départage les deux hypothèses du round 53** : le poseur
n'échoue pas, il n'est pas appelé. Les 9 sites de pose sont énumérés et localisés, les 3 sites
système sont prouvés non atteints, et les 3 poses réelles sont rattachées à `mc_036`.
