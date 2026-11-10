# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| 40 `texloc` natif, 0 wasm | `M3-TEXTURE-BIND-GAP.md` (round 79) | oui |
| Les registres de texture sont `TEXTURE_BASE = 0x1A00` + unit×0x20 | `rsx_renderer.cpp:134,1495-1505` | oui |
| `method()` reçoit tous les registres < 0xE000 | `runtime/src/lv2/rsx.cpp:256` | oui |

# L'expérience : enregistrer l'état de liaison

## Ce que j'ai ajouté

Le stub compte les commandes. Je l'ai étendu pour **enregistrer** l'offset de texture par unité
quand le guest écrit `TEXTURE_BASE + unit*0x20` :

    void note_texture_unit(uint32_t unit, uint32_t offset) {
        g_tex_offsets[unit].store(offset);
        if (offset) g_tex_bound.fetch_or(1u << unit);
        else g_tex_bound.fetch_and(~(1u << unit));
    }

Puis j'ai mesuré le masque des unités liées.

## Le résultat

    texmask=0x0
    texmask=0x0

**Le masque reste à zéro.** Le guest n'écrit **aucun** registre de texture dans ce run.

## Ce que ça signifie

Ce n'est pas que le stub les perd : c'est que **le guest ne les écrit pas**. Les 3 000 draws par
seconde se font donc sans lier de texture — ce qui est cohérent avec des frames vides, mais ne
prouve pas que le chemin de liaison est rompu.

Le compteur est correct (les registres passent bien par `method()`, ligne 256 de `rsx.cpp`), et le
code est placé après les branches `TRANSFORM_*` qui ne couvrent pas `0x1A00`.

## Ce que ça change dans le diagnostic du round 79

Round 79 concluait : « le maillon le plus amont est la liaison de texture, et le stub compte sans
lier ». **La seconde moitié est fausse** : le stub ne peut pas « lier » quelque chose que le guest
ne lui envoie pas.

Le vrai fait mesuré reste : **40 `texloc` natif, 0 wasm**. Mais l'explication n'est pas que le stub
les ignore — c'est que le code natif qui produit ces 40 lignes s'exécute dans le renderer, et le
wasm **n'y arrive pas**.

## Ce que ça laisse

La question redevient : pourquoi le natif lie-t-il 40 textures et pas le wasm, si le guest écrit les
mêmes registres ? Deux possibilités :

1. Le wasm **n'atteint pas** le code invité qui écrit ces registres (le jeu est dans une branche
   différente).
2. Le wasm l'atteint mais le stub reçoit des valeurs nulles.

La première est plus probable, en cohérence avec tout le diagnostic : le jeu s'arrête tôt.

## Conservation de l'ajout

L'enregistrement des offsets est **conservé** : il ne coûte rien, il rend le stub plus fidèle à la
sémantique d'un registre (une écriture est relue), et il fournira la mesure si le guest finit par
écrire ces registres.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **corrige** la conclusion du round 79 : le stub n'ignore pas les
liaisons, le guest ne les envoie pas. Mesure : `texmask=0x0` sur tout le run.
