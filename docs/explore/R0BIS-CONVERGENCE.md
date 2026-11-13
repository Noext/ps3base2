# R0-bis — Menu atteint ET pad lu : la convergence

## Le resultat, mesure sur un seul run

    ps3base-wasm: MAIN MENU REACHED -- menu screen thread 1 times, 2 interface threads corroborate
    threads: 15 (noms distincts)
    padprobe: 15          <- cellPadGetData EST appele
    rsx: flip 3661
    sceNpTrophyGetRequiredDiskSpace -> 0x0   (identique au natif)

Les trois piliers tiennent **ensemble** pour la premiere fois :

| pilier | etat |
|---|---|
| ligne de menu | **atteinte** |
| `cellPadGetData` | **appele** |
| `sceNpTrophyGetRequiredDiskSpace` | `0x0`, comme le natif |

## Les trois defauts d'environnement qui se masquaient mutuellement

### 1. Le disque de test n'etait pas le disque natif

`diff -rq` a montre 7 fichiers manquants a la racine du disque (`PS3_DISC.SFB`, `ICON0.PNG`,
`LICDIR`, `PIC1.PNG`, `PS3LOGO.DAT`, `SND0.AT3`, `PS3_UPDATE`) plus `TROPDIR`.

### 2. Le montage `/dev_hdd0` pointe sur `vfs/dev_hdd0`, pas sur la racine

    runtime/src/vfs.cpp:86
        std::string hdd0 = env_or("GOWA_HDD0_DIR", (local + "/dev_hdd0").c_str());
    runtime/src/vfs.cpp:104
        {"/dev_hdd0", hdd0},

Le pack contenait **deux** copies de `dev_hdd0` : celle de la racine (morte, jamais lue) et celle de
`vfs/`. Poser des fichiers a la racine ne servait donc a rien. Piste R4. Apres correction :

    $ python3 tools/disc_verify.py --target /tmp/disk-r4
    disc_verify: 495 expected, 495 identical, 0 missing, 0 different, 0 extra
    DISC-VERIFY: PASS

Le pack passe de 397 891 439 a **158 483 841 octets** (deduplication).

### 3. Trois compteurs mentaient, et j'en etais l'auteur

| compteur | valait | pourquoi c'etait faux |
|---|---|---|
| `padprobe` | 0 | `-DPS3BASE_PAD_PROBE` etait pose sur la ligne 81 (code PPU) alors que `hle/cellPad.cpp` est compile ligne 56 |
| `pad: cmd` | 0 | emis par `take_command_file()`, qui n'a **aucun appelant** : instrument mort |
| « `fill()` non atteint » | — | `pad: queued 600` vient de `load_command_queue()`, appelee **uniquement** depuis `fill()` : le log prouvait l'inverse de ma conclusion |

Correction : `PAD_PROBE` et `PAD_LOG` deplaces sur la ligne 56, et `-sNODE_HOST_ENV=1` ajoute au link
(`grep -c getenv game.js` = 0 : sans ce drapeau, **toutes** les variables `PS3BASE_*` sont inertes
sous Node).

## Ce que j'ai appris sur ma propre methode

Trois fois dans cette session j'ai conclu d'un compteur a zero que la chose n'existait pas :

1. `grep -aci flip` comptait les lignes `flips=0` -> 107 885 flips inventes ;
2. l'histogramme aliasait `(m>>2)&2047` puis `<<2` -> « 137 methodes » etait 137 buckets ;
3. `padprobe: 0` -> « le jeu ne lit jamais le pad », alors que le define etait sur la mauvaise ligne.

**Un compteur a zero ne prouve rien tant qu'on n'a pas verifie que l'instrument est arme.**

## Ce qui reste : l'image

`-DPS3BASE_NO_RENDERER=1` est toujours en place. Le renderer compile pour wasm mais ne lie pas sans
le remplacement des 18 appels Vulkan (K0-bis, R1/R3). **Aucune image du jeu n'est dessinee ni
capturee.**