# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `park_enabled()` lit `GOWA_SPU_PARK` via `getenv`, défaut **0** | `runtime/src/reservation.cpp:619-622` | **oui — inerte en wasm, donc toujours false** |
| Le park futex exige ce flag | `runtime/src/spu/thread.cpp:407` | oui — la branche n'est jamais prise |
| La branche de repli est `rsv::wait_lost(line, rt, snap, 1000)` | `runtime/src/spu/thread.cpp:427` | **oui — c'est le chemin réel** |
| `wait_lost_impl` boucle avec `_mm_pause()` puis `host::yield()` | `runtime/src/reservation.cpp:559-578` | oui |

# Le chemin d'attente réel, trouvé

Les rounds 60-63 cherchaient une porte fermée dans `block()`. **Le SPU n'attend pas dans
`block()`** : mes compteurs d'entrée n'ont jamais atteint 1 000, et les réveils non plus.

L'attente se fait dans `rsv::wait_lost`, sur le chemin des réservations :

    if (park_slot >= 0 && rsv::park_enabled()) {
        rsv::wait_parked(...);          /* futex, réveil par kick_line() */
    } else if (aux) {
        natsched::aux_wait(...);
    } else {
        rsv::wait_lost(line, rt, snap, 1000);   /*  <-- chemin wasm  */
    }

## Pourquoi le wasm ne prend jamais le park

    bool park_enabled() {
        static const bool on = env_long("GOWA_SPU_PARK", 0) != 0;
        return on;
    }

Défaut **0**, et `env_long` passe par `getenv`, **inerte dans le build wasm**. Le park est donc
**désactivé de façon permanente**, quelle que soit la configuration.

Le chemin de repli est `wait_lost` :

    for (int i = 0; i < 200; i++) { if (lost(...)) return true; _mm_pause(); }
    for (;;) {
        if (lost(...)) return true;
        if (left <= 0) return false;
        if (sleep_after >= 0 && ...) host::wait_on(...);
        else if (yield_after >= 0 && ...) host::yield();
        else _mm_pause();
    }

`sleep_after` vaut **-1** par défaut (`getenv` inerte) : la branche `wait_on` n'est jamais prise.
`yield_after` vaut 30, donc après 30 µs le fil tourne en **`host::yield()`**.

## Ce que ça signifie

Le SPU ne dort pas sur un futex réveillable. Il **tourne** : `_mm_pause()` puis `host::yield()`
en boucle, jusqu'à ce que la réservation soit perdue ou que 1 000 µs s'écoulent.

En wasm, `_mm_pause()` ne fait rien (pas d'instruction de pause) et `host::yield()` rend la main
au scheduler. Le thread hôte **consomme du CPU sans exécuter une seule instruction invitée**,
d'où `insns` figé.

Et le natif ? Il a la même valeur par défaut (`GOWA_SPU_PARK` absent → 0)… sauf que le natif peut
l'activer par variable d'environnement, et **c'est peut-être ce que fait le script de lancement
natif**.

## L'expérience à faire

Activer le park **par constante de compilation** dans le build wasm, comme pour les autres flags
muets :

    #define GOWA_SPU_PARK_FORCED 1

et voir si `insns` décolle. C'est la première fois que je peux tester une hypothèse par un
changement **d'un seul bit de configuration**, au lieu d'un raisonnement.

# Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **trouve le chemin d'attente réel** (pas `block()`, mais
`wait_lost` sur les réservations) et **identifie pourquoi le park futex n'est jamais utilisé** :
un `getenv` inerte et un défaut à 0.
