# Le pointeur nul : la chaine complete, par deux mesures independantes

## Le garde tire enfin

    [entry544] lr=0x002da54c r3=0x0 r4=0xff

Le doublon de garde etait la cause du silence : deux `add_guard` sur `0x5d2b44` (lignes 200 et 217), le second
ecrasant le premier et loguant `replaced by jobsys`. Une fois le doublon retire : `entry544=1`, `replaced=0`.

## L'appelant, confirme

    L_2da534:
        gpr[29] = 0x00d70000;
        gpr[30] = 0x00be0000;
        gpr[4]  = 255;
        gpr[3]  = ld32(gpr[29] + 10240);   // ld32(0x00d72800)
        gpr[5]  = ld32(gpr[30] + 1808);
        lr = 0x2da54c; f_005d2b44(ctx, base);

`f_005d2b44` fait `r31 = r3`, donc `r31 = 0`, donc `ld32(r31 + 12) = 0`, donc `call_indirect(0)`.

**Deux mesures independantes le confirment** : le `lr` du garde (`0x2da54c`) et le registre a l'entree
(`r3=0`).

## L'adresse n'est pas mappee — verifie par calcul

Les cinq regions allouees avant le crash :

| base | taille |
|---|---|
| `0x10000000` | `0x10000` |
| `0x10010000` | `0x80000` |
| `0x10090000` | `0x10000` |
| `0x40000000` | `0x100000` |
| `0x40100000` | `0x300000` |

`0x00d72800` n'est dans **aucune**. Le backend memoire est un `calloc` de 2304 Mo : la lecture rend **zero** au
lieu de faulter.

## Ce que ca etablit

| lecture | statut |
|---|---|
| bug de lift | ecarte — le code fait ce que le binaire dit |
| bug de liaison | ecarte — plus de symbole manquant sur ce chemin |
| champ jamais ecrit par le PPU | **confirme** — aucune ecriture `st32` a `+10240` |
| **region non mappee** | **confirme par calcul** |

## Ce que ca implique

Le jeu lit un pointeur dans une zone qu'il n'a pas encore allouee. Soit :

1. une allocation anterieure manque (le chargeur ou liblv2 devrait couvrir `0x00d70000`), ou
2. le jeu attend que cette zone soit remplie par un DMA RSX/SPU, et le stub no-renderer ne le fait pas.

La seconde est plausible : `0x00d70000` est sous `0x10000000`, dans la plage des buffers graphiques.

## Ce que les trois erreurs d'instrumentation ont coute

| round | erreur | consequence |
|---|---|---|
| 197 | probe `ld32` absent des TU `mc_*` | « jamais lu » — faux |
| 199 | `lr` du crash confondu avec celui d'une frame | mauvais import |
| 203 | `ROOT` code en dur dans le script | vingt rounds sur le mauvais binaire |

La quatrieme — le doublon de garde — a masque la mesure pendant deux rounds de plus.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 44,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |