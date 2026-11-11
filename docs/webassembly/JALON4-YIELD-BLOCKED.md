# Jalon 4 : la boucle qui cede

## Ce que le prompt demande

> Note de conception a respecter : `ps2-web-recomp` documente que les `setTimeout` imbriques sont brides a
> 4 ms et que la boucle doit ceder via un `MessageChannel`. **Ne pas reinventer.**

## Ce que j'ai trouve

`host::yield()` en wasm faisait :

    void yield() { std::this_thread::yield(); }

Dans un worker, **la boucle d'evenements ne tourne que lorsque la tache courante se termine**. Ceder au
planificateur de threads ne fait **rien** pour les messages en attente.

C'est exactement le defaut que le prompt signale.

## Le mecanisme de la reference

`ps2-web-recomp`, `runtime/src/system.cpp:644` :

    EM_ASYNC_JS(void, ps2_browser_yield, (), {
        if (!Module.yieldPort) {
            const channel = new MessageChannel();
            Module.yieldWaiters = [];
            channel.port1.onmessage = () => Module.yieldWaiters.shift()();
            Module.yieldPort = channel.port2;
        }
        await new Promise((resolve) => { Module.yieldWaiters.push(resolve); Module.yieldPort.postMessage(0); });
    });

Un canal cree **une fois**, une file d'attentes, et un `postMessage` qui revient sur une macrotache sans le
clamp de 4 ms.

## Ce que j'ai ecrit

La meme chose, adaptee : `ps3_web_yield`, appele par `host::yield()` sous wasm.

## Ce qui a echoue

| etape | resultat |
|---|---|
| compilation | **OK** |
| execution sans flag | **`Asyncify is not defined`** |
| execution avec `-sASYNCIFY` | **`wasm-opt` echoue**, aucun module produit |

`EM_ASYNC_JS` exige **Asyncify** : tout appelant doit pouvoir suspendre et reprendre la pile. Le runtime
actuel ne l'active pas, et l'activer casse `wasm-opt` sur un module de 50 Mo.

## Ce que ca implique

Le mecanisme de la reference **n'est pas transposable tel quel** : `ps2-web-recomp` tourne avec Asyncify, notre
runtime non.

Deux voies, aucune essayee jusqu'au bout :

1. **Activer Asyncify** et resoudre l'echec de `wasm-opt` (`-sASYNCIFY_IMPORTS` cible, ou `-sWASM_OPT` desactive) ;
2. **Ceder depuis la boucle JavaScript** plutot que depuis C++ : le worker rend la main apres chaque trame
   au lieu d'appeler `yield()` au milieu du code guest.

La seconde est plus proche de l'architecture du jalon 4 (emulation dans un worker, page qui possede le
device) et n'exige pas Asyncify.

## Ce que je n'ai pas fait

Je n'ai **pas** livre un `yield` fonctionnel. Le code est ecrit, compile, et echoue a l'execution pour une
raison identifiee.

## Position

| critere du jalon 4 | etat |
|---|---|
| ring de commandes | **fait, teste** |
| boucle qui cede via MessageChannel | **ecrite, echoue sans Asyncify** |
| worker / WebGPU dans la page | non |
| AudioWorklet | non |
| page dans Chrome | non |