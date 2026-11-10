# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le wasm s'arrête à 59 allocations contre 119 | `M3-ALLOC-DIVERGENCE.md` (round 70) | oui |
| `pc=0x002dc20c` est l'adresse finale (115 échantillons) | `/tmp/def.log` | oui |
| Ce pc est `sys_timer_usleep` (141) dans `f_002dc1e4_body` | `out/ppu/mc_028.cpp` | oui |
| Il lit l'octet à `0xBE0000 + 1804` = `0xBE070C` | `out/ppu/mc_028.cpp` | oui |

# La boucle d'attente, examinée puis écartée

## Ce que le code fait

`f_002dc1e4_body` n'a que 14 lignes avant le `usleep` :

    ctx.gpr[31] = 0 + 12451840;                            /* 0xBE0000 */
    ctx.gpr[3] = uint64_t(ppu::ld8(base, uint32_t(ctx.gpr[31] + 1804)));
    ppu::cr_set(ctx, 0, int64_t(int32_t(ctx.gpr[3])), 0);
    ctx.gpr[11] = 0 + 141;                                 /* sys_timer_usleep */
    ctx.gpr[3] = 0 + 300;                                  /* 300 us */

C'est un **accesseur** : lire un octet à `0xBE070C`, comparer à zéro, dormir 300 µs.

## La mesure

J'ai lu l'octet à l'exécution :

    byte[0xBE070C]=1  byte[0xBE0709]=16  byte[0xBE0708]=48

**L'octet vaut 1**, donc non nul. La condition est déjà satisfaite.

## Ce que ça élimine

`0xBE070C` n'est pas un drapeau d'attente qui resterait à zéro. Il vaut 1 dès le début.

Et la fonction n'a **que 14 lignes** : ce n'est pas une boucle de sondage, c'est un accesseur
appelé depuis un appelant qui, lui, boucle.

Le `usleep` de 300 µs vient donc d'ailleurs dans la pile — pas d'une attente sur cet octet.

## Ce que ce round établit

| fait | valeur |
|---|---|
| octet attendu `0xBE070C` | **1** (prêt) |
| taille de `f_002dc1e4` | 14 lignes |
| `usleep` au total | 79 540 appels |

L'attente n'est **pas** sur cet octet. Mon identification du round 71 était incomplète : j'ai
trouvé le site du `usleep`, pas sa cause.

## Où chercher ensuite

L'appelant de `f_002dc1e4` : c'est lui qui décide de boucler. Il faut remonter la pile depuis
`0x002dc20c` jusqu'à la fonction qui teste une condition et rappelle.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **écarte** l'hypothèse d'une attente sur l'octet `0xBE070C`
(il vaut 1) et précise que `f_002dc1e4` est un accesseur, pas la boucle.
