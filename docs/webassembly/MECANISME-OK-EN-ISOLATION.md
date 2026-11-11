# Le mecanisme fonctionne en isolation, pas dans le runtime

## Le test isole

`tests/hook_guard.cpp` reproduit le chemin reel — `g_hooks` lookup, `impl_body`, `h->guard(ctx)`, puis le corps
— contre une entree synthetique :

    flag=1 impl=armed
    bodies=3 guards=3
    HOOK-MECHANISM-OK

**Le garde tire trois fois pour trois appels.** Le mecanisme `hook_flags` + `hook_call` + `impl_body` est
correct.

## La premiere version du test prouvait le contraire

Mon premier jet avait un dispatcher qui appelait le corps **sans consulter le garde**, et imprimait
`HOOK-MECHANISM-OK`. C'etait un test qui validait sa propre erreur.

C'est exactement la classe d'erreur des gardes eux-memes : un instrument qui ne couvre pas ce qu'il pretend
couvrir. Je l'ai corrige en reproduisant `impl_body` ligne pour ligne.

## Ce qui reste ouvert dans le runtime reel

| verification | resultat |
|---|---|
| index du drapeau dans le wrapper | `0x170a51` |
| index calcule pour `0x5d2b44` | **`0x170a51`** — identiques |
| `set_hook_flag` reussit | oui, aucun avertissement |
| garde unique apres nettoyage | oui (un doublon retire) |
| garde tire dans le run | **non** |

## Le fait qui separe les deux cas

Dans le test, `impl` est atteint parce que `hook_impl` est non nul. Dans le runtime reel, `hook_impl` est arme
par `add_guard` lui-meme (`hooks.cpp:683`) — mais `hooks::init()` a **deja** tourne, et il contient un
`if (!g_hooks.empty() && !ppu::hook_impl) ppu::hook_impl = impl;` conditionnel.

Reste a verifier si `hook_impl` est reellement non nul **au moment ou `f_005d2b44` est appele**, ce qui demande
d'imprimer sa valeur depuis le runtime, pas de la deduire.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 44,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |