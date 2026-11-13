# dev_flash : la procédure complète, mesurée

**Lu une fois, écrit ici pour ne plus jamais le redécouvrir.**

## Pourquoi c'est obligatoire

RDR demande `/dev_flash/sys/external/flashMP3.pic`. Sans ce fichier :

- `cellFsOpen` retourne `ENOENT`
- le titre affiche `Multistream: mp3 failed to load MP3 codec (are you using the correct flash?)`
- le champ `+0x64` de `0x0177cc00` (adresse du buffer codec) n'est jamais écrit
- le SPU construit une DMA avec EA = 0
- le runtime refuse — correctement — et SPURS abandonne à 23 s

Avec le vrai fichier, mesuré le 2026-11-13 :

| | sans dev_flash | avec dev_flash |
|---|---|---|
| flips | 601 | **1321** |
| ENOENT flashMP3 | 1 | **0** |
| refus DMA | fatal | **0** |
| SPURS abort | 23 s | **aucun** |
| port audio 0 | 0/8 rempli | **8/8 rempli** |
| durée | 23 s | **78 s puis SIGSEGV** |

## Où est le firmware

    smb://truenas.local/nas/media_data/_library/RetroGaming/retrodeck/firmwares/ps3/PS3UPDAT.PUP

Monté sur `/mnt/truenas-nas2/media_data/_library/RetroGaming/retrodeck/firmwares/ps3/PS3UPDAT.PUP`,
**206 177 436 octets**, firmware du 2025-02-19.

Vérifier avant de perdre du temps :

    /root/Dev/ps3-self-tools/pup/pup i "$PUP"
    # attendu : File count: 9, license.xml ~309 KB, update_files.tar ~194 MB

Un `file_count` de 65514 est un disque de jeu, pas un firmware.

## La procédure, trois outils

    PUP=/mnt/truenas-nas2/media_data/_library/RetroGaming/retrodeck/firmwares/ps3/PS3UPDAT.PUP

    # 1. le conteneur PUP -> update_files.tar
    /root/Dev/ps3-self-tools/pup/pup x "$PUP" fw-src

    # 2. le tar -> les 25 tranches dev_flash*.tar.aa.*
    mkdir -p fw-src/upd && cd fw-src/upd && tar xf ../update_files.tar

    # 3. chaque tranche est un SDAT SCE chiffre -> un tar "content"
    UNPKG=/root/Dev/_untrusted/allps3tools/kakaroto-ps3tools/unpkg
    $UNPKG dev_flash_000.tar.aa.2025_02_19_001719 /tmp/df-000
    tar xf /tmp/df-000/content -C /root/Dev/devflash-out

Les 25 tranches réunies donnent `dev_flash/` (175 Mo) et `dev_flash3/`.

## L'utiliser

    GOWA_FLASH_DIR=/root/Dev/devflash-out/dev_flash ./build-rdr/ps3base --no-launcher

## Les outils, avec leur chemin exact

| outil | chemin |
|---|---|
| `pup` | `/root/Dev/ps3-self-tools/pup/pup` |
| `unpkg` | `/root/Dev/_untrusted/allps3tools/kakaroto-ps3tools/unpkg` |

**Attention** : `unpkg` est dans `kakaroto-ps3tools`, **pas** `kakaroto-ps3utils`.
Le second contient les sources et `decrypt_dev_flash.sh` qui fait la boucle
complète, mais le binaire est dans le premier.

## Ce qui reste bloquant après

Avec le flash réel, RDR tourne 78 s puis meurt en **SIGSEGV** — un blocage
différent, bien plus loin dans le jeu. C'est la prochaine frontière, et elle n'a
plus rien à voir avec le SPU ni l'audio.
