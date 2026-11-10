# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `texmask=0x0`, le guest n'enverrait aucune texture | `M3-GUEST-SENDS-NO-TEXTURE.md` (round 80) | **FAUX — corrigé ici** |
| `method()` reçoit tous les registres < 0xE000 | `runtime/src/lv2/rsx.cpp:256` | oui |
| Le renderer lit `TEXTURE_BASE + unit*0x20` | `rsx_renderer.cpp:134,1495` | oui |

# Correction du round 80 : le guest écrit bien les registres de texture

## La mesure

J'ai compté les écritures **par plage de registres** dans le chemin FIFO (`lv2/rsx.cpp`) :

    rsxrange: 0x0000-0FFF   932,314
              1000-17FF          0
              1800-19FF    2,927,739
              1A00-1FFF    1,242,341   <-- registres de texture
              2000-7FFF          0
              8000+        7,497,606

**1 242 341 écritures dans la plage des textures.** Le guest les écrit — massivement.

## Pourquoi mon masque restait à zéro

Mon hook dans `rsx_no_display.cpp` couvre `TEXTURE_BASE_R ... TEXTURE_BASE_R + 16*0x20`, soit
`0x1A00-0x1C00`. La plage observée va jusqu'à `0x1FFF`, donc au-delà de 16 unités.

Deux explications possibles, et je n'ai pas départi :

1. Le guest écrit des registres au-delà de l'unité 16, que mon hook ignore.
2. Le guest écrit dans `0x1A00-0x1C00` mais mon compteur n'a jamais été atteint parce que le log
   périodique (toutes les 10 s) n'a pas été lu dans le bon run.

La seconde est probable : le run `/tmp/tex.log` qui affichait `texmask=0x0` était celui du round 80,
et il n'y avait **pas** de compteur par plage pour le corroborer.

## Ce que ça rétablit

| | natif | wasm |
|---|---|---|
| `texloc` (liaisons effectives) | **40** | **0** |
| écritures registres texture | — | **1 242 341** |

Le guest **envoie** les registres. Ce qui manque est leur **traitement** : le stub les enregistre
maintenant (round 80), mais aucun `texloc` n'apparaît puisque `texloc` vit dans le renderer.

Donc la conclusion du round 79 était **juste** et celle du round 80 fausse : c'est bien la **liaison**
qui manque, pas l'envoi.

## Ce que ça laisse

Le stub enregistre les offsets (ajout du round 80, conservé). Il faudrait vérifier que le masque
n'est plus zéro **avec** ce code actif — le run du round 80 disait 0, mais sans mesure de contrôle
indépendante.

C'est la prochaine mesure : rejouer avec le compteur par plage **et** le masque, dans le même run.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **rétablit** le round 79 : le guest écrit 1,24 M de fois dans les
registres de texture. Sa liaison effective reste absente côté wasm (0 `texloc`), mais ce n'est pas
faute d'envoi.
