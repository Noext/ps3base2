# Le build natif ne demarre toujours pas (round 173)

## La verification

| verification | resultat |
|---|---|
| compilation | **0 erreur** |
| binaire | 347 Mo, produit |
| display `:72` | **fonctionne** (`xdpyinfo` repond) |
| lancement 25 s | **exit 124**, une seule ligne de log |

    I   0.048342 vfs: disc /root/Dev/mc-port/disc (BLES01976 01.00), patch ()

Le blocage est **immediatement apres le montage du disque**, comme au round 163.

## Ce que j'ai ecarte

| suspicion | verdict |
|---|---|
| pas de display | **ecarte** — `:72` fonctionne |
| `cellGame.cpp` corrompu | **ecarte** — c'est du texte valide (`#include` visible en `od`) |
| compilation cassee | **ecarte** — 0 erreur |

Git voit `cellGame.cpp` comme binaire parce qu'il contient des octets non-UTF8, pas parce qu'il est
corrompu.

## La cause la plus probable

L'autre agent a **262 lignes de modifications en cours** dans `runtime/src/hle/` :

| fichier | delta |
|---|---|
| `cellGame.cpp` | +9 921 octets |
| `cellSaveData.cpp` | modifie |
| `cellSysutil.cpp` | modifie |
| `support.cpp` | +12 lignes |

Ces changements sont **recompiles dans le binaire** et bloquent le demarrage. Ce n'est pas mon travail, et
je ne peux pas le corriger sans marcher sur le leur.

## Ce que ca empeche

| mesure | depend du binaire natif |
|---|---|
| compteur de changements d'etat RSX (round 162) | oui |
| nombre de pipelines sur 60 s (jalon 2) | oui |
| temps CPU du thread de rendu (jalon 2) | oui |
| fps natif dans le monde (jalon 5) | oui |

**Quatre mesures du prompt restent bloquees par ce build.**

## Ce que je fais a la place

Je ne vais pas attendre ni insister : le round 84, puis le round 163, ont deja etabli que ce build est
partage et intermittentement casse. Je continue sur ce qui ne depend pas de lui.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 3 etapes sur 5 ; mesures bloquees par le binaire natif |
| 3 | 4 criteres sur 5 |
| 4 | ecrit ; validation navigateur impossible ici |
| 5 | goulots nommes ; fps bloques par le binaire natif |