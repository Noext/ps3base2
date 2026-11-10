# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `texmask=0x0` confirmé deux fois | `M3-TEXMASK-CONFIRMED.md` (round 85) | oui |
| `BEGIN_END = 0x1808`, le renderer lit `g_prim = arg & 0xFF` | `rsx_renderer.cpp:121,4229` | **oui** |
| `g_prim == 5` → `TRIANGLE_LIST`, sinon `TRIANGLE_STRIP` | `rsx_renderer.cpp:2683` | oui |

# Quelle forme le jeu dessine

## L'instrumentation

J'ai compté les valeurs de `BEGIN_END` par primitive (index `arg & 7`) et les formats de sommets :

    case 0x1808: {
        const uint32_t prim = arg & 7;
        g_begin_end[prim < 8 ? prim : 7].fetch_add(1);
        return;
    }

## Le résultat

    prims quads=70308  tris=0 strip=0 fan=0 other=0
    prims quads=185444 tris=0 strip=0 fan=0 other=0
    prims quads=300548 tris=0 strip=0 fan=0 other=0
    prims quads=418572 tris=0 strip=0 fan=0 other=0

**418 572 draws de type 0** (que j'avais étiqueté « quads »), et **zéro** de tout autre type. Le format
de sommets a ses 4 bits hauts à zéro dans tous les échantillons.

## Ce que l'étiquette signifie vraiment

Mon étiquette « quads » est arbitraire : le renderer fait `g_prim = arg & 0xFF` et ne distingue que
`5` (TRIANGLE_LIST) des autres (TRIANGLE_STRIP). **Primitive 0 n'est donc pas « quads »** — c'est la
valeur par défaut, traitée comme un strip.

Ce que la mesure établit solidement :

- **toutes les draws utilisent la même primitive** (valeur 0) ;
- **aucune** n'utilise la valeur 5 ;
- le format de sommets ne varie pas sur les bits testés.

## Ce que ça suggère

Un jeu 3D dessine normalement des listes et des strips en proportions variées. Ici, **une seule
primitive pour 418 572 draws** est le profil d'une interface 2D : des sprites/panneaux, tous dessinés
de la même façon.

Combiné à `texmask=0x0` (aucune texture), le tableau est : **beaucoup de géométrie, une seule
primitive, aucune texture**. C'est un écran qui se redessine sans contenu — cohérent avec une boucle de
chargement ou un écran de progression, pas avec un menu habillé ni avec une scène.

## Ce que ça ne prouve pas

Je ne peux pas distinguer « écran de chargement » de « menu non dessiné » avec ces seuls compteurs :
les deux produiraient la même signature.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **caractérise la géométrie** : 418 572 draws, une seule primitive,
zéro texture. Et il corrige une étiquette que j'avais posée sans preuve (« quads »).
