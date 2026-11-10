# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le guest n'écrit jamais de registre de surface | `M3-WRITES-PROFILE.md` (round 92) | oui |
| Les HLE GCM d'affichage ne sont jamais appelés | `M3-GCM-DISPLAY-HLE-UNUSED.md` (round 95) | oui |
| 172 fonctions liftées référencent les constantes `0x200-0x214` | `out/ppu/mc_*.cpp` | oui |

# Les fonctions candidates SONT exécutées

## L'hypothèse testée

J'ai supposé que le code qui référence les registres de surface n'était **jamais exécuté** — cohérent
avec zéro écriture. J'ai posé des gardes sur les cinq fonctions les plus chargées en ces constantes.

## Le résultat

    [surfcode] pc=0x01401210 entry lr=0x221c2c
    [surfcode] pc=0x01004f74 entry lr=0x6eda10   (× 3)

**Deux des cinq sont atteintes** : `f_002202c8` (appelée depuis `0x221c2c`) et `f_006e6d20` (depuis
`0x6eda10`).

## Ce que ça élimine

L'hypothèse « le code n'est jamais atteint » est **fausse**. Le code qui manipule les constantes de
surface **s'exécute**.

## Ce que ça laisse

Le code s'exécute **et** les registres ne sont pas écrits. Donc :

1. Les constantes `0x200-0x214` dans ces fonctions servent à **autre chose** (une structure de données
   dont les champs coïncident numériquement) ;
2. Ou le code calcule une écriture mais **ne la fait pas**, sur une branche non prise.

La première est plausible : 172 fonctions référencent ces valeurs, ce qui est trop pour du code de
configuration de surface. Mes 5 candidates étaient probablement des faux positifs.

## Ce que ça change dans la méthode

Mon filtre « fonctions contenant la constante » est **trop grossier**. Il faudrait chercher les
**écritures au FIFO** avec le registre `0x200` en argument — c'est-à-dire la valeur passée au point
d'entrée des méthodes, et non la constante dans le code.

C'est faisable : le stub reçoit déjà chaque `method(reg, arg)`. Il suffit de compter les `reg == 0x200`
au point d'arrivée — ce que fait déjà mon compteur `g_surface_regs`, qui lit **0**.

**Donc la mesure au point d'arrivée est déjà la bonne, et elle dit zéro.** Le problème n'est pas
l'instrument, c'est que le guest ne produit pas ces appels.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |

Le jalon 3 reste ouvert. Ce round **élimine l'hypothèse « code jamais atteint »** et montre que mon
filtre par constante était un faux positif. La mesure fiable reste celle du point d'arrivée : zéro
écriture de registre de surface.
