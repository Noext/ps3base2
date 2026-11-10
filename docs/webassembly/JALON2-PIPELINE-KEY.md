# Le cache de pipelines : la clé est écrite et testée

## Ce que le prompt demande

> WebGPU n'a pas d'`dynamicState`. Il faut un **cache de pipelines indexé par état** (blend/depth/format).
> RSX change d'état par draw : c'est le coût principal de ce jalon, **à mesurer**.

## Ce que j'ai écrit

| fichier | rôle |
|---|---|
| `runtime/include/runtime/pipeline_key.h` | la clé, 26 octets, POD, hachée en FNV-1a |
| `tests/pipeline_key.cpp` | 17 vérifications |

## La conception

La clé couvre **ce que WebGPU cuit dans un pipeline** et que RSX change par draw :

| champ | contenu |
|---|---|
| `blend` | enabled, 4 facteurs, 2 opérations, write mask |
| `depth_stencil` | test/écriture/compare, 7 champs de stencil avant et arrière |
| `topology` | 5 valeurs |
| `cull` / `front_face` | 3 + 2 valeurs |
| `color_format` | 10 valeurs |

**Ce qui est délibérément exclu** : viewport, scissor, line width, depth bias, blend constants. WebGPU les garde
**dynamiques** — les inclure multiplierait les créations de pipelines sans raison.

C'est le point de conception qui compte : une clé trop large crée des pipelines inutiles, une clé trop
étroite rend une mauvaise image.

## `TriangleFan` : absent par construction

L'énumération `Topology` **n'a pas** de `TriangleFan`. WebGPU ne le supporte pas : le renderer doit convertir
les éventails en bandes **avant** d'atteindre la clé. L'absence est documentée dans l'en-tête.

## `Constant` : distingué de `Zero`

Le cas que le prompt signale — `CONSTANT_ALPHA` sans équivalent — a son propre énumérateur. Un test vérifie
qu'il **ne collisionne pas** avec `Zero`, ce qui serait un bug silencieux (mélange faux plutôt que plantage).

## Le test

    pipeline_key: 17 checks, 0 failures (key 26 bytes)

| ce qui est vérifié | nombre |
|---|---|
| chaque champ modifie la clé | 13 |
| un état identique donne une clé identique | 2 |
| `Constant` ≠ `Zero` | 1 |
| la clé reste petite | 1 |

## Ce qui reste pour le jalon 2

| étape | état |
|---|---|
| table de formats | **fait, testée** |
| clé de pipeline | **fait, testée** |
| **le cache lui-même** (table de hachage + création) | non |
| renderer porté (55 points d'entrée) | non |
| génération WGSL | non |
| mesure du nombre de pipelines sur 60 s | non — dépend du port |

## Position

| critère du prompt | état |
|---|---|
| décisions de format documentées | **oui** |
| cache indexé par état | **clé faite et testée**, cache à écrire |
| nombre de pipelines sur 60 s | **non mesurable** sans le port |