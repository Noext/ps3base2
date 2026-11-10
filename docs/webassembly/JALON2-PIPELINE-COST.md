# Le coût du cache, mesuré sur 60 s de jeu simulé

## Ce que le prompt demande

> RSX change d'état par draw : c'est le coût principal de ce jalon, **à mesurer**.
> Compter et rapporter : nombre de pipelines créés sur 60 s de jeu, temps CPU du thread de rendu.

## Ce que j'ai mesuré

J'ai écrit une simulation : **3 600 images** de **400 draws** chacune (60 s à 60 Hz), avec un changement d'état
tous les 64 draws.

| | valeur |
|---|---|
| images | 3 600 |
| draws par image | 400 |
| draws au total | **1 440 000** |
| lookups | 1 440 000 |
| hits | 1 439 820 |
| **MISSES = pipelines créés** | **180** |
| **taux de hit** | **99,99 %** |
| évictions | **0** |
| images en thrashing | **0** |
| entrées vivantes | 180 sur 512 |

## Ce que ça donne

**0,05 pipeline par image en moyenne.** Le cache tient **une image RSX complète** sans éviction.

| conséquence | valeur |
|---|---|
| créations par seconde à 60 Hz | **3** |
| créations sur 60 s | **180** |
| capacité nécessaire | **~180 entrées** (512 largement suffisant) |

## Pourquoi c'est plausible

La séquence n'est pas arbitraire : le flux RSX mesuré dans les logs de boot est dominé par **une seule
primitive** (2,9 M de draws, part de packets 0,1 %), donc une image est **des centaines de draws
quasi-identiques** avec quelques changements d'état aux frontières de passes (monde, puis interface, puis
clear).

Un changement tous les 64 draws est **pessimiste** : les logs montrent moins.

## Ce que ça ne mesure pas

Le **temps CPU du thread de rendu** — il exige le port et un device. Le prompt demande les deux mesures ;
je n'en fournis qu'une.

## Ce que ça établit

**Le coût du cache n'est pas un obstacle.** 3 créations de pipeline par seconde est négligeable, et la
capacité nécessaire est de l'ordre de 200 entrées.

C'est une **bonne nouvelle** pour le jalon 2 : le point que le prompt identifie comme « le coût principal
» se révèle modeste **si l'état RSX est réellement cohérent par passe** — ce que les logs suggèrent.

## Position

| critère du prompt | état |
|---|---|
| nombre de pipelines sur 60 s | **180, mesuré par simulation** |
| temps CPU du thread de rendu | **non mesuré** — exige le port |
| cache indexé par état | **fait, testé** |
| renderer porté | **non** |

---

# Correction : le premier chiffre était un artefact de mon harnais

## Ce que j'ai découvert en testant la sensibilité

J'ai fait varier la fréquence de changement d'état et obtenu **180 misses dans tous les cas**, même à
changement à chaque draw. Un chiffre constant quelle que soit la fréquence est **impossible** : c'est mon
harnais qui mentait.

**La cause** : mon générateur d'états n'explorait que **180 combinaisons** — `dc` était limité à 4 valeurs sur
8, `bs` à 5 sur 12. Le cache ne pouvait pas déborder parce que l'espace d'états était minuscule.

## La mesure corrigée

| changement tous les | misses | pipelines/image | taux de hit |
|---|---|---|---|
| **64 draws** | **19 725** | **5,48** | 98,63 % |
| 8 draws | 140 024 | 38,90 | 90,28 % |
| 4 draws | 280 080 | 77,80 | 80,55 % |
| 2 draws | 560 164 | 155,60 | 61,10 % |
| **1 draw** | **1 120 065** | **311,13** | 22,22 % |

## Ce que ça change complètement

**Le cache déborde avec 512 entrées.** L'espace d'états réel est 8 × 12 × 3 × 8 = **2 304 combinaisons**, et
512 n'en tient qu'un quart.

| fréquence | verdict |
|---|---|
| changement tous les 64 draws | **5,48 pipelines/image** — coûteux mais possible |
| changement à chaque draw | **311 pipelines/image** — intenable |

## Ce que ça signifie pour le jalon 2

Le coût dépend **entièrement de la cohérence de l'état RSX par passe**. Le prompt dit « RSX change d'état par
draw » ; si c'est littéralement vrai, le port WebGPU **ne peut pas tenir la cadence**.

Les logs de boot suggèrent le contraire (une primitive, 2,9 M de draws), mais **je n'ai pas mesuré le nombre
réel de changements d'état par image**.

## La mesure qui manque

Compter les `vkCmdSet*` **par image** dans un run natif. C'est faisable : le renderer a 22 setters, et un
compteur par nom donnerait la distribution réelle.

## Correction de méthode

**Dixième erreur d'instrument de cette campagne.** Mon harnais ne pouvait pas produire un résultat différent
de 180, et je l'ai publié comme une mesure.