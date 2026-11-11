# Natif : 119 allocations. Wasm : 5. Le jeu n'atteint pas sa phase d'allocation

## La comparaison

| | natif | wasm |
|---|---|---|
| `sys_memory_allocate` reussis | **119** | **5** |
| regions mappees | **3** (`0x10000000`, `0x30000000`, `0x40000000`) | **2** (`0x10000000`, `0x40000000`) |
| plus grosse allocation | `0x1000000` (16 Mo) a `0x30400000` | `0x80000` a `0x10010000` |

## L'ordre des allocations natives

    t=0.293  0x10000   -> 0x10000000
    t=0.294  0x80000   -> 0x10010000
    t=0.295  0x10000   -> 0x10090000
    t=0.297  0x100000  -> 0x30000000     <-- wasm place la meme a 0x40000000
    t=0.328  0x300000  -> 0x30100000
    t=0.355  0x1000000 -> 0x30400000     <-- le wasm ne l'atteint jamais
    t=0.419  0x400000  -> 0x31400000
    ... 112 autres

## Ce que ca etablit

Le wasm fait **les cinq premieres allocations** (avec une base differente pour la quatrieme) puis s'arrete.
La suite — 114 allocations, jusqu'a un total de plusieurs centaines de Mo — **n'est jamais demandee**.

Ce n'est donc **pas** un echec d'allocation : `sys_memory_allocate` reussit pour les cinq premieres. C'est le
**chemin d'execution** qui s'arrete.

## Ce que ca relie au blocage connu

Le crash survient a `t≈0.87 s` cote wasm, apres la cinquieme allocation (`0x40100000`). Cote natif, a
`t=0.87 s` le jeu en est deja a sa dixieme allocation et continue.

**Le jeu meurt exactement la ou il allait demander sa sixieme allocation.**

Le champ nul a `0x00d72800` est donc probablement un **pointeur vers une structure que le natif alloue ensuite**
— mais cette structure serait allouee a `0x30000000`, pas a `0x00d70000`. Les deux adresses different, donc
l'hypothese demande verification.

## Ce qu'il faudrait mesurer

Si le jeu, juste avant le crash, **preparait** l'appel `sys_memory_allocate(0x1000000)` et lisait un champ de la
structure de gestion memoire pour le construire. Le `lr=0x005d2c08` est dans `f_005d2b44`, et l'appelant
(`mc_028`, `lr=0x2da54c`) charge `ld32(0x00d72800)` — une adresse **tres basse**, pas `0x30000000`.

Donc la structure lue n'est pas celle que le natif alloue a `0x30000000`. La correlation est temporelle, pas
causale, et je ne la presente pas comme une cause.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 44,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |