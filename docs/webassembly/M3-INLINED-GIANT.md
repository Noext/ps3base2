# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| Le nom `f_006fe4d8` fait 355 o de source | round 145 | oui |
| **90 noms ont un index au-delà des corps** | mesure | oui — mais l'alignement ne change rien |
| **Le nom reste `f_006fe4d8` après alignement** | mesure | **oui** |

# L'alignement est corrigé, le nom ne change pas

## La vérification

| | valeur |
|---|---|
| fonctions déclarées | 48 918 |
| corps de code | 48 918 |
| noms | 49 008 |
| noms d'index **au-delà** des corps | **90** |

Les 90 noms supplémentaires sont **en fin de section** (index >= 48 918). Les 48 918 premiers noms
correspondent aux corps.

**Après alignement, le plus grand reste `f_006fe4d8`.**

## Ce que ça signifie

L'attribution du nom est **correcte**. Le problème est ailleurs : le corps de 6 instructions occupe
1 358 012 octets dans le `.wasm`.

## L'explication

`f_006fe4d8_body` fait :

    ctx.gpr[6] = 0 + 0;
    f_006fe150(ctx, base); return;

`f_006fe150` fait **12 961 octets de source**. Si le compilateur l'**inline** dans `f_006fe4d8`, cette
dernière grossit de tout son corps.

C'est cohérent : `-Oz` privilégie la taille mais inline encore les fonctions appelées une fois.

## Ce que ça donne

La grosse fonction est une **enveloppe avec une fonction inlinée**. Le source est petit, le code compilé
énorme.

## Ce qu'il faut mesurer

Compiler `mc_070.cpp` avec `-fno-inline` et voir si `f_006fe4d8` rétrécit sous 1 Mo.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui (50,2 Mo)** |
| plus grosse fonction sous 1 Mo | non (1 316 ko) — **probablement de l'inlining** |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |