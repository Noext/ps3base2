# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `cellHttpInit` / `cellSslInit` / `cellRudpInit` implémentés côté RPCS3 | `refs/rpcs3/rpcs3/Emu/Cell/Modules/cellHttp.cpp` | **non** — voir ci-dessous |
| Idem chez Twisted Metal et Saints Row 2 (tables de NIDs générées) | `refs/twistedmetal/src/gen/ppu_hle_nids.cpp`, `refs/saintsrow2/…` | non |
| `flagtrace` est émis par notre propre `lv2/sync.cpp` | `runtime/src/lv2/sync.cpp:110,494,497` | **oui** — c'est le point de comparaison |

# Deux mesures, dont une qui élimine une piste

## Le réseau n'est pas le blocage

Il reste 8 imports non implémentés, tous réseau :

    unimplemented cellHttp::cellHttpInit
    unimplemented cellHttp::cellHttpCreateClient
    unimplemented cellSsl::cellSslInit
    unimplemented cellRudp::cellRudpInit   ... (8 au total)

**Le run natif valide a exactement les mêmes 8** (`/tmp/cmp.log`) et atteint le monde. Le
réseau n'est donc pas ce qui manque. Je n'ai pas écrit de HLE réseau et je n'en ai pas besoin
pour ce jalon.

## La différence de synchronisation

| | natif valide (`cmp.log`) | wasm (`f4.log`) |
|---|---|---|
| lignes `flagtrace` | **3 321 122** | **4** |
| `sys_timer_usleep` | — | **266 568** (dominant) |

Le natif passe son temps dans les **event flags** ; le wasm en enregistre **4** et dort dans
`usleep` 266 000 fois.

C'est le contraste le plus net mesuré jusqu'ici, et il pointe vers un **modèle de
synchronisation différent** : le natif avance par événements, le wasm attend par minuterie.

## L'état à t=150

Seize threads vivants, dont `MSSync`, `MSSTimer`, `(4J) Chunk update`,
`thread_options_load`, `(4J) OpenStreamThreadProc` et cinq `MountInstalledDLC_thread`.
**`main_thread` est `running`.** Le processus n'est pas figé — il tourne, avec un profil de
synchronisation différent.

# Ce que je n'ai pas établi

Je n'ai pas prouvé que l'écart de `flagtrace` **cause** l'absence de menu. C'est une
corrélation forte sur la seule mesure disponible, et elle mérite un test : instrumenter un
event flag précis des deux côtés et comparer la séquence.

# Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo, vérifié à l'exécution** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | non |

Le jalon 3 reste ouvert. Ce round **élimine une piste** (le réseau) et **isole un contraste
mesuré** qui devient la prochaine hypothèse testable.
