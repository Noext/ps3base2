# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le park forcé multiplie les instructions SPU par 2 425 | `docs/webassembly/M3-PARK-FIX.md` (round 65) | oui |
| Le code SPU à `0x1178-0x11ac` est une boucle de test | `out/spu/spu_000.cpp:2810-2832` | **oui — mesuré** |
| Deux pc seulement sont atteints : `0x848` (entrée) et `0x11ac` (parcage) | `/tmp/park.log` | oui |

# Les 7,45 millions d'instructions sont du bouclage

Le round 65 a multiplié le travail SPU par 2 425 en forçant le futex park. Ce round vérifie si ce
travail est **utile** ou **circulaire**.

## La mesure

Sur tout le run, les SPU n'atteignent que **deux pc** :

    pc 0x00848     point d'entrée
    pc 0x011ac     point de parcage

Et les cinq noyaux sont **tous** à `0x11ac` dans l'état final.

## Le code à cet endroit

    op_or
    op_nop
    op_ceqi        compare égal à immédiat
    op_ceqi
    op_nor
    op_sfi
    op_sfi
    op_and
    op_ceqbi      compare égal octet à immédiat
    op_xsbh
    op_brhnz      branche si non nul  <-- la boucle

C'est une **boucle de test** : lire un mot, comparer, rebrancher. Aucune écriture vers la mémoire
principale, aucun `putllc`, aucun travail de tuilage.

## La conclusion

Les 7,45 millions d'instructions sont **du bouclage sur place**. Le noyau SPURS tourne dans sa
boucle d'inactivité, exactement comme avant — mais maintenant il boucle **plus vite** parce que le
park futex ne le met plus en sommeil.

Autrement dit : le round 65 a corrigé un défaut réel (le park était désactivé par un `getenv`
inerte), et cela a produit **plus de cycles gaspillés**, pas plus de travail.

## Ce que ça change dans le diagnostic

La question n'est pas « pourquoi les SPU ne tournent pas » — ils tournent, 7,45 millions
d'instructions. La question est : **pourquoi la condition de sortie de la boucle à `0x11ac` n'est
jamais satisfaite ?**

C'est une lecture de la condition elle-même, pas des portes du runtime. Le code à `0x1180-0x11ac`
compare une valeur chargée à des constantes ; il faudrait identifier la source de cette valeur.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **qualifie** le gain du round 65 : les 2 425× d'instructions sont
du bouclage d'inactivité, pas du travail. Le défaut corrigé était réel ; son effet ne l'est pas.


## Correction du même round : ce n'est pas que du bouclage

J'ai qualifié les 7,45 millions d'instructions de « bouclage sur place ». **C'est partiellement
faux**, et la mesure qui le corrige était déjà dans le log.

Les `putllc` — les écritures SPU vers la mémoire principale — comparent les deux runs :

| | sans park | avec park |
|---|---|---|
| `putllc` réussis | 1 648 934 | 1 084 615 |
| `putllc` échoués | 3 434 000 | 1 034 000 |
| **taux d'échec** | **67,6 %** | **48,8 %** |

Le nombre d'échecs **tombe de 3,43 millions à 1,03 million** — un facteur 3,3. Et le taux passe de
67,6 % à 48,8 %.

Donc le park ne fait pas que faire tourner la boucle plus vite : il **réduit massivement les
échecs de réservation**. Moins de SPU se parquent sur `EV_LR` en perdant leur travail.

Les deux observations sont vraies :

1. Les SPU n'atteignent que deux pc, et une partie des instructions est du bouclage d'inactivité.
2. Mais le travail d'écriture réel progresse : 2,4 millions d'échecs en moins.

Le park était donc **un gain réel sur les deux axes** : plus d'instructions ET moins d'échecs.

## Ce que ça laisse comme question

Le taux de 48,8 % reste élevé. Et le rendu n'a pas bougé (11 161 → 11 281 frames). Donc :

- les SPU font plus de travail utile ;
- ce travail ne se traduit pas en contenu affiché.

Le maillon manquant reste **entre** le travail SPU et le rendu — c'est-à-dire le pipeline RSX, dont
le stub compte sans exécuter.
