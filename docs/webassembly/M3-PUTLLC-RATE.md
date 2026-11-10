# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le probe `spuput` est un `#if PS3BASE_SPU_WAIT_PROBE` | `runtime/src/spu/thread.cpp:1183` | **oui** — activé côté wasm, **pas côté natif** |
| `putllc` échoué parque le SPU sur EV_LR jusqu'à réservation correspondante | commentaire `thread.cpp:1185-1186` | oui |
| Le natif ne loggue aucune ligne `spuput` | `/tmp/cmp.log` : 0 occurrence | **oui — pas de comparaison possible** |

# La mesure

Après le 17ᵉ thread, le wasm ne crée plus de threads. Ce qu'il fait, en revanche, est mesurable :
les SPU écrivent en mémoire par `putllc`, et le probe compte les succès et les échecs.

    2 743 échantillons :
      début   ok=1        fail=0         0,0 %
      milieu  ok=772 915  fail=1 660 000 68,2 %
      fin     ok=1 648 934 fail=3 434 000 67,6 %

**Le taux d'échec est stable à ~68 %** sur les deux derniers tiers du run. Ce n'est pas une
dégradation progressive : c'est un régime permanent qui s'installe au milieu du boot.

## Ce que `putllc` fait, et pourquoi l'échec compte

`putllc` écrit une ligne SPU dans la mémoire principale **si la réservation tient**. Un échec
parque le SPU sur l'événement `EV_LR` jusqu'à ce qu'une réservation correspondante réapparaisse.
Si 68 % des écritures échouent, deux tiers du travail SPU est **retenté, pas perdu** — mais le
débit utile s'effondre.

## Ce que je ne peux pas affirmer

**Je n'ai pas la mesure côté natif.** Le probe est un `#if` de compilation qui n'est pas activé
dans le binaire natif, donc `cmp.log` ne contient aucune ligne `spuput`, et je ne peux pas dire
si 68 % est normal ou non.

C'est exactement l'erreur que j'ai commise aux rounds 51-54 et corrigée au round 55 : conclure
sur un symptôme sans le mesurer dans le run qui réussit. Je ne la répète pas ici — je livre la
mesure wasm et je **nomme explicitement** la mesure manquante.

## La mesure à faire ensuite

Reconstruire le binaire natif avec `-DPS3BASE_SPU_WAIT_PROBE=1` et comparer le taux. C'est le
seul moyen de savoir si 68 % est :

1. **anormal** — le wasm rate des réservations que le natif obtient, et c'est le blocage ;
2. **normal** — les SPU contentieux se disputent la mémoire des deux côtés, et le blocage est
   ailleurs.

Le build natif est actuellement occupé par le chantier parallèle (`cellGame.cpp`), donc je
n'ai pas lancé cette reconstruction ce round pour ne pas interférer.

## Ce que ça ajoute au diagnostic

Une piste quantifiée, avec sa taille : **deux millions d'échecs de réservation** sur le run. Si
le natif en a proportionnellement dix fois moins, c'est le mur.

Et une raison structurelle plausible : le wasm exécute les threads SPU sur des threads hôtes
Emscripten, avec une politique d'ordonnancement différente du natif. Une réservation qui dépend
du timing échoue davantage quand les threads hôtes ne sont pas ordonnancés comme les threads
natifs.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo, vérifié à l'exécution** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round produit une mesure quantifiée (68 % d'échecs de réservation
SPU, stable, 3,4 millions d'occurrences) et **nomme la mesure manquante** qui permettrait de
savoir si c'est anormal.
