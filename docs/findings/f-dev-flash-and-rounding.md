# La procédure dev_flash, et les deux correctifs qui ont débloqué RDR

**Mesuré le 2026-11-13. Écrit pour que le prochain titre n'ait pas à le redécouvrir.**

Résultat de ces deux changements, sur le même binaire, dos à dos :

| | avant | après |
|---|---|---|
| flips | 601 | **1321** |
| ENOENT flashMP3 | 1 | **0** |
| refus DMA | fatal à 23 s | **0** |
| SPURS abort | 23 s | **aucun** |
| port audio 0 | 0/8 rempli | **8/8 rempli** |
| durée | 23 s | **78 s et au-delà** |
| écran atteint | « Press START » | **ALERT, écran de sauvegarde, jeu** |

---

## 1. Le dev_flash : d'où il vient et comment le construire

### Pourquoi c'est obligatoire

RDR ouvre `/dev_flash/sys/external/flashMP3.pic`. Sans ce fichier :

    cellFsOpen -> ENOENT
    le titre affiche "Multistream: mp3 failed to load MP3 codec"
    le champ +0x64 de 0x0177cc00 (adresse du buffer codec) n'est jamais écrit
    le SPU construit une DMA avec EA = 0
    le runtime refuse -- correctement -- et SPURS abandonne à 23 s

Le symptôme apparaissait **trois modules plus loin** que la cause : une DMA
refusée côté SPU, alors que le fichier manquait côté PPU.

### Où est le firmware

    smb://truenas.local/nas/media_data/_library/RetroGaming/retrodeck/firmwares/ps3/PS3UPDAT.PUP

Monté sur cette machine :

    /mnt/truenas-nas2/media_data/_library/RetroGaming/retrodeck/firmwares/ps3/PS3UPDAT.PUP

**206 177 436 octets**, firmware Sony du 2025-02-19.

Vérifier AVANT de perdre du temps :

    /root/Dev/ps3-self-tools/pup/pup i "$PUP"
    # attendu : "File count: 9", license.xml ~309 KB, update_files.tar ~194 MB

Un `file_count` de 65514 avec 256 Mo est un **disque de jeu**, pas un firmware :
les disques contiennent des mises à jour minimales sans `dev_flash` complet.

### La procédure, trois étapes

    PUP=/mnt/truenas-nas2/media_data/_library/RetroGaming/retrodeck/firmwares/ps3/PS3UPDAT.PUP

    # 1. conteneur PUP -> update_files.tar (194 Mo) + ps3swu.self
    /root/Dev/ps3-self-tools/pup/pup x "$PUP" fw-src
    # le repertoire de destination ne doit pas exister

    # 2. le tar -> 25 tranches dev_flash*.tar.aa.* + les .pkg
    mkdir -p fw-src/upd && cd fw-src/upd
    tar xf ../update_files.tar

    # 3. chaque tranche est un SDAT SCE chiffre -> un tar "content"
    UNPKG=/root/Dev/_untrusted/allps3tools/kakaroto-ps3tools/unpkg
    mkdir -p /root/Dev/devflash-out
    for f in dev_flash*tar.aa.*; do
        $UNPKG "$f" "/tmp/df-$f" > /dev/null 2>&1
        [ -f "/tmp/df-$f/content" ] && tar xf "/tmp/df-$f/content" -C /root/Dev/devflash-out
        rm -rf "/tmp/df-$f"
    done

Les 25 tranches donnent `dev_flash/` (175 Mo) et `dev_flash3/`.

### L'utiliser

    GOWA_FLASH_DIR=/root/Dev/devflash-out/dev_flash ./build-rdr/ps3base --no-launcher

### Les outils, chemins exacts

| outil | chemin |
|---|---|
| `pup` | `/root/Dev/ps3-self-tools/pup/pup` |
| `unpkg` | `/root/Dev/_untrusted/allps3tools/kakaroto-ps3tools/unpkg` |

**Piège** : `unpkg` est dans `kakaroto-ps3tools`, **pas** `kakaroto-ps3utils`.
Le second contient les sources et le script `decrypt_dev_flash.sh` qui fait la
boucle complète, mais le **binaire** est dans le premier. Chercher le binaire
dans `ps3utils` fait perdre du temps.

---

## 2. Le correctif d'arrondi SPU

`runtime/src/spu/ops_impl.h`, `RoundGuard` :

    RoundGuard() : old(_mm_getcsr()) { _mm_setcsr((old & ~0x6000u) & ~0x8040u); }
                                                ^^^^^^^^^^^^^^ EFFAÇAIT les bits

Les bits 13-14 de MXCSR **sont** le champ de contrôle d'arrondi :

    00 = au plus proche    01 = vers le bas
    10 = vers le haut      11 = vers zéro

Les effacer sélectionne **l'arrondi au plus proche**, l'exact opposé du
commentaire au-dessus (« forces truncation »). Or la console **tronque** :
RPCS3 fait `std::fesetround(FE_TOWARDZERO)` pour tout le thread
(`SPUThread.cpp:1491`).

**Correctif** :

    RoundGuard() : old(_mm_getcsr()) { _mm_setcsr((old | 0x6000u) & ~0x8040u); }
                                                ^^^^^^  0x6000 = 11b = vers zéro

Les bits 15 et 7 (FTZ, DAZ) restent effacés : le SPU ne flush pas les dénormaux.

**Preuve**, par le fuzzer SPU (`spu_op_fuzz`, 24,9 M de vérifications) :

    DFA de 3ff0000000000000 + 3ca4000000000000   (exact = 1 + 0,625 ulp)
      console / RPCS3, RC=truncate  ->  3ff0000000000000
      arrondi au plus proche        ->  3ff0000000000001
      notre runtime AVANT correctif ->  3ff0000000000001   FAUX
      notre runtime APRÈS           ->  3ff0000000000000   juste

Les huit opérations double plus FRDS et FESD héritaient de cette erreur.

---

## 3. Ce qui reste bloquant

Avec le dev_flash réel, RDR atteint l'écran **ALERT** (« A Red Dead Redemption
single player save could not be found ») et laisse choisir entre *Continue
Without Saving*, *Create A New Save Game* et *Retry*.

Au-delà, un **SIGSEGV** apparaît vers 78 s — un blocage différent, plus loin dans
le jeu, sans rapport avec le SPU ni l'audio. C'est la prochaine frontière.
