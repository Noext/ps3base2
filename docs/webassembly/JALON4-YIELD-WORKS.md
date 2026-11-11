# Jalon 4 : la boucle qui cede, sans Asyncify

## Le probleme

`host::yield()` en wasm faisait `std::this_thread::yield()`. Dans un worker, la boucle d'evenements ne
tourne que lorsque la tache courante se termine : ceder au planificateur de threads ne fait rien.

La reference utilise un `MessageChannel` via `EM_ASYNC_JS` -- qui exige Asyncify.

## Ce que j'ai mesure

| essai | resultat |
|---|---|
| `EM_ASYNC_JS` sans flag | `Asyncify is not defined` |
| `-sASYNCIFY` | `wasm-opt` echoue, aucun module |
| `emscripten_thread_sleep(0)` sans flag | fonctionne |

Le troisieme essai est la cle : `emscripten_thread_sleep` n'exige pas Asyncify et pompe la file du worker.

Verifie isolement d'abord : un programme de deux lignes appelant `emscripten_thread_sleep(50)` sans flag
imprime les deux lignes dans l'ordre.

## La solution

    void yield() { emscripten_thread_sleep(0.0); }

Un sommeil de duree nulle pompe la file sans ajouter de temps de trame.

## Le resultat

    exit: 124          (timeout, donc le run a dure 200 s)
    menu: 1            (la ligne de menu est emise)
    asyncify: 0        (aucune erreur Asyncify)
    threads: 26
    draws: 9k/s
    crash: 0

    ps3base-wasm: MAIN MENU REACHED -- menu screen thread 3 times, 3 interface threads corroborate

## Ce que ca etablit

1. La boucle cede correctement sans Asyncify ni wasm-opt casse.
2. Le module tourne 200 s avec 26 threads et 0 crash.
3. La ligne de menu reste emise -- le critere du jalon 3 tient apres ce changement.

## Ce que je n'ai pas fait

Je n'ai pas branche ce yield sur un vrai MessageChannel : emscripten_thread_sleep passe par le proxy
d'Emscripten, qui pompe la file. Le resultat observable est le meme, mais le mecanisme interne differe de la
reference.

C'est une divergence assumee, documentee, avec sa raison mesuree.

## Position

| critere du jalon 4 | etat |
|---|---|
| ring de commandes | fait, teste |
| boucle qui cede | fait, mesure sans Asyncify |
| worker / WebGPU dans la page | non |
| AudioWorklet | non |
| page dans Chrome | non |