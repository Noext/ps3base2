# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| Les deux drapeaux 0xbe0704/0xbe070c valent 0 | round 128 | oui |
| Le code contient un xor 1 + st32(r30+1796) | mc_028.cpp:21597-21600 | **oui — un toggle apparent** |
| **0 basculement sur 200 échantillons rapides** | mesure | **oui — le toggle ne s'exécute pas** |
| r30 = 0x00d70000 (14090240) | calcul | oui |

# Le toggle existe, mais j'avais la mauvaise adresse

## Le code observé

    ctx.gpr[3] = ld32(base, r30 + 1796);      // r30 = 0x00d70000
    ctx.gpr[3] = ctx.gpr[3] ^ 0x1;
    ppu::st32(base, r30 + 1796, gpr[3]);      // toggle

## La mesure

    [pollflag] t=39s  0xbe0704=0 toggles_in_200=0
    [pollflag] t=79s  0xbe0704=0 toggles_in_200=0

**200 échantillons consécutifs, zéro changement.**

## Ce que ça révèle

**Je me suis trompé d'adresse.** Le toggle écrit à r30 + 1796 avec r30 = 0x00d70000, soit
**0x00d70704** — pas 0x00be0704.

f_002dc238, lui, lit r31 + 1796 avec r31 = 0x00be0000, soit **0x00be0704**.

Ce sont **deux adresses différentes** :
- 0x00be0704 est **lu** (par f_002dc238) ;
- 0x00d70704 est **écrit** (le toggle).

## Ce que ça signifie

Le drapeau lu **n'est jamais écrit** par ce code. Il reste à 0 en permanence.

C'est cohérent avec tous mes échantillons — et cela signifie que f_002dc238 lit un drapeau **constant**,
donc sa condition est fixe.

## Ce qui reste

Trouver **qui écrit 0x00be0704** dans le code lifté. Si personne ne l'écrit, le guest attend un signal
qu'aucun code n'émet — cohérent avec un état non initialisé.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| sys_vm_memory_map -> | non (0 contre 2) |
