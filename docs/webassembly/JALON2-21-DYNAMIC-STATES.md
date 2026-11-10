# Jalon 2 : le prompt sous-estime les états dynamiques

## Ce que le prompt dit

> WebGPU n'a pas d'`dynamicState` (**2 états dynamiques aujourd'hui**). Il faut un **cache de pipelines
> indexé par état** (blend/depth/format).

## Ce que le code contient

| | valeur |
|---|---|
| états dynamiques **déclarés** | **21** |
| **setters distincts** appelés | **20** |
| ce que le prompt annonce | **2** |

## La liste

    BLEND_CONSTANTS          CULL_MODE              DEPTH_BIAS
    DEPTH_BIAS_ENABLE        DEPTH_BOUNDS           DEPTH_BOUNDS_TEST_ENABLE
    DEPTH_COMPARE_OP         DEPTH_TEST_ENABLE      DEPTH_WRITE_ENABLE
    FRONT_FACE               LINE_WIDTH             PRIMITIVE_RESTART_ENABLE
    PRIMITIVE_TOPOLOGY       SCISSOR                STENCIL_COMPARE_MASK
    STENCIL_OP               STENCIL_REFERENCE      STENCIL_TEST_ENABLE
    STENCIL_WRITE_MASK       VERTEX_INPUT_BINDING_STRIDE   VIEWPORT

Les 20 setters : `vkCmdSetBlendConstants`, `SetCullMode`, `SetDepthBias`, `SetDepthBiasEnable`, `SetDepthBounds`,
`SetDepthBoundsTestEnable`, `SetDepthCompareOp`, `SetDepthTestEnable`, `SetDepthWriteEnable`, `SetFrontFace`,
`SetLineWidth`, `SetPrimitiveRestartEnable`, `SetPrimitiveTopology`, `SetScissor`, `SetStencilCompareMask`,
`SetStencilOp`, `SetStencilReference`, `SetStencilTestEnable`, `SetStencilWriteMask`, `SetViewport`.

## Ce que ça change pour le jalon 2

Le cache de pipelines doit être indexé par **21 champs d'état**, pas 2. C'est un facteur **10,5** sur la
dimension du cache.

Et le coût est réel : RSX change d'état **par draw**. Avec 21 états dynamiques, chaque combinaison non vue
provoque une **création de pipeline** — l'opération la plus chère de WebGPU.

## Ce qui est réalisable

Un cache à clé structurée sur ces 21 champs est du code pur, testable sans device :

| champ | source |
|---|---|
| blend | `VkPipelineColorBlendAttachmentState` |
| depth | test/écriture/compare |
| stencil | test/op/référence/masques |
| topologie | `PRIMITIVE_TOPOLOGY` |
| raster | `CULL_MODE`, `FRONT_FACE` |
| format | cible de rendu |

## Ce que je n'ai pas fait

Je n'ai pas écrit ce cache. Le round a servi à **mesurer l'écart avec le prompt** — 2 contre 21 — ce qui
change la conception avant de l'écrire.

## Position

| critère du jalon 2 | état |
|---|---|
| table de formats | **fait, testée** |
| nombre réel d'états dynamiques | **21, mesuré** (le prompt dit 2) |
| cache de pipelines | **non commencé**, dimension désormais connue |
| renderer porté | **non** |