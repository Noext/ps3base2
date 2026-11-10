# Le guest dessine : 329 885 programmes de sommets soumis, zéro compilé

## La mesure

En accrochant `method()` du stub sur les registres qui déclenchent la compilation de
shaders dans le vrai renderer :

    shaders vp=292710 fp=0 fp_ok=0
    shaders vp=311310 fp=0 fp_ok=0
    shaders vp=329885 fp=0 fp_ok=0

| | valeur |
|---|---|
| vertex programs soumis | **329 885** |
| fragment programs | **0** |

Le titre **soumet massivement du travail de rendu**. Il ne s'agit pas d'un jeu qui tourne à
vide : 330 000 changements de programme de sommets en 200 secondes, soit ~1 650 par seconde.

## Ce que ça explique

La mesure du round 36 disait `flip 93 %, draw 3k/s` contre `flip 29 %, draw 60k/s` pour un
run natif valide. La cause est maintenant nommée : **le stub compte les commandes mais ne
compile aucun shader**, donc les triangles sont soumis sans programme associé et les frames
sortent vides.

## Ce qui n'est pas un obstacle

`runtime/src/gpu/fp_decompiler.cpp` — le générateur de GLSL à partir des mots de programme
RSX — **compile pour wasm** (130 Ko d'objet) et **n'appelle aucun `vk*`**. Il n'était simplement
jamais dans le build.

Le générateur est donc portable ; c'est la **liaison** au pipeline graphique (création des
`VkShaderModule`, des pipelines, des bind groups — 84 appels `vk*` dans le renderer) qui ne
l'est pas.

## Le chemin restant, chiffré

| étape | état |
|---|---|
| décompiler les mots RSX en GLSL | **portable, compile déjà** |
| compiler le GLSL en shader GPU | besoin du renderer (wgpu ou Vulkan) |
| lier aux pipelines et dessiner | besoin du renderer |

Le jalon 2 (backend WebGPU natif) est donc exactement ce qui manque, et il est **déjà
dé-risqué** : le round 2 a mesuré 540 pipelines à 0,022 ms, tous les formats créables, et
WGSL qui compile sans glslang.

## État des critères du jalon 3

| critère | état |
|---|---|
| module sous 80 Mo | **oui, 55 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | non |

## Position

Le jalon 3 reste ouvert. Ce round établit la **chaîne causale complète** entre ce qui manque
et ce qui est observé :

    330k vertex programs soumis
      → aucun compilé (le stub ne fait que compter)
        → 93 % de flips vides
          → pas de contenu affiché
            → pas de ligne de menu

Et il montre que le maillon central — la décompilation RSX → GLSL — **est déjà portable**.
Ce qui manque est le backend graphique, c'est-à-dire le jalon 2.
