# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `texmask=0x0` (round 80) | `M3-GUEST-SENDS-NO-TEXTURE.md` | **le masque est juste, l'explication était fausse** |
| 1 242 341 écritures dans `1A00-1FFF` (round 81) | `/tmp/ps3rng.js` | **oui — mais ce ne sont pas des textures** |
| `CLEAR_SURFACE = 0x1D94`, `TRANSFORM_PROGRAM_LOAD = 0x1E9C` | `rsx_renderer.cpp:139,142` | **oui — voilà ce qui est écrit** |

# La mesure qui tranche

J'ai enregistré les **premiers registres exacts** écrits dans la plage `1A00-1FFF` :

    texreg: first write 0x1d94 = 0x00000000 (unit 28 field 0x14)
    texreg: first write 0x1d6c = 0x00000ff0 (unit 27 field 0xc)
    texreg: first write 0x1d70 = 0x00010000 (unit 27 field 0x10)
    texreg: first write 0x1e9c = 0x00000000 (unit 36 field 0x1c)

## Ce que sont ces registres

| registre | signification | source |
|---|---|---|
| `0x1D6C` | `sema_off_4097` | `lv2/rsx.cpp:310` |
| `0x1D70` | sémaphore | `lv2/rsx.cpp:311` |
| **`0x1D94`** | **`CLEAR_SURFACE`** | `rsx_renderer.cpp:139` |
| **`0x1E9C`** | **`TRANSFORM_PROGRAM_LOAD`** | `rsx_renderer.cpp:142` |

**Aucun n'est un registre de texture.** Ma plage `1A00-1FFF` englobait des registres de commande
(clear, chargement de programme, sémaphores) qui n'ont rien à voir avec les unités de texture.

## Ce que ça corrige définitivement

| affirmation | verdict |
|---|---|
| Round 79 : « la liaison de texture manque » | **reste à établir** |
| Round 80 : « le guest n'envoie aucune texture » | **juste** — `texmask=0x0` |
| Round 81 : « le guest écrit 1,24 M de registres de texture » | **FAUX** — ce sont clear/programme/sémaphores |

## L'état réel

    texmask = 0x0

Le guest **n'écrit aucun registre d'unité de texture** (`0x1A00 + unit*0x20`, champ 0). C'est
mesuré deux fois, par deux instruments différents, et corroboré par l'absence de `texloc`.

Donc : **le guest ne lie pas de texture du tout** dans ce run. Les draws qu'il soumet (3k/s) sont des
draws sans texture.

## Ce que ça signifie pour la chaîne

La chaîne du round 77 tient, et ce round en précise le premier maillon :

    le jeu tourne en mode « pas de texture liée »
      → ses draws n'ont pas de contenu
        → il ne franchit pas l'étape qui lui ferait charger la suite
          → allocations arrêtées à 59
            → FullscreenProgress jamais créé
              → bit 0x1 absent
                → Chunk update bloqué
                  → frames vides

Et la cause du « pas de texture liée » est dans le **chemin invité** : le jeu prend une branche qui
n'utilise pas de textures.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **tranche** entre les rounds 80 et 81 : c'est le round 80 qui avait
raison. Les 1,24 M d'écritures étaient des registres de commande, pas de texture.
