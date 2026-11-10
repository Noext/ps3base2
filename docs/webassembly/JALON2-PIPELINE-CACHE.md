# Le cache de pipelines est écrit et testé

## Ce qui est ajouté

| fichier | rôle |
|---|---|
| `runtime/include/runtime/pipeline_cache.h` | l'interface |
| `runtime/src/gpu/pipeline_cache.cpp` | sonde linéaire + éviction LRU + statistiques |
| `tests/pipeline_cache.cpp` | 15 vérifications |

## La conception, et pourquoi

Le cache est **sans device** : il associe une `PipelineKey` à un handle opaque et compte les défauts. Le
caller fournit la création.

C'est ce qui rend la mesure possible **avant** le port : le taux de défauts et le thrashing sont des
propriétés du cache, pas du GPU.

| choix | raison |
|---|---|
| capacité fixe + LRU | un unbounded map cacherait le thrashing au lieu de le révéler |
| sonde linéaire | quelques centaines d'entrées, chaînage inutile |
| handle 0 = défaut | un échec de création ne peut pas être confondu avec un hit |
| `thrash_frames` | compte les images où les défauts dépassent la capacité |

## Le test

    pipeline_cache: 15 checks, 0 failures

Couverture : défaut puis hit, compteurs, absence de collision, handle 0 refusé, LRU qui garde l'entrée
chaude, comptage du thrashing.

## Les deux bugs trouvés en écrivant le test

1. **Mon `insert` sondait deux fois** sans revérifier après éviction — deux échecs, corrigé ;
2. **Mon test LRU attendait le mauvais résultat** : après rafraîchissement, une entrée est le 4ᵉ plus
   ancien sur 4 insertions, donc **évincée légitimement**. Le cache avait raison.

Le second est instructif : j'ai d'abord cru à un bug du code, avant de vérifier l'attente.

## Ce qui reste pour le jalon 2

| étape | état |
|---|---|
| table de formats | **fait, testée** |
| clé de pipeline | **fait, testée** |
| **cache** | **fait, testé** |
| renderer porté (55 points d'entrée) | non |
| génération WGSL | non |
| mesure des pipelines sur 60 s | non — dépend du port |

## Ce qu'on peut mesurer dès maintenant

Le cache permet de **simuler** une séquence d'états RSX et de rapporter le nombre de pipelines. C'est ce
que le prompt demande (« compter et rapporter : nombre de pipelines créés sur 60 s »), sauf que 60 s de jeu
exigent le port.

## Position

| critère du prompt | état |
|---|---|
| décisions de format documentées | **oui** |
| cache indexé par état | **oui, écrit et testé** |
| nombre de pipelines sur 60 s | **non mesurable** sans le port |