# Le module officiel avance jusqu'a un appel virtuel nul

## La progression

| etape | etat |
|---|---|
| demarrage | **OK** — `guest machine up, 32 cpus` |
| images | **OK** — `mc=0 liblv2=1`, les deux chargent |
| threads | **2 crees** |
| echec | appel virtuel a l'adresse **0** |

## Les deux correctifs du round

### `spu::compiled_blocks` n'est defini nulle part

    runtime/include/spu/aot.h:21  : const CompiledBlock* compiled_blocks(size_t& count);
    runtime/src/spu/aot.cpp:25    : const CompiledBlock* blocks = compiled_blocks(n);

**Declare, jamais defini.** C'est une table AOT generee pour un titre donne. `aot.cpp` gere deja `n == 0`
(l'index reste inactif, l'interprete execute tout). Defini a zero.

### `edge::gpu::g_active` perdu par une edition

Mon ajout de `compiled_blocks` a **ecrase** la definition de `g_active` ecrite au round 192. Symptome :
`undefined symbol: edge::gpu::g_active`, et le build entier echoue.

C'est un **doublon de ma propre main**, pas un probleme du depot.

## L'echec actuel

    F 0.884432 ppu: unknown indirect target 0x00000000 (lr=0x005d2c08)

Le code au site d'appel :

    ctx.gpr[9]  = ld32(gpr[31] + 12);     // pointeur de vtable
    ctx.gpr[0]  = ld32(gpr[9] + 0);       // premiere methode
    ppu::call_indirect(ctx, base, ctr);

**C'est un appel de methode virtuelle.** Le pointeur de vtable est **nul** : l'objet n'a pas ete construit, ou
le champ `+12` n'a pas ete initialise.

## Ce que ca signifie

Ce n'est **pas** un bug de lift ni de liaison : le code est correct, c'est **l'etat du jeu** qui est incomplet.
Un objet qui devrait exister ne l'est pas, parce qu'une etape anterieure ne s'est pas deroulee.

C'est different des 190 rounds precedents : on n'est plus dans « il manque du code », mais dans **« une
initialisation n'a pas eu lieu »**.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 5 criteres sur 6 ; **module officiel : demarre, charge les images, cree des threads** |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |

---

# Round 193, suite : trois obstacles de plus franchis

## Ce qui a change apres le premier rapport

| obstacle | resolution | resultat |
|---|---|---|
| `spu::compiled_blocks` | defini a zero (table AOT vide) | franchi |
| `edge::gpu::g_active` | defini a `false` | franchi |
| `jsedge::spu_idle` | stub no-op | franchi |
| erreur de syntaxe dans `cellGcmSys.cpp` | point-virgule manquant (autre agent) | corrigee |

## L'etat mesure

    guest machine up          OK
    WASM-GUEST-CODE-RAN       OK
    images mc=                OK
    threads                   2
    dernier evenement         sys_memory_allocate(0x300000) -> 0x40100000
    echec                     unreachable, sans nom de symbole

## Ce que l'absence de nom signifie

`-sERROR_ON_UNDEFINED_SYMBOLS=0` transforme les 18 symboles non definis restants en **trampolines abortifs**.
Quand l'un est appele, wasm produit `unreachable` **sans nommer le symbole** — contrairement a
`Aborted(missing function: ...)` qui apparait quand la table d'imports est incomplète.

Le crash est donc tres probablement un **18e symbole GOWA appele a l'execution**.

## Ce qu'il faudrait

Remplacer `-sERROR_ON_UNDEFINED_SYMBOLS=0` par `1` pour obtenir le **nom** du symbole au lieu d'un
`unreachable` anonyme. C'est une ligne, et elle rendrait les 18 symboles visibles un par un.

## Position inchangee

Le module officiel **demarre, charge les images, cree 2 threads, atteint `sys_memory_allocate`**. Il ne va pas
plus loin, et le prochain round doit nommer le symbole exact.
