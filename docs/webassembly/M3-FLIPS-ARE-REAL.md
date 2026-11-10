# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `rsx::present` est interne, invisible du guest | `M3-PRESENT-IS-INTERNAL.md` (round 100) | oui |
| Hypothèse : les flips sont un compteur dérivé de notre code | round 100 | **FAUX — corrigé ici** |
| `flip_done()` incrémente `g_rsx.flips`, appelé après le paquet `0x102` | `lv2/rsx.cpp:129-134` | oui |

# Les flips viennent bien du guest

## La mesure

J'ai compté les paquets vblank (package `0x102`) au même endroit que le chemin de flip :

    flipprobe: vblank packets 10600
    flipprobe: vblank packets 10800
    flipprobe: vblank packets 11000

Et le compteur de flips final :

    rsx: flip 11221

| | valeur |
|---|---|
| paquets vblank | **~11 000** |
| flips | **11 221** |

**Les deux chiffres correspondent.** Chaque paquet `0x102` produit un flip.

## Ce que ça corrige

Le round 100 disait : « les flips du wasm sont peut-être artificiels, produits par notre chemin interne
sur réception de paquets, et non par une vraie demande du jeu ».

**C'est faux.** Les paquets `0x102` **sont émis par le guest** : ils arrivent par `context_attribute`, qui
reçoit ses arguments de l'appel HLE du jeu. Ce n'est pas notre code qui les fabrique — nous les recevons.

Donc : **le jeu demande bien 11 221 présentations.** C'est une activité réelle.

## Ce que ça laisse intact

L'ordre inversé du round 99 (présenter avant dessiner) reste vrai, mais son interprétation change :

- Ce n'est pas « notre compteur invente des flips ».
- C'est « **le jeu demande à présenter avant d'avoir configuré une surface** ».

C'est un état du jeu, pas un artefact de l'émulateur.

## Ce que ça consolide

Le jeu, dans le run wasm :
1. Demande **11 221 présentations** (activité réelle).
2. Émet **2,9 M de commandes de draw**.
3. N'écrit **jamais** de registre de surface.
4. Ne lie **aucune** texture.

Un vrai RSX aurait présenté un écran vide à chaque flip (pas de surface = pas de contenu). Le jeu continue
néanmoins — il ne vérifie pas l'état de sa cible.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |

Le jalon 3 reste ouvert. Ce round **corrige** l'hypothèse du round 100 : les flips sont une activité réelle
du guest (11 000 paquets `0x102` pour 11 221 flips), pas un compteur dérivé.
