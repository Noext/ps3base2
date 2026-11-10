# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Profil RSX : 430k sémaphores, 2,9 M draws, 0 surface, 0 texture | `M3-WRITES-PROFILE.md` (round 92) | oui |
| `libfiber.sprx` chargé des deux côtés, HLE, même adresse | `/tmp/def.log`, `/tmp/cmp.log` | **oui — éliminé** |
| `libspurs_jq` chargé des deux côtés | idem | oui |

# Ce que la comparaison par événements élimine encore

## `libfiber`

Le natif charge `libfiber.sprx` à t=4,57 s, juste avant ses surfaces. J'ai vérifié : **le wasm le charge
aussi**, à t=10,1 s, avec le **même statut HLE** et la **même adresse** `0x23047400`.

Ce n'est pas la différence — et c'est le fichier qu'un opérateur m'avait demandé de chercher au
round 34, que je n'avais pas trouvé. Il est là, dans le jeu, chargé normalement.

## Les sons

| | natif | wasm |
|---|---|---|
| ouvertures `Minecraft.msscmp` | **373** | **373** |
| première ouverture | t=4,29 s | t=9,48 s |

**Identique.** Le chargement audio se fait complètement des deux côtés.

## Les assets

| | natif | wasm |
|---|---|---|
| `MediaPS3.arc` | 49 | 27 |
| `pngdec` | 941 | 911 |
| `cellPngDec` | 106 | 103 |
| `msscmp` | 373 | **373** |
| `libfiber` | 2 | **2** |
| `libspurs_jq` | 2 | **2** |

**Le wasm charge et décode l'essentiel des mêmes assets.** Ce qui manque n'est pas une ressource.

## Ce que ça laisse

La différence n'est ni dans les fichiers, ni dans les bibliothèques, ni dans le décodage. Elle est dans
**l'ordre du travail** :

| natif | wasm |
|---|---|
| décode PNG → **crée des surfaces** → lie des textures → crée les threads d'affichage | décode PNG → charge les sons → allocations... et ne crée pas de surface |

Le wasm fait le même travail mais **ne franchit pas le pas suivant** après le décodage.

## L'hypothèse la plus probable

Le jeu décide s'il peut créer une surface en **interrogeant le matériel**. Notre HLE
`cellGcmGetConfiguration` (round 26) renvoie des valeurs fixes — peut-être pas celles qu'attend le jeu.

C'est testable : comparer ce que le jeu lit via `cellGcmGetConfiguration` avec ce qu'un vrai RSX
renvoie.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |

Le jalon 3 reste ouvert. Ce round **élimine trois pistes** (libfiber, sons, assets) par comparaison
d'événements, et oriente vers une valeur de configuration RSX qui ne satisfait pas le jeu.
