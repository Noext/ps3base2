# Pourquoi le build natif ne demarre pas

## La methode

J'ai trace les appels systeme du binaire (`strace -f`) au lieu de constater le blocage.

## Ce que la trace montre

Le jeu **ne bloque pas au montage du disque**. Il progresse :

| etape | preuve dans la trace |
|---|---|
| montage du disque | `vfs: disc ... BLES01976 01.00` |
| lecture de l'icone | `openat(.../ICON0.PNG)` puis lecture complete |
| lecture d'un cache | `openat(/root/.cache/nvidia/GLCache/...)` |
| **puis** | **31 `openat(/dev/nvidiactl)` consecutifs** |

Les trois derniers evenements sont le `SIGTERM` de mon `timeout`, puis la sortie.

## Ce que ca etablit

Le dernier appel **reel** du jeu est une ouverture repetee de `/dev/nvidiactl`. Ce n'est pas notre code :
c'est **le pilote NVIDIA qui ne repond pas**.

| fait | consequence |
|---|---|
| `/dev/nvidia*` existe et est accessible (`crw-rw-rw-`) | le device est la |
| 31 ouvertures consecutives de `nvidiactl` | le pilote est interroge en boucle |
| aucun appel systeme entre elles | le thread **attend** dans le pilote |

## Ce que ca exclut

| suspicion | verdict |
|---|---|
| les modifications de l'autre agent | **non demontre** — le jeu lit ses fichiers normalement |
| le disque | **ecarte** — `EBOOT.BIN` et `ICON0.PNG` lus sans erreur |
| le display | **ecarte** — `:72` repond a `xdpyinfo` |
| mon instrumentation | **ecarte** — elle n'agit qu'apres le demarrage du rendu |

## Ce que je ne peux pas faire

Je ne peux pas reparer le pilote NVIDIA. Je ne peux pas non plus prouver que le jeu demarrerait si le
pilote repondait — seulement que **c'est la qu'il attend**.

## Ce que ca change pour le rapport

Le round 173 attribuait le blocage aux modifications de l'autre agent. **Ce n'etait pas demontre.** La trace
montre un blocage dans le pilote graphique, ce qui est une cause differente et plus probable.

Je retire l'attribution precedente.

## Les mesures toujours bloquees

| mesure | jalon |
|---|---|
| compteur de changements d'etat | 2 |
| pipelines sur 60 s | 2 |
| temps CPU du rendu | 2 |
| fps natif dans le monde | 5 |

Les quatre exigent un binaire qui demarre, donc un pilote qui repond.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7, mesures bloquees par le pilote |
| 3 | 5 criteres sur 6 |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps bloques par le pilote |