# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `park_enabled()` dépend de `getenv`, inerte en wasm | `runtime/src/reservation.cpp:619-622` | **oui — corrigé par constante de compilation** |
| Le chemin de repli `wait_lost` tourne en `_mm_pause()`/`host::yield()` | `runtime/src/reservation.cpp:559-578` | oui — c'est ce qui consommait le quantum |
| `kick_line()` réveille le park par `host::wake_one` | `runtime/src/reservation.cpp:602-609` | oui — réveil réel |

# L'expérience, et son résultat

Le round 64 avait identifié le mécanisme : en wasm le futex park est **désactivé** (getenv inerte,
défaut 0), donc le SPU tourne en `_mm_pause()`/`host::yield()` sans exécuter d'instruction.

J'ai forcé le park par constante de compilation :

    #if PS3BASE_SPU_PARK_FORCED
        return true;
    #else
        static const bool on = env_long("GOWA_SPU_PARK", 0) != 0;
        return on;
    #endif

## Le résultat

| | avant | après | natif |
|---|---|---|---|
| **`insns` SPU (max)** | **3 072** | **7 450 624** | 287 669 291 008 |
| threads PPU | 17 | **18** | 21 |
| frames | 11 161 | 11 281 | 74 581 |

**Facteur 2 425 sur les instructions SPU.** Les SPU passent de ~3 000 à 7,45 millions.

## Ce que ça prouve

Le park était **un** verrou, et un vrai : forcer le futex multiplie le travail SPU par 2 400 et
fait apparaître un thread PPU de plus.

## Ce que ça ne résout pas

Le natif fait 287 milliards. Nous sommes à 7,45 millions — **facteur 38 600 restant**. Le park
était nécessaire mais pas suffisant.

Et surtout : **toujours pas de ligne de menu**, et les frames n'ont quasiment pas bougé (11 161 →
11 281). Le travail SPU a augmenté sans que le rendu change.

## Pourquoi ça ne suffit pas, hypothèse

Le park futex réveille le SPU **quand la ligne réservée change** (`kick_line`). Mais si le travail
n'est jamais soumis — `cellSpursAddWorkload` n'est pas implémenté (round 63) — le SPU se réveille,
ne trouve rien, et se re-parque.

7,45 millions d'instructions correspondent peut-être à ce cycle réveil/re-park plutôt qu'à du
travail utile.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **corrige un vrai verrou** (park désactivé par getenv inerte) et
mesure son effet : **2 425× plus d'instructions SPU**, un thread PPU de plus. Mais le rendu ne
change pas, ce qui indique que le travail SPU produit n'est pas celui qu'attend le pipeline
graphique.
