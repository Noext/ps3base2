# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `cellAudioInit` / `PortOpen` / `GetPortConfig` implémentés | `refs/rpcs3/rpcs3/Emu/Cell/Modules/cellAudio.cpp` | oui — la sémantique suivie par notre fichier |
| Table HLE audio chez flOw | `refs/flow/src/hle_modules.cpp` | oui — module standard |
| Notre `cellAudio.cpp` : 7 fonctions HLE | `runtime/src/hle/cellAudio.cpp:239-309` | oui |

# Le port, en étapes compilées

Le round 47 avait échoué en éditant trois choses à la fois. Cette fois, une étape, une
compilation.

**Étape 1 — l'include.**
`#include <SDL3/SDL.h>` devient conditionnel. Compilation : seules les *utilisations* de SDL
échouent, l'include passe.

**Étape 2 — le struct `Output`.** Un `#elif defined(__wasm__)` entre le bras Win32 et le bras
POSIX :

    struct Output {
        bool open() { return false; }
        uint64_t underruns = 0;
        void write(const float* stereo, uint32_t n) { (void)stereo; (void)n; }
    };

Compilation : **zéro erreur**.

**Étape 3 — rien.** J'avais prévu de porter le thread de pompage (`std::chrono`). J'ai testé
le **lien** avant : **0 erreur**. `std::chrono` lie parfaitement ici.

C'est le résultat le plus utile du round : **mon diagnostic du round 47 était partiellement
faux.** J'avais attribué l'échec à `std::chrono` ; le vrai obstacle était uniquement l'include
SDL et le struct.

# L'effet

    I cellAudio: cellAudioInit
    I cellAudio: port 0 open: 2 ch, 32 blocks at 0x100b0000
    I cellAudio: audio stats: 938 blocks, 2 late wakeups, 0 stale blocks, 0 device

| | avant | après |
|---|---|---|
| `unimplemented cellAudio` | **9** | **0** |
| port audio ouvert | non | **oui**, 2 canaux × 32 blocs |
| blocs traités | — | **938** |
| frames rendues | arrêt à t≈10 s | **11 221** |
| `unknown indirect target` | oui | **aucun** |

**L'audio fonctionne réellement** : le thread de pompage consomme des blocs à 5,33 ms, avec 2
réveils tardifs sur 938. Ce n'est pas un stub qui renvoie `CELL_OK` — c'est le protocole de
ports complet, seule la sortie sonore est absente.

# La limite retrouvée

Avec le filtre de couverture, le chemin audio+START casse sur `0x0086aab0` — présent dans la
table complète, absent de la filtrée. **C'est la troisième fois** que ce schéma apparaît : un
nouveau parcours atteint une fonction que le profil n'a pas vue.

Avec la table complète : 109 Mo, 11 221 frames, aucun crash. Le filtre reste **valide pour
les parcours profilés**, et l'audio en ajoute un de plus.

# Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 55 Mo** (parcours profilés) |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | non |

Le jalon 3 reste ouvert. Ce round franchit le mur identifié au round 47 et corrige une
attribution d'erreur : `std::chrono` n'était pas le problème, SDL l'était.
