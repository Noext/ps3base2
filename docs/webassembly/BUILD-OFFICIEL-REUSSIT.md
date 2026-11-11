# Le build officiel fonctionne : 44,4 Mo, 49 631 fonctions

## Le resultat

    module: 44353904 bytes
    game.wasm: 49631 functions, largest 725073 bytes, total code 43770857 bytes
    game.js: 133132 bytes
    game.data: 126425603 bytes

**`tools/build_wasm.sh` produit un module complet.** C'est la premiere fois que le build officiel est utilise
dans ce projet.

## Les corrections apportees au script

| # | correction | raison |
|---|---|---|
| 1 | `-I third_party/stb` | `stb_image.h` |
| 2 | `-I third_party/imgui` | `imgui.h` |
| 3 | exclure `*/launcher/*` et `main.cpp` | interface native, SDL/X11 |
| 4 | exclure `host_input.cpp` | entree SDL, remplacee par `host_input_port.cpp` |
| 5 | `-I runtime/src/spu/renjob` | `renjob_chan.h` |
| 6 | exclure `*/jobsys/*` | systeme de jobs de God of War |
| 7 | exclure `*/gpu/edge*` | renderer GOWA |
| 8 | exclure `vk_context/rsx_renderer/presenter/motion_vectors/shader_compiler` | Vulkan natif |
| 9 | exclure `*_adapters.cpp`, `renjob_whole.cpp`, `jobs_whole.cpp` | generateurs GOWA |
| 10 | exclure `spu/{anim,cloth,edge,edgejob,meshhier,meshprep,mpjob,pm,renjob,scream,shadows,tasks}` | sous-systemes de jobs GOWA |

## Ce que ca revele sur le depot

**`ps3base v2` est present comme runtime generique, mais il contient un jeu entier de generateurs et de
sous-systemes SPU specifiques a God of War.** Ces fichiers exigent des headers produits par
`tests/edge/spu_cgen.py` & co, qui lisent une image a l'offset **`0x727580` code en dur** — propre a GOWA.

| element | appartient a |
|---|---|
| `runtime/src/jobsys/` | GOWA |
| `runtime/src/spu/{anim,cloth,edge,...}/` | GOWA |
| `runtime/src/spu/*_adapters.cpp` | GOWA |
| `runtime/src/gpu/edge_compute.cpp` | GOWA |

Les exclure est correct pour PS3base, et n'enleve rien : le jeu n'utilise pas ces sous-systemes.

## Ce qui reste

Le module **se construit mais ne demarre pas** : `RuntimeError: unreachable` dans `initRuntime`.

C'est un probleme different de tout ce que le round a traite : le binaire est complet, il faut trouver ce qui
echoue a l'initialisation.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 5 criteres sur 6 ; **build officiel reproductible, module 44,4 Mo, demarrage a corriger** |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |