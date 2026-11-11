# Renversement : le syscall 300 EST emis

## Ce qui a change

Le round 179 a etabli que mes gardes remplacent le corps des fonctions gardees (hook_call au lieu de
l'execution). Les rounds 124, 177 et 178 avaient donc mesure a travers un instrument qui supprimait ce qu'il
mesurait.

J'ai retire toutes les gardes de la chaine sauf une, sur f_0021c42c.

## La mesure propre

    mapemit: 4
    [mapemit] pc=0x01004f74 lr=0x331740
    [mapemit] pc=0x013049f8 lr=0x2da908
    [mapemit] pc=0x005ec0ac lr=0x288e80
    [mapemit] pc=0x005ec0ac lr=0x2f07d0
    menu: 1
    crash: 0

f_0021c42c est atteint quatre fois, depuis quatre des six appelants identifies au round 178 :

| appelant | adresse de retour |
|---|---|
| f_003316d4 | 0x331740 |
| f_002da6f0 | 0x2da908 |
| f_00288e24 | 0x288e80 |
| f_002f0750 | 0x2f07d0 |

## Ce que ca renverse

| round | conclusion | statut |
|---|---|---|
| 124 | le code du map est atteint | CORRECT — le round 177 l'avait declare faux a tort |
| 177 | f_0021c42c n'est jamais atteinte | FAUX — artefact de gardes imbriquees |
| 178 | aucun des six appelants n'est execute | FAUX — meme artefact |
| 179 | les gardes remplacent le corps | CORRECT, et c'est la cause des deux precedents |

## Ce que ca signifie pour le blocage

Le syscall 300 est emis quatre fois. Or le handler sys_vm_memory_map n'est jamais entre (round 125, mesure
sans garde sur cette fonction).

Le blocage n'est donc pas l'emission, c'est le handler.

## Ou regarder maintenant

Le handler LV2_SYSCALL(300, ...) commence par LOG_INFO avant toute validation. Il n'apparait jamais dans les
logs. Quatre emissions sans entree dans le handler signifie :

1. le dispatch de syscall ne route pas 300 vers le handler, ou
2. ctx.gpr[11] = 300 n'est pas le numero reellement transmis, ou
3. l'appel ppu::syscall(ctx, base) est saute par une branche.

C'est la premiere fois que cette question se pose avec une mesure fiable derriere.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 5 criteres sur 6 |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |