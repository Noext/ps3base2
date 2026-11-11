# Le blocage s'est deplace vers les threads SPU

## Ou le boot arrive maintenant

    I 0.816611 [4] spu: group_start 0x4000100 'MinecraftCellSpursKernelGroup'
    I 0.816672 [2] cellAudio: cellAudioInit
    I 0.821745 [2] memory: sys_memory_allocate(0x300000) -> 0x40100000
    I 0.823833 [2] cellVideoOutGetDeviceInfo -> 4 modes, first 1920x1080
    I 0.859394 [5] spu: thread 0x1000100 'MinecraftCellSpursKernel1' start pc 0x00848
    I 0.861708 [6] spu: thread 0x100 'MinecraftCellSpursKernel0' start pc 0x00848
    RuntimeError: unreachable

**Le crash du pointeur nul a disparu** (round 241) ; le boot franchit maintenant l'initialisation video, monte
le SPU et lance **deux threads SPURS**, puis echoue.

## Ce que le bloc SPU fait

    bool b16(Context& c, Host& h) {
        c.pc = 0x848;
        if (!op_brsl(c, h, 0x337fd480u, 0x844u)) return false;   // <- appel a 0x337fd480
        return true;
    }

`0x337fd480` n'est pas une adresse valide dans l'espace SPU local (128 Ko, `0x0`-`0x1FFFF`). C'est
probablement un **pointeur de fonction** lu depuis les arguments ou la LS.

## Ce que ca signifie

| fait | valeur |
|---|---|
| bloc `0x848` est lifte | **oui** |
| il appelle `0x337fd480` | `op_brsl` le 2e bloc |
| adresse dans la LS SPU | **non** |

Le SPURS kernel charge ses fonctions par adresse calculee ; le lifte les insecurise en appelant un pointeur
qui n'a pas ete initialise par le code hote.

## Ce qu'il faudrait

Tracer `op_brsl` et voir quelle adresse est reellement calculee dans `c.lsa`, puis si le bloc cible existe. Le
runtime SPU a probablement un log de cible inconnue, comme le PPU.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; le blocage PPU est resolu, un blocage SPU apparait |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |