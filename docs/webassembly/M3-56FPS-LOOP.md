# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le FIFO wasm ne dort jamais, 79 % de flips | `M3-FIFO-SATURATED.md` (round 68) | oui |
| Un flip vient du guest (package 0x102), pas du stub | `runtime/src/lv2/rsx.cpp:160` | **oui** |
| La classification des paquets est par plage de méthode | `runtime/src/lv2/rsx.cpp:581-582` | oui |

# Le jeu tourne en boucle serrée, et c'est mesurable

## Le débit de frames

| | wasm | natif |
|---|---|---|
| flips | **11 221** | 74 581 |
| durée | **200 s** | 9 126 s |
| **fps effectif** | **56** | **8** |

**Le wasm présente 56 frames par seconde, le natif 8.** Le wasm va **7× plus vite**.

C'est la signature d'une boucle qui ne fait rien : le jeu efface, flippe, recommence, sans jamais
attendre autre chose que le vblank.

## Et le reste du système suit

    ps3base-wasm: t=200s pc=0x002dc20c syscalls=401741 exited=0

**401 741 syscalls** — contre ~8 500 au début de ce diagnostic et 10 214 après le correctif des
assets. Le système **travaille**, il ne bloque pas.

## Ce que ça change dans le diagnostic

Depuis une vingtaine de rounds je cherchais **ce qui bloque**. Les mesures disent l'inverse :

| indicateur | état |
|---|---|
| threads PPU | 18 |
| instructions SPU | 7,4 M |
| `putllc` réussis | 1,08 M |
| syscalls | 401 741 |
| frames | 11 221 |
| FIFO | 100 % occupé, 0 sommeil |
| **fps** | **56** |
| crashs | 0 |

**Rien ne bloque.** Le jeu tourne à 56 fps, multi-threadé, avec de l'audio, des assets chargés et des
options lues. Il ne fait simplement pas ce que j'attends : il n'affiche pas de menu.

## La reformulation

Le système tourne **plus vite** que le natif, donc il ne manque pas de temps — il manque un **état**
que le jeu n'atteint pas.

## Ce que je n'ai pas établi

**Pourquoi** le jeu reste dans cette boucle. Les candidats déjà écartés : réseau, audio, assets,
options, réservations SPU, `ie`, décrémenteur. Les candidats restants sont dans la **logique du jeu**
elle-même — un état d'attente applicatif que le natif franchit et pas nous.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 MB | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **chiffre la boucle** : 56 fps contre 8, 401 741 syscalls, aucun
blocage. Le diagnostic change de nature — il ne s'agit plus de débloquer mais de comprendre quel
état applicatif le jeu n'atteint pas.
