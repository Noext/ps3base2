# La cible SPU est valide et le bloc est lifte

## Le decodage

Le bloc `b16` de `spu_000.cpp` contient :

    if (!op_brsl(c, h, 0x337fd480u, 0x844u)) return false;

`0x337fd480` est la **word d'instruction**, pas une adresse cible. `op_brsl` prend ses 16 bits bas,
les signe-etend, decale de 2 et ajoute au PC :

    imm16 = 0xd480 = -11136
    cible = (0x844 + (-11136 << 2)) & 0x3FFFF = 0x35a44

`0x35a44` est **dans** la LS SPU (256 Ko), et le bloc y est **lifte** (`spu_061.cpp`, `spu_q086.cpp`).

## Ce que ca elimine

| hypothese | statut |
|---|---|
| le bloc appelle hors de la LS | **faux** — cible `0x35a44` |
| le bloc cible n'est pas lifte | **faux** — il existe |
| `op_brsl` sort de la LS | **faux** — il masque avec `PC_MASK` |

## Ce que ca laisse

Le `unreachable` n'est ni un symbole manquant (round 242), ni un bloc SPU absent (ce round). Les pistes
restantes :

| piste | indice |
|---|---|
| un `ppu::syscall` non implemente rend un code d'erreur traite comme fatal | aucun log |
| l'interpreteur SPU de secours rencontre une instruction inconnue | pas de log `spujob` |
| un `abort()` explicite dans un module charge | a chercher |

## Ce qu'il faudrait

Obtenir le nom de la fonction wasm qui a fait `unreachable`. Le build n'emet pas de section de noms ; les
indices `wasm-function[37425]` ne disent rien. **Construire avec les noms** (option `-g` ou
`-sASYSCALL_DEBUG`/`--profiling`) donnerait le nom en une passe.

C'est la mesure la plus rentable du moment : sans nom, je devine ; avec, je regarde.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; blocage PPU resolu, blocage SPU ensuite |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |