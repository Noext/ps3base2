# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Un backend RSX « null » headless existe : « a host-memory framebuffer with a CPU triangle filler -- enough to prove the guest's command stream produced the pixels it asked for » | `refs/jdc-ps3recomp/CHANGELOG.md:314-318` | **oui** — c'est le modèle que j'ai suivi, et il n'était pas dans notre build |
| Le backend null lit les mêmes valeurs neutres que D3D12/Metal parce que l'abstraction a été extraite (#101-#103) | `refs/jdc-ps3recomp/CHANGELOG.md:320-324` | oui — nous n'avons pas cette abstraction, d'où le stub qui compte sans lire |
| Linux par défaut reste le backend headless | `refs/jdc-ps3recomp/README.md:53,66` | oui — confirme que c'est un chemin supporté, pas un bricolage |

# Ce que j'ai mesuré, et l'erreur que j'ai faite

## La mesure solide

Le guest soumet une quantité massive de géométrie :

    fb tris=3,981,415 verts=12,005,655   (à t≈180 s)
    shaders vp=237,587 fp=237,587

**3,98 millions de triangles et 12 millions de sommets.** Le titre ne « fait pas semblant » :
il envoie du travail géométrique réel, avec un vertex et un fragment program par draw.

## L'erreur

J'ai écrit un rasteriseur CPU qui remplit un framebuffer 256×256 avec des **triangles
synthétiques** — un triangle par trois sommets, positionné par une formule arbitraire. Puis
j'ai mesuré :

    hash=5e509dc5 colors=1     (constant, y compris à tris=0)

**Le framebuffer est uniforme et son hash ne change jamais.** Deux raisons, et la seconde est
la vraie :

1. Mes `clear` écrasent mes triangles : l'image est remise à plat avant d'être lue.
2. Surtout : **la géométrie est synthétique**. Elle ne dit rien du contenu réel. Un hash
   constant sur des triangles inventés ne prouve pas que le guest dessine quelque chose —
   il prouve seulement que mon code de test est mal placé.

Le modèle du refs est juste, mais il exige d'exécuter le **vertex program** pour obtenir les
positions réelles. Mon stub ne l'exécute pas, donc mes triangles ne représentent rien.

## Ce que je ne prétends pas

Je n'ai pas prouvé que le guest dessine du contenu réel. J'ai prouvé qu'il **soumet de la
géométrie** (3,98 M triangles) et des programmes (237 k paires VP/FP), ce qui est différent et
déjà mesuré au round 45.

## Ce qui reste vrai et utile

1. `fp_decompiler.cpp` — le générateur GLSL depuis les mots RSX — **compile pour wasm** et
   n'appelle aucun `vk*`. C'est le maillon portable de la chaîne.
2. `refs/jdc-ps3recomp` fournit un modèle de backend headless qui va jusqu'au remplissage de
   triangles, avec des valeurs neutres partagées entre backends.
3. La chaîne manquante est identifiée : compiler le GLSL et exécuter le vertex program pour
   obtenir des positions réelles. Ni l'un ni l'autre n'est dans notre build.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | oui, 55 Mo |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | non |

Le jalon 3 reste ouvert. Ce round applique la règle « refs d'abord » — qui donne un modèle
utilisable, `refs/jdc-ps3recomp` — et constate que mon implémentation partielle ne mesure
rien, parce qu'un rasteriseur sans vertex program ne rend pas la géométrie du guest.
