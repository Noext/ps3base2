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

## 4. Dechiffrer les tranches : `unpkg`

Chaque `dev_flash_NNN.tar.aa.*` est un conteneur `SCE\0` de **`header_type` 3** :
un SDAT de donnees, pas un SELF executable. Deux outils existent sur cette
machine, dans `_untrusted/allps3tools/kakaroto-ps3tools/` :

| Outil | Pour quoi |
|---|---|
| `unpkg <in> <outdir>` | le SDAT firmware -> un dossier avec `content` (tar) |
| `unself <in> <out>` | un SELF de module -> un ELF ; **ne marche pas** ici, trop ancien |

`unpkg` lit ses cles dans **`~/.ps3/`**. Elles sont dans
`_untrusted/allps3tools/kakaroto-ps3keys/` et `~/Dev/ps3keys/` ; copier le
contenu, sans le README :

```bash
mkdir -p ~/.ps3
cp /root/Dev/ps3keys/{app,iso,lv1,lv2,pkg,ldr,rvk,spp}-* ~/.ps3/
```

Sans elles : `no key found`. Avec :

```bash
unpkg dev_flash_001.tar.aa.2025_02_19_001719 /tmp/df001
# -> /tmp/df001/content  (tar), plus info0 et info1
tar xf /tmp/df001/content -C /tmp/dffull      # -> dev_flash/...
```

Les tranches se recollent : chacune porte une partie de l'arbre `dev_flash/`, et
extraire les 24 dans le meme dossier reconstitue l'ensemble. Resultat mesure :
**436 `.sprx`, dont 142 dans `dev_flash/sys/external/`**.

## 5. Les `.sprx` sont encore des SELF : les passer en `.prx`

Un `.sprx` sorti du PUP est toujours un conteneur `SCE\0` (`self_type` 4,
`key_revision` 0x1c). Le runtime veut des ELF : c'est ce que sont les huit
`fw/4.91/*.prx` existants (`7f 45 4c 46`).

**L'outil du depot le fait, celui de kakaroto non.** `unself` (kakaroto) echoue
avec `self_decrypt_header failed` : ses tables de cles s'arretent aux revisions
315-356 et il ne connait pas 0x1c. `tools/unself.py` du depot lit
`keys/self_keys.json` (89 entrees) et gere la revision :

```bash
cd tools && python3 - <<'PY'
import sys; sys.path.insert(0,'.')
from unself import Self
s = Self('/tmp/dffull/dev_flash/sys/external/libfiber.sprx')
open('/root/Dev/ps3base2/fw/4.91/libfiber.prx','wb').write(s.elf())
PY
```

Mesure : **32 modules dechiffres, 0 echec.**

## 6. Brancher les nouveaux modules

1. Les mettre dans `fw/4.91/*.prx` (faits : `libfiber`, `libnet`, `libvoice`,
   `libsysutil`, `libsysutil_np`, `libsysutil_np2`, `libsysutil_np_trophy`,
   `libnetctl`, `libhttp`, `libssl`, `libaudio`, `libio`, `librtc`, `libfont`,
   `libfontFT`, `libfreetype`, `libresc`, `libsail`, `libl10n`, `libavcdec`,
   `libadec`, `libdmux`, `libgem`, `libusbd`).
2. Les lister dans `recomp/lle_modules.txt`, un par ligne.
3. `tools/layout_for.py --elf <titre> --modules recomp/lle_modules.txt --out <gen>/lle_layout.json`
   calcule leurs bases depuis la fin du texte du titre.
4. `tools/prx_relocate.py <layout> fw/<ver> fw/<ver>/reloc` : les reloge. Le
   dossier de sortie **doit exister**.
5. Le manifeste d'images doit lister chaque module. `tools/setup.py --steps ppu`
   les lifte tous.

Effet mesure sur Red Dead Redemption : les imports fournis passent de **106 a 308
sur 343**, et ce qui reste a ecrire en HLE de **237 a 35**.

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
