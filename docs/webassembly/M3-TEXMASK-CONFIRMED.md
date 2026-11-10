# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `texmask=0x0` : le guest ne lie aucune texture | `M3-NOT-TEXTURE-REGS.md` (round 82) | **confirmé** |
| Mon hook couvrait 16 unités (`0x1A00-0x1C00`), les écritures allaient à `0x1FFF` | `rsx_no_display.cpp` | **corrigé — plage élargie** |
| Le build natif ne démarre plus (chantier parallèle) | `M3-PARK-CONFIG-DIFFERS.md` (round 84) | oui — mesure wasm indépendante |

# Confirmation indépendante

## L'élargissement

Mon hook précédent couvrait `TEXTURE_BASE_R .. +16*0x20`, soit `0x1A00-0x1C00`. Or les écritures
observées au round 81 allaient jusqu'à `0x1FFF`.

Je l'ai élargi à **toute la fenêtre `0x1A00-0x2000`**, avec un compteur par (unité, champ) sur
32 unités × 8 champs, et un rapport des champs 0.

## Le résultat

    texmask=0x0  (× 8 échantillons sur tout le run)

**Aucune ligne `tex unit N field0 writes=...`** n'apparaît, alors que le code la produirait dès la
première écriture dans le champ 0 d'une unité.

## Ce que ça établit

Le guest **n'écrit aucun registre d'unité de texture**, sur une plage deux fois plus large que celle
qui était mesurée auparavant. `texmask = 0x0` est maintenant établi par **deux instruments
indépendants** et une plage correcte.

## Autre mesure du même run

    fb tris=1,474,310 verts=4,445,670 hash=5360c294 colors=16

Le framebuffer synthétique montre **16 couleurs** et un hash non uniforme — mais ce sont **mes**
triangles de test, pas la sortie du guest (constaté au round 46). Cette mesure ne prouve rien sur le
contenu réel et je ne l'utilise pas comme preuve.

## Ce que ça signifie

Le jeu tourne en mode **sans texture** : 87 918 paires de programmes de sommets/fragments, 1,47 M de
draws, et zéro liaison de texture.

C'est cohérent avec la chaîne du round 77 : le jeu est dans une boucle de présentation qui ne rend pas
de contenu, et il n'utilise pas de textures parce qu'il n'est pas entré dans la phase de rendu
habillée.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **confirme par un second instrument** le fait du round 82, avec une
plage de registres corrigée. La mesure native reste bloquée par le chantier parallèle.
