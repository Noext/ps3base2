# Comment obtenir les modules firmware (dev_flash)

**Lu une fois, ecrit ici pour que le prochain titre n'ait pas a le redécouvrir.**

Le runtime ne peut pas lifter un module qu'il n'a pas. Sur cette machine, `fw/4.91/`
ne contient que **8 modules**, et c'est ce qui a bloque Red Dead Redemption : il
demande `libfiber`, `libvoice` et `libnet`, qui n'y sont pas.

## 1. Ou est le firmware

```
smb://truenas.local/nas/media_data/_library/RetroGaming/retrodeck/firmwares/ps3/PS3UPDAT.PUP
```

Monte sur `/mnt/truenas-nas2/media_data/_library/RetroGaming/retrodeck/firmwares/ps3/PS3UPDAT.PUP`,
**206 177 436 octets**, version de firmware de **2025-02-19**. C'est un vrai firmware
complet, pas les stubs de 256 Mo qu'on trouve dans `PS3_UPDATE/` des disques de jeux
(ceux-la sont des mises a jour minimales et ne contiennent pas le `dev_flash` complet).

Verifier avant de perdre du temps :

```bash
/root/Dev/ps3-self-tools/pup/pup i "$PUP"
# attendu : File count: 9, license.xml ~309 KB, update_files.tar ~194 MB
```

Un `file_count` de 65514 avec un fichier de 256 Mo est un disque de jeu, pas un firmware.

## 2. Extraire le PUP

```bash
cd <repo>
/root/Dev/ps3-self-tools/pup/pup x "$PUP" fw-src
```

Le repertoire de destination **ne doit pas exister**. Ce que cela produit :

| Fichier | Taille | Contenu |
|---|---|---|
| `license.xml` | 309 Ko | le contrat de licence firmware |
| `update_files.tar` | 194 Mo | **les paquets**, dont `CORE_OS_PACKAGE.pkg` |
| `ps3swu.self` | 5,7 Mo | l'installateur de mise a jour |
| `vsh.tar`, `dots.txt`, `version.txt` | petits | |

Les entrees d'identifiant inconnu sont ignorees avec `*** Unknown entry id, file
skipped ****` ; ce n'est pas une erreur.

## 3. Ce que contient `update_files.tar`

51 entrees. Les deux qui comptent :

- `CORE_OS_PACKAGE.pkg` (5,9 Mo) : le systeme de base ;
- `dev_flash_000.tar.aa.<horodatage>` ... `dev_flash_009` et `dev_flash3_024` :
  **les modules**, decoupes en tranches d'environ 2 a 5 Mo.

```bash
cd fw-src
tar xf update_files.tar CORE_OS_PACKAGE.pkg dev_flash_001.tar.aa.2025_02_19_001719
tar tf update_files.tar | grep dev_flash     # lister les tranches exactes
```

## 4. Le piege : ce ne sont pas des SELF

```
$ xxd -l 16 dev_flash_001.tar.aa.2025_02_19_001719
00000000: 5343 4500 0000 0002 0000 0003 0000 0000   SCE.............
```

La signature est `SCE\0`, comme un SELF, mais **`header_type` vaut 3**, et
`tools/unself.py` echoue dessus :

```
error unpack requires a buffer of 24 bytes
```

Raison : `unself.py` lit `appinfo_off`, `elf_off`, `phdr_off`, `shdr_off` a
`0x20..0x70` et attend la mise en page d'un SELF executtable. Un conteneur de
**donnees** (SDAT, `header_type` 3) n'a pas ces champs : les octets a cet endroit
sont du materiel chiffre, d'ou la taille absurde (24 octets demandes pour une
structure tronquee).

**Ce qu'il faut a la place :** un outil qui dechiffre un SDAT firmware avec la cle
de revision, pas un `unself`. C'est le role de `scetool` (option `--decrypt`) ou de
l'extracteur de PUP de RPCS3, aucun des deux n'est installe ici.

## 5. Ce que le depot fournit deja

`fw/4.91/*.prx` : 8 modules, ceux que les trois titres portes jusqu'ici demandaient.

```
libfs  libgcm_sys  liblv2  libsmvd2  libspurs_jq  libsre  libsysmodule  libvdec
```

Ce sont des `.prx` deja dechiffres, prets pour `tools/prx_relocate.py`. Ni
`libfiber`, ni `libvoice`, ni `libnet`, ni `libsysutil` n'y sont.

**Les 8 ne viennent pas de ce PUP** : ils etaient la avant, d'une source anterieure
sur cette machine (aucun `.sprx` n'existe nulle part dans `~/Dev`). Le PUP de 2025
est plus recent que le firmware 4.91 que ces modules supposent.

## 6. Consequence pour un titre qui demande plus

Red Dead Redemption importe 343 fonctions ; 106 sont fournies par les 8 modules,
**237 ne le sont pas**, dont 92 pour la pile reseau/PSN. Sans `libfiber.sprx` le
scheduler de fibres ne demarre pas : le titre flippe en boucle sans dessiner
(`fifo packets: flip 72-85%, draw 0.2%`) et l'ecran reste noir.

Autrement dit : **ecran noir + flip eleve + draw proche de zero = un import de
module manquant**, pas un probleme de rendu. La liste des imports non implementes
du log le nomme directement.

## 7. Ce qu'il reste a faire

1. Trouver ou installer un `scetool` (ou l'extracteur de RPCS3) capable de
   dechiffrer un SDAT firmware de revision 3.
2. Extraire les `dev_flash_NNN` de `update_files.tar`, les dechiffrer, recuperer
   `sys/external/*.sprx`.
3. Dechiffrer les `.sprx` en `.prx` si necessaire, puis les placer dans
   `fw/<version>/` et les ajouter a `recomp/lle_modules.txt`.
4. Relancer `tools/setup.py` : `layout_for.py` calculera leurs bases et
   `prx_relocate.py` les relogera. Rien d'autre a changer.

**Ne pas commiter** le firmware ni les modules extraits : `.gitignore` couvre
`*.prx`, `*.sprx`, `*.pup`, et `fw-src/` doit l'etre aussi.
