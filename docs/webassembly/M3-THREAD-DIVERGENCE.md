# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `texloc` est émis par le renderer | `runtime/src/gpu/rsx_renderer.cpp:1652` | **oui** — 40 occurrences natives, 0 wasm |
| Le prompt exige un stub qui compte les commandes RSX (point 6) | `docs/PORT-WEB-PROMPT.md` | oui — le stub est **conforme** |
| Milestone 3's criterion is a log line, not a screenshot | `docs/PORT-WEB-PROMPT.md` | oui |

# Correction de ma lecture du round 56

Le round 56 concluait que le jalon 3 butait sur le backend graphique (jalon 2). **C'était trop
rapide** : le point 6 du prompt **exige** que « le chemin d'affichage soit remplacé par un stub
qui compte les commandes RSX ». Mon stub est donc **conforme à la spécification**, pas un défaut.

Le manque de GPU n'explique pas l'absence de la ligne de menu, parce que le prompt demande
précisément de ne pas l'avoir.

# La divergence, localisée par les threads

Tous les threads sont journalisés. Comparaison :

| | natif valide | wasm |
|---|---|---|
| créations `_sys_ppu_thread_create` | **129** | **17** |
| noms distincts | **21** | **13** |

Les 8 threads que le natif crée et **que le wasm n'atteint jamais** :

    (4J) GetFriendsThreadProc
    thread_readSaveCacheFile
    thread_getSavesInfo
    (4J) FullscreenProgress
    (4J) Server
    (4J) Post processing
    thread_autosave
    thread_options_save

## L'ordre exact

    ... 13-17  MountInstalledDLC_thread  ×5      ← le wasm s'arrête ici (t=20,8 s)
        18     (4J) GetFriendsThreadProc          ← le natif continue (t=82,6 s)
        19     thread_readSaveCacheFile
        20     thread_getSavesInfo
        21     (4J) FullscreenProgress
        22     (4J) Server

## Ce qui déclenche le pas suivant, côté natif

Juste avant le thread 18 :

    memory: sys_memory_allocate(0x100000 flags 0x400) -> 0x35300000
    memory: sys_memory_allocate(0x100000 flags 0x400) -> 0x35400000
    texloc: off 0x3e1bd00 fmt 0x0001a529 raw 1 decoded 0 ...
    ppu: _sys_ppu_thread_create '(4J) GetFriendsThreadProc'

Le natif **lie trois textures** puis crée le thread suivant. Le wasm alloue de la mémoire
(60 `sys_memory_allocate`) mais **ne lie aucune texture** — `texloc` vient du renderer, absent.

## Ce que ça signifie, avec la prudence requise

Deux lectures, et je ne les départage pas :

1. **La liaison de texture est une condition du progrès.** Le jeu attend qu'une texture soit
   prête avant de continuer, et sans renderer elle ne l'est jamais.
2. **La liaison est un symptôme concomitant.** Le jeu aurait avancé de toute façon, et c'est
   autre chose (un verrou, une attente) qui bloque entre le thread 17 et 18.

La première est plausible parce que le prompt demande explicitement un stub qui **compte** les
commandes — et compter n'est pas lier. Si le titre interroge l'état d'une texture liée, un stub
qui ne fait que compter ne peut pas répondre.

C'est la prochaine mesure : instrumenter `cellGcmSetTexture` et la lecture d'état de texture, et
voir si le wasm reçoit une requête à laquelle le stub ne répond pas.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo, vérifié à l'exécution** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **corrige** la conclusion du round 56 (le stub RSX est
conforme au prompt, le GPU n'est pas la cause) et **localise la divergence** : le wasm s'arrête
après le 17ᵉ thread, le natif en crée 21, et la différence se situe autour de la liaison de
textures.
