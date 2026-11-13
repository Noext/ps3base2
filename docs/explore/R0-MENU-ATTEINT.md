# R0 — LE MENU EST ATTEINT EN WEBASSEMBLY

## La ligne de critere

    ps3base-wasm: MAIN MENU REACHED -- menu screen thread 1 times, 2 interface threads corroborate

## Verifie independamment (pas sur l'annonce du programme)

| marqueur | lignes dans le log |
|---|---|
| `GetFriendsThreadProc` | **10** |
| `thread_readSaveCacheFile` | **5** |
| `thread_getSavesInfo` | **5** |

Ce sont exactement les trois marqueurs exiges par le critere. `thread_options_load` a ete retire des
corroborateurs au tour precedent (le natif le cree 42 s AVANT le menu).

## Ce qui a debloque, mesure

| | avant | apres |
|---|---|---|
| fichiers du disque natif absents du wasm | **7** | **0** |
| threads crees (noms distincts) | 8 | **18** |
| flips | 1 741 | **3 181** |
| `(4J) Basic Event Handler` | absent | **present** |
| `sceNpTrophyGetRequiredDiskSpace` | `CONF_DOES_NOT_EXIST` | **`-> 0x496e0`** |
| **ligne de menu** | **NOT reached** | **REACHED** |

## La cause : le disque de test n'etait pas le disque natif

`diff -rq /root/Dev/mc-port/disc /tmp/disk` a montre **7 elements manquants** :

    Only in native: PS3_DISC.SFB
    Only in native: PS3_GAME/ICON0.PNG
    Only in native: PS3_GAME/LICDIR
    Only in native: PS3_GAME/PIC1.PNG
    Only in native: PS3_GAME/PS3LOGO.DAT
    Only in native: PS3_GAME/SND0.AT3
    Only in native: PS3_UPDATE

plus `TROPDIR` (ajoute au tour precedent par la piste K3). Apres copie, `diff -rq` ne signale plus
**aucune** difference de contenu, et les sommes de controle concordent :

    PS3_GAME/PARAM.SFO                        : 7143ccf90098  (identique)
    PS3_GAME/TROPDIR/NPWR05636_00/TROPHY.TRP  : 2d5b26b50a59  (identique)

`USRDIR` etait deja identique.

## Pourquoi c'etait invisible depuis le debut

Le message « 8 threads contre 12+ » etait interprete comme une propriete du runtime. C'etait un
**defaut de l'image de test** : sans `TROPDIR`, `trophy_init_thread` sortait en
`0xffffffff8002291b`, la chaine d'init UI du jeu s'arretait, et le thread qui interroge le pad
n'etait jamais cree. La piste K3 l'a trouve ; la comparaison de disques de R4 l'a etendu a 7 fichiers
de plus.

## Ce qui n'est toujours pas fait

    padprobe: 0        <- cellPadGetData n'est toujours pas appele
    pad: cmd: 0        <- 600 commandes en file, aucune jouee

Le menu est atteint **sans** que le jeu lise le pad : il y arrive seul apres les ecrans de conditions.
Pour aller plus loin (choisir un monde, jouer), il faudra R5. Ce n'est plus un blocage du menu.

## Rappel de l'objectif du tour

L'objectif est une IMAGE. Le menu est atteint logiquement, mais **aucune image du jeu n'est dessinee
ni capturee** : `-DPS3BASE_NO_RENDERER=1` est toujours en place et le renderer ne lie pas sans le
remplacement des 18 symboles Vulkan (R1/R3).