# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le park est par défaut pour wasm, 7,4 M d'instructions SPU | `M3-PARK-DEFAULT.md` (round 67) | oui |
| `1,08 M` `putllc` réussis dans le run par défaut | `/tmp/def.log` | oui — les SPU écrivent |
| Le FIFO ne s'endort que si `get == put` | `runtime/src/lv2/rsx.cpp:409-411` | **oui** |
| `idle_us` vient de `getenv` (inerte), défaut 1000 | `runtime/src/lv2/rsx.cpp:363` | oui |

# Le maillon, chiffré

Les SPU produisent du travail (7,4 M d'instructions, 1,08 M d'écritures réussies) et le rendu ne
bouge pas. Ce round mesure **entre** les deux.

## Le FIFO RSX

| | wasm | natif |
|---|---|---|
| `busy` | **100,0 %** | 77,9 % |
| `idle` | **0,0 %** | 22,1 % |
| `idle sleeps/s` | **0** | **173** |
| `methods` | 508k/s | — |

**Le FIFO du wasm ne dort jamais.** Il tourne à 100 % d'occupation, sans un seul sommeil.

## Pourquoi

    uint32_t put = vm::read32(ctrl + 0x40);
    uint32_t get = vm::read32(ctrl + 0x44);
    if (get == put) { idle_wait(256); continue; }

Le FIFO ne s'endort que si `put == get` — c'est-à-dire s'il n'y a **rien** à traiter. Avec zéro
sommeil sur tout le run, la file n'est **jamais vide** : quelque chose l'alimente en permanence.

## Ce qu'il traite

| type | wasm | natif |
|---|---|---|
| `draw` | **3k/s** (0,0 %) | **65k/s** (42 %) |
| `flip` | **0k/s** mais **79,2 %** des paquets | 29,8 % |
| `clear` | 1k/s (11,4 %) | 0k/s (0,4 %) |
| `bulk` | 158k/s | **660k/s** |
| `other` | 346k/s | **1 020k/s** |

Le wasm traite **20× moins de draws** que le natif, et son trafic est dominé par `flip` (79 %) et
`clear` (11 %) — c'est-à-dire par des **présentations et des effacements**, pas par du dessin.

## La lecture

Le jeu du wasm **tourne en boucle de présentation** : il efface, il flippe, il efface, il flippe.
Les draws arrivent au compte-gouttes (3k/s contre 65k/s) parce qu'il n'a **rien à dessiner**.

Ce n'est donc pas « le FIFO ne reçoit rien » — il reçoit beaucoup, mais du vide. Et le fait qu'il ne
dorme jamais montre qu'il consomme cette boucle aussi vite qu'elle est produite.

## Ce que ça ne dit pas

Je n'ai pas établi **pourquoi** le jeu est dans cette boucle de présentation plutôt que dans le
menu. Ce round établit seulement que le maillon entre le travail SPU et le rendu n'est pas rompu
par une file vide, mais par une file **pleine de présentations**.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round mesure le maillon : FIFO à 100 % d'occupation, 0 sommeil,
20× moins de draws, 79 % de flips. Le jeu présente des frames vides au lieu de dessiner un menu.
