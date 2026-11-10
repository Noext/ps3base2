# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Mon compteur était aveugle au chemin groupé | `M3-INSTRUMENT-BLIND-SPOT.md` (round 103) | oui |
| `color_targets()` vide si clip = 0 | `rsx_renderer.cpp:1184-1186` | oui |
| **Le registre de fichier montre ce que le guest écrit vraiment** | `gpu::rsx::registers()` | **oui — la mesure fiable** |

# Ce que le guest écrit réellement

## Le dump complet

    nonzero regs 0x0000-0x0220:
      0x000=31337000
      0x180=66604200  0x184=feed0000  0x188=feed0001  0x18c=feed0000
      0x194=feed0000  0x198=feed0000  0x19c=feed0000  0x1a0=feed0001
      0x1b4=feed0000  0x1b8=feed0000
      0x208=00000121  0x20c=00000040  0x21c=0000004...

    tex unit offsets: u0=0x280000  u1=0x20a8000  u7=0x485c480

## Ce qui EST écrit

| registre | valeur | signification |
|---|---|---|
| **`0x208` SURFACE_FORMAT** | **0x121** | format de couleur |
| `0x20C` | 0x40 | pitch de couleur A |
| `0x21C` | 0x4... | pitch de couleur B |
| `0x1B4`/`0x1B8` | 0 | offsets couleur C/D |
| **`0x1A00` tex u0** | **0x280000** | offset de texture 0 |
| **`0x1A20` tex u1** | **0x20a8000** | offset de texture 1 |
| **`0x1AE0` tex u7** | **0x485c480** | offset de texture 7 |

## Ce qui n'est PAS écrit

| registre | valeur |
|---|---|
| **`0x200` SURFACE_CLIP_HORIZONTAL** | **0** |
| **`0x204` SURFACE_CLIP_VERTICAL** | **0** |
| `0x210` SURFACE_COLOR_AOFFSET | **0** |

## Ce que ça signifie précisément

Le jeu configure :
- le **format** de sa surface (`0x121`),
- les **pitch** (0x40, 0x4...),
- **trois textures** (unités 0, 1 et 7),

et **ne configure pas** :
- les **dimensions** de clip,
- l'**offset couleur A**.

## L'incohérence qui en ressort

Un jeu qui écrit le **format** et le **pitch** mais pas les **dimensions** est dans un état **partiel**. Et
l'offset couleur A à zéro alors que le format est écrit est également partiel.

C'est cohérent avec un jeu **interrompu au milieu de sa configuration de rendu** : il a commencé (format,
pitch, textures) et n'a pas fini (dimensions, offset).

## Ce que ça élimine

Les rounds 92-102 concluaient « le guest ne configure aucune surface ni texture ». **Faux** : il configure
les deux partiellement.

Le renderer retourne `color_targets()` vide **parce que les dimensions manquent**, pas parce que rien n'est
configuré.

## Ce que ça déplace

La question n'est plus « pourquoi le guest n'écrit rien » mais **« pourquoi s'arrête-t-il entre le format et
les dimensions »**.

C'est un point d'arrêt très précis : le jeu écrit `0x208`, `0x20C`, `0x1A00`, `0x1A20`, `0x1AE0` — puis
rien.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |

Le jalon 3 reste ouvert. Ce round **établit le profil exact** : le guest configure format, pitch et trois
textures, mais **pas les dimensions de clip ni l'offset couleur**.
