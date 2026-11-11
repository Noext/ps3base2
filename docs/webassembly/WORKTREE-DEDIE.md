# Un worktree dedie, et pourquoi il fallait en arriver la

## Le probleme

Un second agent travaille dans `/root/Dev/ps3base2` sur le **meme depot, la meme branche `main`** :

| observation | mesure |
|---|---|
| `gowa_stubs.cpp` modifie pendant que je l'edite | mtime a **2 secondes** de mon ecriture |
| definitions ajoutees puis retirees | 4 fois pour `g_active` |
| commits sur `main` | `499b4b2 runtime: remove the fourth jsedge duplicate, and guard against the pattern` |

Mes ajouts de stubs etaient **ecrases en quelques secondes**, et mes builds liaient un melange de deux etats.

## La solution

    git worktree add /root/Dev/ps3base2-wasm -b wasm-main main

Un arbre de travail **separe**, sur sa propre branche. Les repertoires volumineux et ignores (`out`, `out-mc`,
`bin`, `third_party`) sont des liens vers l'original : ils ne sont pas dupliques, ils sont partages en lecture.

## Ce que ca a revele immediatement

Une fois isole, le diagnostic est devenu simple :

| tentative | resultat |
|---|---|
| stub `compiled_blocks` | **doublon** — `gowa_stubs.cpp` le fournit deja |
| stub `jsedge::*` | **doublon** — idem |
| stub `g_active` | **necessaire** — aucun fichier lie ne le definit |

L'agent parallele avait deja resolu trois des quatre symboles. Sans le worktree, je n'aurais pas pu le voir :
le fichier changeait sous mes yeux.

## Le fichier minimal qui reste

`runtime/src/ps3base_wasm_stubs.cpp`, 867 octets, **une seule definition** :

    namespace edge { namespace gpu { bool g_active = false; } }

Avec son commentaire qui explique pourquoi il est seul dans son unite de traduction.

## Un detail qui a coute du temps

Le `while read` du script de build **n'a pas compile** ce fichier, alors que `find` le listait : les flags du
`find` filtrent les repertoires GOWA, et le fichier etait untracked au moment du premier build. Une fois
l'objet compile manuellement dans `obj/`, le lien a reussi (`build exit=0`).

C'est un **defaut de mon script**, pas du depot : il faudra ajouter un `find` de verification.

## L'etat mesure

    build                    exit=0, 17 symboles GOWA non definis (inoffensifs)
    run                      threads 2
    dernier evenement        unknown indirect target 0x00000000 (lr=0x005d2c08)

Le module se lie et demarre ; il atteint un **appel de methode virtuelle sur un pointeur nul**.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 5 criteres sur 6 ; module officiel lie et demarrant |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |