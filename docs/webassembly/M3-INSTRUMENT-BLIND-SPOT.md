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
