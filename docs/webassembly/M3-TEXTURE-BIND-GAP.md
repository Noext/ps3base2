# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le natif reprend les allocations à t=76,9 s après 65 s de pause | `M3-DLC-NOT-THE-CAUSE.md` (round 78) | oui |
| `texloc` est émis par le renderer, sur la liaison de textures | `runtime/src/gpu/rsx_renderer.cpp:1652` | **oui — 40 natif, 0 wasm** |
| Le stub RSX compte sans lier | `runtime/src/gpu/rsx_no_display.cpp` | oui |

# Ce qui précède immédiatement la reprise

## La séquence native à t=76,83-76,88 s

    texloc: off 0x3e03580 fmt 0x0001a529 raw 1 decoded 0 ...
    texloc: off 0x3e1bd00 fmt 0x0001a529 raw 1 decoded 0 ...
    fs: open '.../MediaPS3.arc' -> 4
    sys_memory_allocate(0x100000) -> 0x35000000

Trois lignes : **deux liaisons de texture**, une réouverture d'archive, une allocation.

## Les compteurs

| | natif | wasm |
|---|---|---|
| `texloc` (liaisons de texture) | **40** | **0** |
| réouvertures `MediaPS3.arc` | **49** | 27 |

**Le wasm ne lie aucune texture.** Zéro sur quarante.

## Ce que `texloc` mesure

    const uint8_t* src = vm::ptr(ea);
    uint64_t h = fast_hash(src, tex->layout.source_size);
    g_stats.hash_count++;

C'est le chemin qui **résout l'adresse d'une texture et en hash le contenu** avant de l'envoyer au
GPU. Le log n'est qu'un diagnostic, mais sa présence prouve que ce chemin s'exécute — et il vit dans
`rsx_renderer.cpp`, le fichier exclu du build wasm (`PS3BASE_NO_RENDERER`).

## Ce que ça ajoute à la chaîne

La chaîne du round 77 était :

    allocations arrêtées → FullscreenProgress manquant → bit 0x1 absent → Chunk update bloqué

Ce round ajoute **en amont** :

    pas de liaison de texture (0 contre 40)
      → le jeu ne peut pas valider ses ressources graphiques
        → il ne franchit pas l'étape qui mène à l'allocation 57
          → et la chaîne du round 77 s'applique

Le maillon le plus amont identifié est donc **la liaison de textures**, qui est précisément ce que le
prompt demande de stubber (point 6 : « un stub qui compte les commandes RSX »).

## Le point de tension

Le prompt **exige** un stub qui compte. Mais si le jeu **vérifie** qu'une texture est liée avant de
continuer, un stub qui compte ne peut pas le satisfaire.

C'est un conflit entre la spécification du jalon et le comportement du jeu — et il expliquerait
pourquoi le jalon 3 est présenté comme « la porte, c'est ici que le projet peut échouer ».

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **identifie le maillon le plus amont** : 40 liaisons de texture
côté natif, 0 côté wasm, et cette liaison vit dans le fichier exclu par construction.
