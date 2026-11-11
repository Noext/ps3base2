# Round 190 : la seconde iteration du lift, et le vrai etat de la liaison

## Ce qui a ete fait

| etape | resultat |
|---|---|
| ajouter `0x000c2ba0` au seed | fichier a 2 lignes |
| relifter | 23 302 fonctions, `f_000c2ba0` **presente** |
| recompiler les 100 sources | **0 echec** |
| relier | **incomplet** |

## Le lift fonctionne

    seeds: 2 indirect target(s) from out-mc/gen/indirect_seeds.txt
    total functions 23302
    f_000f656c : present
    f_000c2ba0 : present

**Deux iterations, deux fonctions liftées.** Le mecanisme est fiable.

## La liaison ne converge pas

J'ai passe le round a resoudre des symboles un par un :

| manquant | objet fournisseur |
|---|---|
| `spu::hle::g_after_install` | `sp_hle_builtin` |
| `edge::gpu::g_active` | `e_edge_gpu` |
| `pm::hooks`, `wj::run` | `r_gowa_stubs` |
| `gpu::rsx::method_run` | `g_rsx_no_display` |
| `edge2xo::f_073e0` | `e_edge_adapters` |

Chaque ajout en revele d'autres. A la fin il reste :

    undefined symbol: wj::ext_stats(unsigned long long&)
    undefined symbol: edge2xo::f_073e0, f_0d618
    undefined symbol: edge::ovl::count

## Ce que j'ai appris

**Mon jeu d'objets etait un artefact accumule, pas une configuration reproductible.** J'ai ajoute et retire des
objets pendant vingt rounds ; l'ensemble est incoherent.

La cause : `tools/build_wasm.sh` echoue sur `stb_image.h` manquant, donc je n'ai jamais utilise le build
officiel. J'ai maintenu une liste d'objets a la main.

## Le fait mesurable qui reste

Le round 189 a obtenu un module qui **creait 12 threads** et bloquait sur `0xc2ba0`. Ce module (`ps3i2`/`ps3i8`)
utilisait les objets frais de `gen190` combines a l'ancien set runtime.

**Ce resultat reste valide** : le lift de `0xf656c` a fait avancer le jeu.

## Ce qu'il faudrait

Executer `tools/build_wasm.sh` apres avoir fourni `stb_image.h`, pour retrouver un ensemble d'objets
**reproductible** au lieu de ma liste manuelle.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 5 criteres sur 6 ; **deux fonctions liftées, 12 threads atteints, liaison non reproductible** |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |