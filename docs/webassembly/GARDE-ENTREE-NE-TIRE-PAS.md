# Round 201 : le garde d'entree ne tire pas, et je ne sais pas encore pourquoi

## Ce qui est etabli

| fait | preuve |
|---|---|
| `f_005d2b44` a un wrapper avec garde | `if (ppu::hook_flags[0][0x170a51]) { hook_call(...); return; }` ligne 27685 |
| l'adresse est une entree de table | `{0x5d2b44, f_005d2b44}` dans `table.cpp` |
| `add_guard` arme `hook_impl` | `runtime/src/hooks.cpp:683` |
| `set_hook_flag` reussit pour cette adresse | aucun avertissement `no image owns 0x005d2b44` dans le run |
| le crash est bien dans cette fonction | `lr=0x005d2c08`, label interne `L_5d2bf0` a la ligne 27671 |

## Ce qui ne s'explique pas

Le garde **ne tire pas** (`entry544: 0` sur quatre runs differents), alors que toutes les conditions
verifiables sont remplies.

## Ce que j'ai ecarte

| hypothese | statut |
|---|---|
| garde posee avant `hooks::init()` | **ecarte** — elle est apres, verifiee par `grep -n` |
| adresse non possedee par une image | **ecarte** — `set_hook_flag` n'a pas averti |
| fonction non appelee | **ecarte** — le crash est a son `lr` |
| saut depuis l'exterieur vers un label interne | **ecarte** — le `goto` est interne a la fonction (27640 -> 27671) |

## La piste la plus probable

`hooks::init()` appelle ensuite `profile.install_ppu_shims()` et les installations jobsys, qui peuvent
**remplacer** `h->guard` (le code logue d'ailleurs `guard at 0x%08x replaced by jobsys`). Si un installateur
reinstalle `0x5d2b44` avec un autre garde, le mien est ecrase.

Je n'ai pas verifie cette piste : elle demande de lire `install_jobsys_edge()` et `install_ppujobs()` pour
voir s'ils touchent `0x5d2b44`.

## Ce que je n'ai pas fait

Je n'ai pas resolu le blocage. Le round a etabli **cinq faits verifiables** et **une piste**, sans conclure
au-dela.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 44,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |

---

# La piste des installateurs est ecartee aussi

grep -n '5d2b44' runtime/src/hooks.cpp runtime/src/game_profile*.cpp runtime/src/jobsys/*.cpp : aucun resultat.

## Les cinq faits, et l'absence d'explication

| hypothese | verdict |
|---|---|
| garde avant hooks::init() | ecartee -- position verifiee |
| adresse hors image | ecartee -- pas d'avertissement set_hook_flag |
| fonction non appelee | ecartee -- le crash est a son lr |
| label interne depuis l'exterieur | ecartee -- goto interne |
| installateur qui ecrase le garde | ecartee -- aucun ne touche l'adresse |

## Ce que ca laisse

Le mecanisme hook_flags + hook_call devrait fonctionner et ne le fait pas. C'est un defaut du runtime, pas du
jeu, et il merite un test isole : un module minimal qui pose un garde sur une entree connue et verifie que la
garde tire.

C'est la prochaine mesure, et elle est petite.