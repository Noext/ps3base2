# Jalon 2 — état du backend WebGPU

## Ce qui existe déjà

| élément | état |
|---|---|
| table de correspondance Vulkan → WebGPU | **`runtime/include/runtime/wgpu_map.h`** + `.cpp` |
| test dédié | **`tests/wgpu_map.cpp`**, 11 vérifications |
| résultat du test | **11 checks, 0 failures** |

## La couverture, mesurée

J'ai croisé les formats **réellement utilisés** dans le renderer et le presenter avec la table :

| | valeur |
|---|---|
| formats utilisés (`rsx_renderer.cpp` + `presenter.cpp`) | **22** |
| formats dans la table | 28 |
| **utilisés mais non mappés** | **0** |
| mappés mais inutilisés | 6 |

**Aucun format utilisé n'est absent de la table.**

## Les cas que le prompt signale, tous traités

| cas du prompt | classification |
|---|---|
| `A1R5G5B5_UNORM_PACK16` → expansion | **`Expand`** |
| `BC1/BC2/BC3` → feature `texture-compression-bc` | **`Feature`** (7 entrées) |
| `CONSTANT_COLOR`/`CONSTANT_ALPHA` + `ONE_MINUS` | **`Emulate`** (2 entrées) |

Les trois décisions que le prompt exige de documenter sont **prises et classées**.

## Les cas non directs, tous nommés

    VK_FORMAT_D32_SFLOAT_S8_UINT             Feature
    VK_FORMAT_A1R5G5B5_UNORM_PACK16          Expand
    VK_FORMAT_R5G5B5A1_UNORM_PACK16          Expand
    VK_FORMAT_R4G4B4A4_UNORM_PACK16          Expand
    VK_FORMAT_R5G6B5_UNORM_PACK16            Expand
    VK_FORMAT_BC1/2/3_*                      Feature  (7)
    VK_BLEND_FACTOR_CONSTANT_ALPHA           Emulate
    VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA Emulate
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN       Emulate
    VK_CULL_MODE_FRONT_AND_BACK              Emulate

Deux cas **supplémentaires** que le prompt ne mentionne pas : `TRIANGLE_FAN` (absent de WebGPU) et
`CULL_MODE_FRONT_AND_BACK` (absent). Ils sont identifiés, ce qui vaut mieux que de les découvrir au portage.

## Ce qui reste pour le jalon 2

| étape | état |
|---|---|
| 1. wgpu-native vs Dawn | **fait** — wgpu-native v29, déjà dans `third_party/` |
| 2. porter le renderer (55 points d'entrée `vk*`) | **non commencé** |
| 3. traduire les 28 formats | **fait et testé** |
| 4. cache de pipelines indexé par état | **non commencé** |
| 5. génération WGSL | **non commencé** |
| validation : le binaire natif atteint le menu avec WebGPU | **non** |

## Ce que je n'ai pas fait

Je n'ai pas porté le renderer. C'est **55 points d'entrée Vulkan** sur 4 626 lignes, plus le presenter. Ce
n'est pas réalisable en un round, et je ne vais pas prétendre le contraire.

## Position

| critère du jalon 2 | état |
|---|---|
| format table avec décisions documentées | **oui, testée, 0 format manquant** |
| renderer porté en WebGPU | **non** |
| le binaire atteint le menu en WebGPU | **non** |