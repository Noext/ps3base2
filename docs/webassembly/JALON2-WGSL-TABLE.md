# Jalon 2 : la table GLSL -> WGSL

## La methode

Le prompt demande de remplacer la compilation de shaders a l'execution (glslang -> SPIR-V) par une generation
**WGSL** au moment du port.

Les deux decompilateurs (`vp_decompiler.cpp`, `fp_decompiler.cpp`) construisent le shader **dans des chaines
litterales**. J'ai donc extrait le vocabulaire en scannant ces litteraux, pas la specification GLSL.

**64 identifiants appelables** apparaissent. La table en classifie 53.

## Les quatre classes

| classe | nombre | signification |
|---|---|---|
| `Same` | 19 | meme orthographe, copier l'appel |
| `Rename` | 17 | meme semantique, nom different |
| `Type` | 15 | type ou qualificatif : la declaration change |
| `Rewrite` | 2 | **aucun equivalent** : le generateur doit produire autre chose |

## Les renommages qui comptent

| GLSL | WGSL |
|---|---|
| `inversesqrt` | `inverseSqrt` |
| `dFdx` / `dFdy` | `dpdx` / `dpdy` |
| `texture` | `textureSample` |
| `textureLod` | `textureSampleLevel` |
| `textureSize` | `textureDimensions` |
| `floatBitsToUint` | `bitcast<u32>` |
| `packUnorm2x16` | `pack2x16unorm` |

## Les deux sans equivalent

| GLSL | recette |
|---|---|
| `lit(x)` | formule demi-Lambert depliee ; `lit()` compresse quatre termes a la fois |
| `isnan(x)` | `(x != x)` -- vrai pour NaN en IEEE |

**Ce sont les deux seuls cas qui exigent de generer du code different**, et ils sont documentes avec leur
recette plutot que traduits en silence.

## Le test

    wgsl_map: 53 entries, 2 rewrites, 0 failures

Ce qui est verifie :

- toute entree `Rewrite` **n'a pas** de nom WGSL (sinon la reecriture serait cachee) ;
- les 9 renommages critiques sont classes `Rename` et leur nom change reellement ;
- `bvec4` -> vecteur de `bool` ;
- les 6 builtins partages gardent leur orthographe ;
- **`fogc` et `nrm` ne sont pas dans la table** : ce sont des helpers ecrits par le generateur, a porter a la main.

## Ce que ca ne fait pas

**Je n'ai pas ecrit le generateur WGSL.** J'ai ecrit la **table de correspondance** dont il a besoin, testee.

Ecrire le generateur demanderait de reecrire `vp_decompiler.cpp` et `fp_decompiler.cpp` pour emettre du WGSL
au lieu de GLSL -- plusieurs rounds.

## Position

| etape du jalon 2 | etat |
|---|---|
| table de formats | **fait, testee** |
| cle de pipeline | **fait, testee** |
| cache | **fait, teste** |
| **table GLSL -> WGSL** | **fait, testee** |
| generateur WGSL | non -- depend de la table, desormais disponible |
| renderer porte | non |