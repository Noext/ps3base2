# `_cellGcmInitBody` recoit exactement l'adresse du champ nul

## La mesure

    [gcminit] code entry r3=0xd72800 r4=0xfa000

`r3` = **`0x00d72800`** — l'adresse meme du champ lu comme nul.

## Ce que ca etablit

| fait | valeur |
|---|---|
| adresse passee a `_cellGcmInitBody` | **`0x00d72800`** |
| taille (`r4`) | `0xfa000` = 1 024 000 octets |
| retour de l'init | `0x80010004` (`CELL_GCM_ERROR_FAILURE`) |
| adresse lue ensuite comme pointeur | `0x00d72800 + 10240` |

**La boucle est fermee.** Le jeu demande a GCM d'initialiser une structure a `0xd72800` ; GCM echoue ; le jeu
ignore le retour et lit dans cette meme structure, qui n'a jamais ete remplie.

## La chaine complete, du debut a la fin

    1. f_002da36c  initialisation video
    2. cellVideoOutGetDeviceInfo / GetResolution   OK
    3. _cellGcmInitBody(0xd72800, 0xfa000)         -> 0x80010004  ECHEC
    4. le jeu ignore le code de retour
    5. cellGcmGetTiledPitchSize                    OK (0x1400)
    6. L_2da534 : ld32(0xd72800 + 10240) = 0       jamais ecrit
    7. f_005d2b44 : call_indirect(0)               crash

## Pourquoi `0xd72800` est coherent avec les mesures precedentes

| mesure anterieure | coherence |
|---|---|
| round 208 : page mappee | oui, l'init a echoue **apres** que la page soit fournie |
| round 220 : ecritures a `0xd729e8` et plus | **oui** — l'init a echoue, donc seuls les champs au-dela du premier bloc sont ecrits par un autre chemin |
| round 219 : `0xd72800` jamais ecrit | **oui** — l'init qui devait l'ecrire a echoue |

## Ce qu'il faut maintenant

Trouver **pourquoi** le corps LLE de `_cellGcmInitBody` (`0x33000e5c`) rend `CELL_GCM_ERROR_FAILURE`. Le corps
est du firmware : l'echec vient d'une dependance. La piste la plus directe est de tracer **les imports appeles
depuis `0x33000e5c`** et leurs retours.

C'est un travail d'instrumentation cible, pas de diagnostic : la question est desormais precise.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |