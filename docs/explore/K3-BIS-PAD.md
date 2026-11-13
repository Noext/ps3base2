# K3-bis — TROPDIR corrige une partie, mais le pad n'est toujours pas lu

## Ce que K3 a trouve, et que je confirme

La piste K3 a identifie la cause du blocage des entrees : **`TROPDIR` manquait de l'arbre de disque
wasm**. `/tmp/disk/PS3_GAME/` ne contenait que `PARAM.SFO` et `USRDIR`, alors que le disque natif
(`/root/Dev/mc-port/disc/PS3_GAME/`) porte `TROPDIR/NPWR05636_00/TROPHY.TRP`.

Chaine causee : `sceNpTrophyGetRequiredDiskSpace` -> `CONF_DOES_NOT_EXIST` -> `trophy_init_thread`
sort en `0xffffffff8002291b` -> la chaine d'init UI du jeu s'arrete -> le thread qui interroge le pad
n'est jamais demande.

## Ma verification, apres `cp -r .../TROPDIR /tmp/disk/PS3_GAME/`

| mesure | avant | apres |
|---|---|---|
| `sceNpTrophyGetRequiredDiskSpace` | `CONF_DOES_NOT_EXIST` | **`-> 0x496e0`** |
| threads crees | 8 | **12** |
| `(4J) Basic Event Handler` | 0 ligne | **5 lignes** |
| flips | ~1 700 | **5 401** |

**K3 a raison sur son diagnostic.** Le thread existe maintenant, et le nombre de threads correspond
au natif (12 contre 13 sur 140 s ; le 13e natif est un second `OpenStreamThreadProc` a t=86 s, que le
wasm plus lent n'atteint pas).

## Ce qui ne suit pas, mesure sur MON build

    pad: queued 600 built-in command(s)    <- le script est charge
    pad: cmd ...                           <- 0 ligne : aucune commande jouee
    padprobe: cellPadGetData call #N       <- 0 ligne : le pad n'est pas lu

`cellPadInit` est appele, le script est charge, mais **ni `take_command_file()` ni `fill()` ne sont
jamais atteints**. Le jeu ne consomme pas le pad, malgre les 12 threads presents.

Difference avec la mesure de K3 : sa sonde `PS3BASE_PAD_PROBE` etait compilee dans son build. J'ai du
l'ajouter a `tools/build_wasm.sh:81` (la boucle qui compile `hle/cellPad.cpp`) — la ligne 56 ne la
portait pas. Mon resultat `0` est donc mesure sur un build qui A la sonde.

## Ce que cela signifie
Le blocage s'est **deplace** d'un cran. Il n'est plus « le thread n'existe pas », il est « le thread
existe mais n'atteint pas le polling du pad ». La prochaine chose a mesurer est ce que fait ce thread
entre sa creation (t=18 s en natif) et l'appel au pad.

## Correction que j'apporte au build

`TROPDIR` doit faire partie de l'arbre de disque precharge. Sans lui, le wasm ne reproduit pas
l'experience native. C'est une correction de l'ENVIRONNEMENT DE TEST, pas du runtime.

## Ce que j'ai casse et repare en chemin

J'ai retire `-DPS3BASE_NO_RENDERER` et l'exclusion de `rsx_renderer.cpp` : le renderer compile, mais
le link reclame **18 symboles Vulkan**. Le build ne produisait plus de module. J'ai **restaure** la
configuration qui linke (renderer exclu, `NO_RENDERER` remis) et nettoye les objets perimes. Le module
reproduit bien les 4 criteres.

Ces deux changements restent valides et sont documentes pour la suite (`K0`, `K0-bis`) : le renderer
**compile** pour wasm, il ne **lie** pas sans un remplacement des 18 appels Vulkan.