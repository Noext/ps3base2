# Le build officiel : quatre obstacles resolus, un cinquieme bloquant

## Ce que le round a trouve

`tools/build_wasm.sh` **n'avait jamais tourne**. Je maintenais une liste d'objets a la main depuis vingt rounds,
ce qui explique l'incoherence du round 190.

## Les obstacles resolus

| obstacle | cause | correction |
|---|---|---|
| `stb_image.h` introuvable | include absent | `-I third_party/stb` ajoute |
| `imgui.h` introuvable | include absent | `-I third_party/imgui` ajoute |
| launcher natif compile | pas exclu | `-not -path '*/launcher/*' -not -name main.cpp` |
| `SDL3/SDL.h` introuvable | `host_input.cpp` est l'entree SDL native | exclu (wasm utilise `host_input_port.cpp`) |
| `renjob_chan.h` introuvable | include absent | `-I runtime/src/spu/renjob` ajoute |

## L'obstacle bloquant

    runtime/src/jobsys/jobs_whole.cpp:9: fatal error: 'wj_gv_gen.h' file not found

**`wj_gv_gen.h` n'existe nulle part sur la machine.** C'est un fichier **genere** par l'etape `natives` du
pipeline, qui n'a jamais tourne :

| repertoire | contenu |
|---|---|
| `out/gen/spu/` | **vide** (un `generated.txt` seulement) |
| `out-mc/gen/spu/` | **inexistant** |

Ces headers sont produits par `tools/setup.py --steps natives`, qui exige les blocs SPU extraits du disque —
une etape que je n'ai pas lancee et qui demande une execution complete du pipeline.

## Ce que ca explique

Pourquoi mon jeu d'objets etait incoherent : j'ai ajoute les objets que le linker reclammat, un par un, sans
jamais reconstruire depuis la source. Le build officiel **revele l'ordre correct** et les dependances reelles.

## Ce qui est acquis

| acquis | etat |
|---|---|
| deux fonctions liftees (`0xf656c`, `0xc2ba0`) | **fait, verifie** |
| 100 sources d'images compilees | **0 echec** |
| `build_wasm.sh` repare (5 corrections) | **fait** |
| headers SPU generes | **absents** |

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 5 criteres sur 6 ; lift a jour, build officiel repare, headers SPU manquants |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |