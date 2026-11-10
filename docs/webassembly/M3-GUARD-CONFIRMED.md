# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Les flips sont une activité réelle du guest | `M3-FLIPS-ARE-REAL.md` (round 101) | oui |
| Le stub reçoit `clipH=0 clipV=0` | `M3-WRITES-PROFILE.md` (round 92) | oui |
| **`color_targets()` retourne vide si clip est zéro** | `rsx_renderer.cpp:1184-1186` | **oui — confirmation directe** |

# La chaîne est confirmée par le code du renderer

    uint32_t clip_w() { return R(SURFACE_CLIP_HORIZONTAL) >> 16; }
    uint32_t clip_h() { return R(SURFACE_CLIP_VERTICAL) >> 16; }

    std::vector<Surface*> color_targets() {
        uint32_t w = clip_w(), h = clip_h();
        if (!w || !h) return out;            // AUCUNE SURFACE si zéro
        VkFormat f = color_format(R(SURFACE_FORMAT));
        ...
    }

| | natif | wasm |
|---|---|---|
| `clip_w` / `clip_h` | non nuls | **0 / 0** |
| `color_targets()` | surfaces | **vide** |
| surfaces créées | **6** | **0** |

**La mesure du stub explique exactement l'absence de surfaces.** Le renderer a une garde explicite.

# La chaîne, adossée au code

    guest n'écrit pas 0x0200/0x0204
      → clip_w = clip_h = 0
        → color_targets() vide
          → aucune surface
            → aucun TEXTURE_OFFSET
              → aucun texloc
                → frames vides

Chaque maillon est adossé à du code ou à une mesure.

# Ce que ça ne dit pas

**Pourquoi** le guest n'écrit pas ces registres. C'est la seule inconnue restante.

Le jeu émet 2,9 M de draws et 11 221 présentations mais ne définit jamais la taille de sa cible.

# Une hypothèse testable

Le jeu pourrait écrire ces registres via une **commande FIFO groupée** (paquet `bulk`) que notre compteur ne
décompose pas. Les `bulk` font 158k/s côté wasm — à vérifier.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 MB | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |
