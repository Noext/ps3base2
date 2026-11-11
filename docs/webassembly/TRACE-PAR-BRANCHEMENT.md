# Une trace par branchement, la ou les gardes de fonction ne peuvent pas aller

## Le besoin

Le round 212 a etabli une limite : `hook_flags` ne s'applique qu'aux **entrees de fonction**, donc un label
interne n'est pas instrumentable. Or la question posee — « quel chemin mene de `GetResolution` au crash en
sautant `Configure` » — est precisement une question de **branchement interne**.

## L'instrument

`bc_cond` est le seul point par ou passe **chaque decision de saut** du code lifté. Un crochet la-dedans voit
exactement les branches prises, sans regenerer `out/ppu` :

    inline bool bc_cond(Context& c, uint32_t bo, uint32_t bi) {
        ...
        const bool taken = ctr_ok && cond_ok;
    #if PS3BASE_BLOCK_TRACE
        ps3base_block_trace(bo, bi, taken);
    #endif
        return taken;
    }

Actif sous `-DPS3BASE_BLOCK_TRACE=1`.

## Le piege qui a coute trois essais

Le define n'etait applique qu'a la **compilation du runtime**, pas a celle des `mc_*.cpp` :

    for f in "$ROOT"/out/ppu/*.cpp; do
        "$EMCC" "${CXXFLAGS[@]}" -c "$f" -o "$o"     # aucun -D
    done

`bc_cond` est `inline` dans `ops.h` : le code de trace est donc inliné **dans l'unite de traduction** qui
l'appelle. Sans le define sur les `mc_*.cpp`, il n'existe nulle part.

C'est la **quatrieme** fois dans ce projet qu'un instrument ne couvrait pas ce que je croyais. Les trois
precedentes : probe `ld32` absent des TU `mc_*`, `ROOT` code en dur, doublon de garde.

## La sortie

    [blk] bo=4 bi=30 taken=0
    [blk] bo=4 bi=18 taken=0
    [blk] bo=12 bi=30 taken=1
    [blk] bo=4 bi=2 taken=1

201 branchements enregistres sur le boot.

## Le cout

Le module passe de 44 367 014 a **44 642 476** octets avec les trois `-D` de diagnostic. C'est acceptable pour
un build d'analyse, et les flags sont dans le script, pas dans le code.

## Ce qui reste a faire

Filtrer la trace sur la plage de `f_002da36c` (`0x002da36c`-`0x002dac00`) et sur le `lr`, pour ne garder que
les decisions de la fonction etudiee. La trace globale est trop bruyante pour repondre directement.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 44,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |