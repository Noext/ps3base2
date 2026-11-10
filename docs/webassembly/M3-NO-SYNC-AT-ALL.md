# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le natif fait 219 207 `sys_event_flag_wait`, le wasm aucun dans le top | `M3-NATIVE-SLEEPS-MORE.md` (round 74) | oui |
| Le wasm ne fait que **2** `sys_event_flag_wait` sur tout le run | `/tmp/fid2.log` | **oui — mesuré** |
| Le jeu importe 15 fonctions `cellSpursEventFlag*` | `out/ppu/table.cpp` | oui |
| Le HLE `cellSpurs` a 3 logs pour 15 fonctions | `runtime/src/hle/cellSpurs.cpp` | oui — presque muet |

# Aucun mécanisme de synchronisation n'est utilisé

## Les deux voies possibles

| voie | implémentation | appels mesurés |
|---|---|---|
| LV2 `sys_event_flag_wait` (85) | `runtime/src/lv2/sync.cpp` | **2** |
| `cellSpursEventFlagWait` (HLE) | `runtime/src/hle/cellSpurs.cpp` | **0** |

J'ai instrumenté les trois fonctions SPURS clés (`EventFlagSet`, `EventFlagWait`,
`EventFlagAttachLv2EventQueue`) avec un probe de compilation :

    SPURS_PROBE("EventFlagWait") ...

Résultat : **aucune sortie**. Les fonctions ne sont jamais appelées.

## Ce que ça signifie

Le jeu **n'utilise aucun des deux mécanismes** de synchronisation par event flag. Ce n'est donc pas
que le HLE soit défaillant : rien ne l'invoque.

Et pourtant le natif en fait 219 207. Donc **le natif arrive à un état du jeu où la synchronisation
par event flag est active**, et le wasm n'y arrive pas.

## Où cela situe le problème

C'est cohérent avec la chaîne établie :

    wasm: 59 allocations (natif 119), s'arrête à t=18,9 s
      → pas d'appel à sys_vm_append_memory (natif 6)
        → threads (4J) FullscreenProgress / Server jamais créés
          → aucun event flag wait (natif 219 207)
            → aucune coordination de rendu par événements
              → frames vides

Le wasm **s'arrête avant** d'entrer dans la phase où le jeu coordonne ses threads par événements.

## Ce que ça ne dit pas

Je n'ai pas établi **quelle** condition manque pour que le jeu entre dans cette phase. Les rounds 70
et 72-73 l'ont circonscrite : la boucle `f_002dc29c` attend quelque chose, et trois de ses quatre
fonctions dorment.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **élimine les deux voies de synchronisation** comme cause : elles
ne sont pas défaillantes, elles ne sont **jamais invoquées**. Le wasm s'arrête avant la phase où le
jeu les utilise.
