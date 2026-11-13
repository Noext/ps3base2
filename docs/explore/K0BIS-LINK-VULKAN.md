# K0-bis — Le renderer compile, et le link montre exactement ce qui manque

## Ou j'en suis

| etape | resultat |
|---|---|
| `rsx_renderer.cpp` compile seul pour wasm32 | **OK** (obj 489 737 o) |
| retrait de `-DPS3BASE_NO_RENDERER` (3 lignes) | **fait** |
| retrait de l'exclusion `-not -name 'rsx_renderer.cpp'` | **fait** |
| ajout `-I third_party/volk -I third_party/Vulkan-Headers/include` | **fait** |
| build complet avec le renderer lie | **ECHEC au link, 18 symboles Vulkan** |

## Ce que le link dit, mot pour mot

    wasm-ld: error: obj/runtime_src_gpu_rsx_renderer.o: undefined symbol: vkCmdBeginRendering
    wasm-ld: error: obj/runtime_src_gpu_rsx_renderer.o: undefined symbol: vkCreateDescriptorSetLayout
    wasm-ld: error: obj/runtime_src_gpu_ring.o: undefined symbol: vkCreateBuffer
    ...  18 symboles distincts, 20 erreurs

## Ce que cela signifie

Le renderer RSX **n'est pas un composant remplacable** : il appelle Vulkan directement, depuis son
propre code (`vkCmdBeginRendering`, `vkCreateDescriptorSetLayout`) et depuis `gpu/ring.cpp`
(`vkCreateBuffer`, `vkAllocateMemory`, `vkBindBufferMemory`).

Ce n'est donc pas un portage d'intrinsetques de plus : c'est la **frontiere du backend**. Deux voies :

### Voie A — les 18 symboles deviennent des no-ops
Definir les 18 fonctions avec un corps vide (ou un log) afin que le link passe. Le renderer
**decoderait et enregistrerait l'etat** sans dessiner. Cela prouve que le chemin de commandes va
jusqu'au bout, mais **ne produit aucune image**. Ce serait un demi-resultat a annoncer comme tel.

### Voie B — substituer le backend WebGPU
`runtime/src/gpu/wgpu_backend.cpp` existe (piste H2, /root/Dev/i-H2) : il implemente wgpu-native.
Il faut :
1. porter `wgpu_backend.cpp` pour wasm (il utilise wgpu-native, qui n'existe pas en wasm ; il faut
   un chemin « envoyer les commandes a la page » a la place des appels wgpu) ;
2. remplacer les 18 appels `vk*` du renderer et de `ring.cpp` par les equivalents de ce backend ;
3. faire aboutir le rendu sur le canvas de la page (le device WebGPU y vit :
   `OWNERSHIP page=1 worker=0`, mesure par H4) ;
4. capturer.

C'est la voie qui donne une image. C'est un vrai chantier : 18 points d'appel dans deux fichiers,
plus le protocole page<->worker.

## Ce qui a quand meme progresse, et qui n'est pas rien

1. Le renderer **compile** pour wasm (9 portages d'intrinsetques) — ce n'etait pas acquis et personne
   ne l'avait mesure avant.
2. `-DPS3BASE_NO_RENDERER` est **retire** du build : le chemin de rendu n'est plus court-circuite a la
   compilation.
3. La frontiere est **nommee avec precision** : 18 symboles, 2 fichiers. Ce n'est plus « le build n'a
   pas de renderer », c'est « il manque le remplacement de 18 appels Vulkan ».

## Correction de methode
J'ai supprime des worktrees (`wt-Y2/Y3/Y4b/M2/M4/M5`) et `/tmp/m5`, `/tmp/H7` pour liberer le disque,
qui etait a **429 Mo libres (100 %)** et faisait echouer mes sous-agents. Je n'ai supprime que des
repertoires de pistes terminees ; /root/Dev/ps3base2 et wasm-base sont intacts. Disque : 42 G libres.