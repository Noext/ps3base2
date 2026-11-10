# La mesure des changements d'état est bloquée par le build natif

## Ce que j'ai tenté

Lancer le binaire natif pour relever le compteur de changements d'état que j'ai ajouté au round 162.

    DISPLAY=:72 XDG_RUNTIME_DIR=/tmp/xdg163 PS3BASE_DISC_DIR=/root/Dev/mc-port/disc \
    timeout 25 build/ps3base

## Ce que j'ai obtenu

    I   0.063632 [2872167] vfs: disc /root/Dev/mc-port/disc (BLES01976 01.00), patch  ()
    exit=124            (timeout)
    size=85 octets      (une seule ligne de log en 25 s)

**Le jeu se bloque juste après le montage du disque.** Aucun log de rendu, donc aucun compteur de
changements d'état.

## Pourquoi ce n'est pas mon fait

| observation | conséquence |
|---|---|
| `git diff HEAD~1 --stat` liste `runtime/include/runtime/cycles.h`, `runtime/src/hle/cellGame.cpp` | **fichiers que je n'ai pas touchés** |
| `cellGame.cpp` apparaît comme **binaire** (8 135 → 18 056 octets) | le fichier est corrompu ou régénéré par l'autre agent |
| mon changement porte sur `rsx_renderer.cpp` uniquement | il ne peut pas bloquer le boot, qui n'atteint pas le rendu |

Le round 84 avait déjà rencontré cette situation : le build natif est **partagé** avec l'agent qui porte le
jeu sous Linux, et leurs changements en cours cassent le démarrage.

## Ce que j'ai vérifié malgré tout

| vérification | résultat |
|---|---|
| binaire construit | oui, 347 476 800 octets |
| montage du disque | **réussi** (titre BLES01976 01.00 lu) |
| processus vivant | oui, mais bloqué |
| threads | 1 (le runtime n'en a pas encore lancé) |
| logs après le disque | **aucun** |

## Ce que je peux encore mesurer

Le compteur est **en place et compilé**. Il fonctionnera dès que le build natif démarrera à nouveau.

En attendant, la mesure du coût réel reste **non faite**, et je ne vais pas la remplacer par la simulation du
round 162 — dont j'ai déjàmontré qu'elle ne mesurait rien de valable avant correction.

## Ce que je vais faire

Me rabattre sur ce qui ne dépend pas du runtime natif : la **génération WGSL**, étape 5 du jalon 2, qui est du
travail de traduction de shaders et ne demande aucun device.

## Position

| critère du jalon 2 | état |
|---|---|
| table de formats | **fait, testée** |
| clé de pipeline | **fait, testée** |
| cache | **fait, testé** |
| nombre de pipelines sur 60 s réel | **bloqué** — le build natif ne démarre pas |
| temps CPU du thread de rendu | **bloqué** — même raison |
| génération WGSL | **prochaine cible** |