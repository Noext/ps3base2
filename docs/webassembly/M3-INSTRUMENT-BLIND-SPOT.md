# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `color_targets()` retourne vide si clip = 0 | `rsx_renderer.cpp:1184-1186` (round 102) | oui |
| Hypothèse : les registres arrivent par un paquet `bulk` | round 102 | **testée — et elle révèle autre chose** |
| `special[]` liste les méthodes à distribuer | `lv2/rsx.cpp:434-440` | **oui — la clé** |

# Un instrument aveugle, découvert en cherchant ailleurs

## Le chemin d'écriture des paquets groupés

    } else if (!noninc || method != 0x0100) {
        for (uint32_t i = 0; i < count; i++) {
            uint32_t reg = noninc ? method : method + i * 4;
            uint32_t arg = reader.read(ea + 4 + i * 4);
            if (!full_dispatch && !special[reg >> 2]) {
                regs[reg >> 2] = arg;        // <-- écrit SANS passer par method()
                ...
            }
            execute(reg, arg);               // <-- seulement si special[]
            ...

## Le tableau `special`

    static const auto special = [] {
        std::array<uint8_t, 0x4000> t{};
        std::memcpy(t.data(), gpu::rsx::special_methods(), t.size());
        for (uint32_t r : {0x50u, 0x60u, 0x64u, 0x68u, 0x6Cu, 0x1A4u, 0x1A8u,
                           0x1D6Cu, 0x1D70u, 0x1D74u, 0x1800u}) t[r >> 2] = 1;
        for (uint32_t r = 0xE000; r < 0x10000; r += 4) t[r >> 2] = 1;
        return t;
    }();

**Ce sont exactement les registres que j'ai vus écrits au round 92** : `0x50, 0x60, 0x64, 0x68, 0x6C`
(430 000 écritures). Ils passent par `execute()` → `method()` → mon compteur les voit.

## Ce que ça révèle

Les registres **absents** de `special[]` — dont `0x0200` (clip) et `0x0208` (format) — sont écrits
**directement dans `regs[]`**, sans passer par `method()`.

**Mon compteur de surface est donc aveugle à eux.** Si le guest écrit `0x0200` par un paquet groupé, je ne
le verrais pas.

## Ce que ça invalide

Mes rounds 92, 96, 98 concluaient « le guest n'écrit jamais de registre de surface » sur la base de
`g_surface_regs` — un compteur dans `rsx_no_display.cpp::method()`.

**Ce compteur ne voit que le chemin `special[]`.** Les écritures par paquet groupé lui échappent.

Ce n'est pas que la conclusion soit fausse : c'est que **l'instrument ne couvrait pas tous les chemins**, et
je ne l'avais pas vérifié avant d'en tirer une conclusion forte (« 101 millions de méthodes, zéro
écriture »).

## La mesure correcte

Il faut lire `regs[0x200 >> 2]` à la fin du run — la valeur que le renderer lirait réellement — plutôt que
de compter les écritures.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |

Le jalon 3 reste ouvert. Ce round **découvre un angle mort de mon instrument** : `special[]` ne contient pas
`0x0200`, donc les écritures de surface par paquet groupé passent directement dans `regs[]` sans être
comptées. La conclusion « zéro écriture de surface » doit être réexaminée par lecture de `regs[]`.


## La mesure correcte, faite dans le même round

J'ai lu le registre de fichier au lieu de compter les écritures :

    regfile clipH=0x00000000 clipV=0x00000000 fmt=0x00000121 caoff=0x00000000 | tex0off=0x00280000

### Ce que ça dit

| registre | valeur | lecture |
|---|---|---|
| `0x0200` clipH | **0** | le renderer retournera vide |
| `0x0204` clipV | **0** | idem |
| `0x0208` format | **0x121** | écrit ! |
| `0x0210` caoff | **0** | pas d'offset couleur |
| **`0x1A00` tex0off** | **0x280000** | **écrit !** |

**Deux corrections majeures :**

1. **Le guest ÉCRIT `0x1A00` (offset de texture)** — mon compteur disait zéro. Il était bien aveugle.
2. **Le guest écrit aussi `0x0208` (format de surface)** — mon compteur disait zéro.

## Ce que ça invalide définitivement

Les rounds 92, 96, 98 et 100 concluaient « le guest n'écrit aucun registre de texture ni de surface » sur la
base d'un compteur aveugle. **C'est faux.**

Le guest écrit des registres de texture et de surface — par le chemin groupé que `method()` ne voit pas.

## Ce qui reste vrai

`clipH = 0` et `clipV = 0` sont **lus dans le registre de fichier**, donc c'est une mesure fiable. Le
renderer retournera bien `color_targets()` vide.

**L'absence de surface tient.** Mais l'explication n'est pas « le guest n'écrit rien » : c'est « le guest
n'écrit pas les deux registres de clip ».

## La nouvelle question

Pourquoi `0x0200` et `0x0204` restent à zéro alors que `0x0208` (format) et `0x1A00` (texture) sont
écrits ? Le jeu configure **partiellement** sa surface : format et texture, mais pas les dimensions.

C'est un état étrange — et c'est peut-être précisément l'indice : un jeu qui écrit le format sans les
dimensions n'a pas terminé sa configuration.

## Correction de méthode

**Compter les écritures d'un registre est insuffisant** quand plusieurs chemins d'écriture existent. La
mesure fiable est de **lire le registre de fichier final**, ce que le renderer lira réellement.
