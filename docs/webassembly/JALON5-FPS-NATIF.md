# Jalon 5 : fps natif dans le monde, extrait des logs

## La mesure

Le log du run natif de reference contient le budget par image du renderer :

    renderer per frame: draw 23.05 ms (textures 0.66 ms), present 16.28 ms, frame complete 0.00 ms

| composante | valeur |
|---|---|
| draw | **23,05 ms** |
| present | **16,28 ms** |
| frame complete | 0,00 ms |
| **total** | **39,33 ms** |
| **fps** | **25,4** |

## Ce que le present represente

`present` a 16,28 ms est **la synchronisation verticale** : 1/60 s = 16,67 ms. Le jeu est donc **limite par le
vsync**, pas par le rendu.

| hypothese | fps |
|---|---|
| avec vsync (**mesuree**) | **25,4** |
| sans present (draw seul) | 43,4 |

## Pourquoi 25 et pas 60

Si chaque image attend un vsync de 16,67 ms **et** que le travail prend 23,05 ms, l'image suivante rate le
vsync et attend le suivant : le jeu tombe a **un vsync sur deux**, soit 30 fps theoriques, moins le travail
residuel -> **25,4 mesure**.

C'est coherent avec `GOWA_FPS_UNLOCK=1`** dans l'environnement de reference : le jeu essaie de depasser 30 et
n'y arrive pas.

## Ce que le wasm ne peut pas fournir

| metrique | raison |
|---|---|
| fps wasm dans le monde | le wasm n'atteint pas le monde |
| budget draw wasm | le stub ne rend rien (`PS3BASE_NO_RENDERER`) |
| budget present wasm | idem |

Le wasm rapporte `draw 23.05 ms` equivalent ? **Non** : son rapport periodique donne le **debit de methodes**
(645k/s), pas un temps de rendu.

## Ce que le prompt demande et ou j'en suis

> fps dans le monde, wasm vs natif, sur le meme point de spawn.

**Natif : 25,4 fps, mesure.** **Wasm : non mesurable**, le jeu n'atteint pas le monde.

La comparaison **ne peut pas etre completee** tant que le wasm n'atteint pas le monde — ce qui depend du
`sys_vm_memory_map` jamais emis.

## Position

| critere du jalon 5 | etat |
|---|---|
| fps natif dans le monde | **25,4, mesure** |
| fps wasm dans le monde | **non mesurable** |
| profil du worker | **non** — pas de navigateur |
| goulots nommes | **oui** |