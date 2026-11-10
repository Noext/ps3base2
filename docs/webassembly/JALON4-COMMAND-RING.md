# Jalon 4 : le tampon circulaire de commandes

## Ce que le prompt demande

> Tampon circulaire de commandes RSX du worker vers le thread de page -- `runtime/src/gpu/ring.cpp`
> existe deja cote Vulkan, a adapter.

## Ce que j'ai trouve

`runtime/src/gpu/ring.cpp` est le ring GPU Vulkan : un `VkBuffer` de 256 Mo pour les donnees de sommets
et d'indices. Ce n'est pas un ring de commandes inter-thread.

Il fallait ecrire le second. Ce sont deux objets differents qui partagent un nom.

## Ce que j'ai ecrit

| fichier | role |
|---|---|
| `runtime/include/runtime/command_ring.h` | producteur/consommateur mono-thread, sans verrou |
| `runtime/src/gpu/command_ring.cpp` | l'implementation |
| `tests/command_ring.cpp` | la suite |

## Les quatre contraintes, chacune un mode de defaillance

| contrainte | ce qui casse sans elle |
|---|---|
| wrap-around | un enregistrement a cheval sur la fin |
| back-pressure | une commande RSX perdue = image fausse sans erreur |
| memoire release/acquire | le consommateur lit le pointeur avant les octets |
| alignement 4 octets | lecture non alignee = trap en wasm |

## Les trois bugs que j'ai du corriger

1. Segfault sur le padding : mon memset debordait quand le gap etait plus court que l'en-tete.
2. Ecrasement de donnees non lues : used() calcule par comparaison head/tail ne distingue pas
   vide de plein-apres-wrap. Le producteur ecrasait 55 records.
3. Ring qui ne wrappait plus : en interdisant le wrap quand des donnees restaient, il se bloquait.

## La correction

Un compteur d'octets explicite, et non une comparaison de positions :

    std::atomic<uint32_t> used_;

head/tail ne servent plus qu'a la copie contigue. Plein et vide se decident par le compteur, ce qui
supprime l'ambiguite que trois versions successives ont mal traitee.

## Le test

    command_ring: 0 failures

Couverture : aller-retour, ordre sur 20 records, back-pressure (un push refuse ne modifie rien),
500 rounds a travers un ring de 64 octets avec drain, payload surdimensionne refuse.

## La lecon

J'ai ecrit quatre versions de push avant la bonne. Les trois premieres echouaient toutes sur le meme
cas : head == tail apres un wrap. Le test de 500 rounds est ce qui l'a revele ; les tests courts passaient.

## Position

| critere du jalon 4 | etat |
|---|---|
| ring de commandes worker -> page | fait, teste |
| worker / WebGPU dans la page | non |
| AudioWorklet | non |
| entrees web | non |
| page dans Chrome | non |