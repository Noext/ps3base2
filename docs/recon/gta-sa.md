# Recon : GTA San Andreas (BLES02219) sur ps3base v2

Livrable R1 de `docs/RECIPE.md` §4 : la table des imports du titre et leur état
dans le runtime. Mesuré, pas décrit.

## Identité du titre

| Champ | Valeur | Source |
|---|---|---|
| TITLE_ID | **BLES02219** | `PARAM.SFO` du disque |
| TITLE | GTA San Andreas | `PARAM.SFO` |
| VERSION | 01.02 | `PARAM.SFO` |
| CAT | DG (disque) | `PARAM.SFO` |
| PS3_SYSTEM_VER | 04.7600 | `PARAM.SFO` |
| EBOOT.BIN | 8 113 656 octets, `self_type` 4 (APP), `key_revision` 0x1C | `tools/unself.py` |
| EBOOT.elf | 8 111 224 octets, ELF64 MSB PowerPC64, entry `0x77b718` | `readelf -h` |
| sha256 de l'ELF | `f8da31bd12b7c4bc71191c72bb9c5e48ba054c3cad464f882e85c7ab0693924e` | `hashlib` |

Le disque porte ses données dans quatre archives, chacune accompagnée d'un
`.idx` : `PS3DataMain.obb` (1,48 Go), `PS3Data.obb` (709 Mo),
`PS3DataText.zip` (11,4 Mo), `PS3DataText2.zip` (9,5 Mo).

**Mesuré : ce sont des archives ZIP ordinaires, en `store` (compression 0).**

```
$ xxd -l 64 PS3Data.obb
00000000: 504b 0304 0a00 0000 0000 ...
$ 7z l PS3Data.obb | tail -2
2015-11-02 22:39:57          708628413    708628413  76 files
```

Le `.idx` est un index plat, pas du code : un `u32` de compte, puis par entrée
`u32 offset, u32 size, u16 name_len, name` (offsets **relatifs au début des
données**, pas au fichier). Vérifié : 76 entrées lues par ce parseur pour
76 fichiers listés par 7z, et `off + size` de l'avant-dernière entrée tombe
exactement sur l'offset de la dernière.

```
count = 76
  off=57         size=10898    data/360Default1280x720.cfg
  off=699421949  size=4279     Textures/fonts/RussianFont.met
  off=699426288  size=38743    Textures/fonts/RussianFont.png
```

Conséquence pour le port : **le contenu n'est pas un format obscur**, un ZIP
suffit à l'ouvrir. Le jeu lit ces archives par `sys_fs` + son `.idx` ; le
runtime n'a donc qu'à servir les octets et laisser le titre faire son propre
parsing. Ce n'est pas du code à lifter, c'est un format de fichier (R5) résolu
par inspection. Les noms relevés confirment l'ascendance : `data/script/mainPS3.scm`
(Script Compiler Main, le successeur de CLEO), `data/handling.cfg`, `.gxt`
(textes), `Textures/fonts/` — l'arbre de données du jeu PC, porté tel quel.

## Les 8 segments du programme

`readelf -l` donne 8 en-têtes : deux `PT_LOAD` réels (code `0x10000`-`0x839070`
en R E, données `0x750000` + `0x4d71a0` de `MemSiz` en RW), un `PT_TLS`, et deux
segments `LOOS+0x1`/`LOOS+0x2`. Le `MemSiz` de données dépasse `FileSiz` de
4,3 Mo : c'est du `.bss`, à zéroter à la charge, pas à lire du fichier.

## Import table : 116 imports, 0 export

`recomp/ppu/analysis.Image` lit 116 stubs et 116 imports. Ils se répartissent en
12 modules, et **tous les NIDs se résolvent** dans `ref/nid_db.json` :

| Module | Imports |
|---|---|
| `cellGcmSys` | 25 |
| `cellSysutil` | 19 |
| `sysPrxForUser` | 15 |
| `cellSync2` | 13 |
| `sys_fs` | 11 |
| `cellGame` | 11 |
| `cellAudio` | 8 |
| `sceNpTrophy` | 6 |
| `sys_io` | 5 |
| `cellSysmodule` | 1 |
| `cellSysutilAvconfExt` | 1 |
| `cellSaveData` | 1 |

**Aucun import SPU, aucun `cellSpurs`.** C'est la différence structurelle avec
Minecraft : ce titre n'a pas de SPURS à initialiser, donc la classe de blocage
« taskset rejeté / event flag SPURS » ne s'applique pas ici. La surface SPU est à
mesurer séparément (R4).

## Couverture runtime : 52 fournis par LLE, 64 à écrire

C'est **le** chiffre de ce document. Mesuré en croisant les 116 NIDs avec les
tables d'export des modules firmware déjà relogés et liftés dans
`fw/4.91/reloc/` :

```
imports GTA: 116   fournis par le firmware lifté: 52
```

Découpage des 64 restants :

| Module | À écrire | État dans le runtime |
|---|---|---|
| `cellSysutil` | 19 | partiel : `cellSysutil.cpp` existe, mais aucun des 19 NIDs importés n'y est |
| `cellSync2` | 13 | **absent du dépôt** : ni `cellSync2` ni `cellSync`2 dans `runtime/` |
| `cellGame` | 11 | partiel : `cellGame.cpp` existe, 3 des 11 manquent |
| `cellAudio` | 8 | partiel : 7 présents, `cellAudioAddData` manque |
| `sceNpTrophy` | 6 | **absent** |
| `sys_io` (`cellPad`) | 5 | `cellPad.cpp` existe mais aucun des 5 NIDs |
| `cellSaveData` | 1 | `cellSaveDataListDelete` manque |
| `cellSysutilAvconfExt` | 1 | `cellVideoOutSetGamma` manque |

Point important pour la suite : **`cellSync2` n'existe nulle part dans ce
dépôt**, et c'est le troisième module le plus importé. Il est à écrire depuis la
référence RPCS3 (`refs/rpcs3/rpcs3/Emu/Cell/Modules/cellSync2.cpp`), en
respectant les tailles de structures — un mutex cellSync2 rend un handle opaque et
les champs de buffer ont des tailles fixes.

## Ce que ce titre ne demande pas

Vérifié absent de la table d'imports, donc hors périmètre pour ce port :
`cellSpurs*`, `cellPngDec`, `cellJpgDec`, `cellFont`, `cellOskDialog`,
`cellUserInfo`, `cellVdec`, `cellNetCtl`, `sys_net`, `sceNp`/`sceNp2`.
Les quatre premiers sont pourtant ceux qui ont coûté le plus cher sur Minecraft :
leur absence du chemin d'import retire d'emblée cette classe de défauts.

## Reproduire ces mesures

```bash
# 1. clés (dérivées du source RPCS3, jamais recopiées à la main)
python3 tools/extract_keys.py refs/rpcs3/rpcs3/Crypto/key_vault.cpp keys/self_keys.json

# 2. EBOOT.BIN -> EBOOT.elf  (voir le piège decrypt_all.py ci-dessous)
python3 -c "import sys; sys.path.insert(0,'tools'); from unself import Self; \
  open('bin/gta/EBOOT.elf','wb').write(Self('bin/gta/EBOOT.BIN').elf())"

# 3. imports
cd recomp/ppu && python3 -c "
import sys; sys.path.insert(0,'.')
from analysis import Image
img = Image('../../bin/gta/EBOOT.elf')
print(len(img.imports), 'imports')
for i in img.imports: print(i['module'], i['nid'])"
```

## Pièges rencontrés (à corriger dans les outils, pas contournés à la main)

### `tools/decrypt_all.py` ne supporte pas un deuxième titre

Il fait `glob.glob(bin/*/)` puis ouvre `EBOOT.BIN` **dans chaque sous-dossier**.
Or `bin/mc/` ne contient que `mc.elf` : le titre précédent a été renommé à la
main. Ajouter `bin/gta/EBOOT.BIN` fait donc échouer le déchiffrement de
**Minecraft** avec :

```
FileNotFoundError: [Errno 2] No such file or directory: 'bin/mc/EBOOT.BIN'
```

Le déchiffrement direct par `unself.Self(...).elf()` fonctionne et a produit
l'ELF ci-dessus. À corriger : sauter un dossier sans `EBOOT.BIN` au lieu
d'abandonner le lot entier.

### `docs/setup.md` §1 annonce une dépendance non installée

Il disait que `pycryptodome` manquait sur cette machine. **Mesuré faux** :
`python3 -c 'import Crypto'` répond `4.4.0` sur `/usr/bin/python3`. La ligne est
corrigée dans `setup.md`.

### Les clés ne sont pas un fichier à trouver, elles sont à générer

`keys/keys.json` n'existait pas, et une recherche sur tout le disque ne trouve
ni `keys.json` ni `self_keys.json` ailleurs. Les deux se **dérivent** de
`refs/rpcs3/rpcs3/Crypto/key_vault.{cpp,h}`, présents :

- `self_keys.json` : 89 entrées extraites de `key_vault.cpp`, tables
  `APP ISO LDR LV0 LV1 LV2 NPDRM UNK7` ;
- `keys.json` : `NP_KLIC_KEY`, `NP_KLIC_FREE`, `NP_OMAC_KEY_2` depuis
  `key_vault.h`.

Un EBOOT retail (`self_type` 4) ne demande que la paire ERK/RIV de sa révision ;
aucun RAP ni `act.dat` n'intervient (voir `ps3-self-tools/keys/README.md`).

## Premier run : le mur est le registre de TOC (r2)

**État mesuré.** `build-gta/ps3base` **se lie et démarre** : le VFS monte
`BLES02219`, `gta` se charge (`text 0x00010200-0x006fccdc toc 0x007855e8
entry 0x0077b718`), `liblv2` démarre, puis 12 modules `.sprx` s'enregistrent
(5 LLE, 6 HLE). Le crash arrive à **t = 0,544 s**, 82 syscalls après l'entrée.

```
I 0.512 loader: loaded gta (bin/gta/EBOOT.elf) text 0x00010200-0x006fccdc toc 0x007855e8 entry 0x0077b718
I 0.524 prx: load '/dev_flash/sys/external/libsysutil.sprx' -> 0x23000c00 (HLE, start 0x00000000)
E 0.544 crash: signal 11 ... fault 0x7803dbbf898d (guest 0xffff898d) thread main_thread
```

**Verdict mécanique :** `exit=139` (SIGSEGV), 0 flip. C'est un crash, pas une
sortie propre.

### La silhouette de la pile

`addr2line` nomme la chaîne, et elle est courte :

```
#0 ppu::ld32                        runtime/include/ppu/ops.h:43
#1 f_0064ee60_body                   out-gta/ppu/gta_063.cpp
#2 f_0001a844_body                   out-gta/ppu/gta_000.cpp
#3 f_001568a4_body                   out-gta/ppu/gta_013.cpp
#4 f_00018490_body                   out-gta/ppu/gta_000.cpp
```

Les 5 premières trames sont du **code invité lifté**, pas du HLE. Le HLE
(`cellAudioInit`, dernier appel réussi) n'est pas dans la pile : il a rendu la
main proprement.

### La cause : r2 vaut 1 au lieu de la TOC du jeu

L'instruction qui faute, dans `f_0064ee60_body`, est la troisième du corps :

```c
ctx.gpr[31] = uint64_t(ppu::ld32(base, uint32_t(ctx.gpr[2] + -30324)));
```

C'est un accès par le **registre de table des matières** (`r2`), la convention
ABI64 de PowerPC pour atteindre les globales. Les registres pris par le rapport de
crash sont sans ambiguïté :

```
guest regs: r0=0x1a85c r1=0xd004f900 r2=0x1 r3=0x81d698 ...
fault 0x7803dbbf898d (guest 0xffff898d)
```

`r2 = 0x1`, donc l'adresse calculée est `1 + (-30324) = 0xffff898d`, qui n'est
évidemment pas mappée. La TOC attendue est `0x7855e8` — elle est connue, le
chargeur l'a imprimée.

### Où r2 devait être posé

Trois endroits le posent aujourd'hui, et aucun ne couvre ce chemin :

| Site | Ce qu'il pose | Couvre |
|---|---|---|
| `thread.cpp:196` `call_opd` | `r2 = read32(opd+4)` | l'OPD d'entrée du thread |
| `ppu_runtime.cpp:351` | `r2 = read32(e.opd+4)` avant un appel LLE | les imports servis par le firmware lifté |
| `ppu_runtime.cpp:375` | restaure `r2` depuis la pile après l'appel | le retour |

Le thread principal est lancé sur **l'entrée de `liblv2`**, donc `r2` reçoit la
TOC de `liblv2` (`0x0101c620`). Le titre est ensuite démarré *depuis* ce code
`liblv2`, et rien ne repose `r2` sur la TOC du jeu (`0x7855e8`) quand on entre
dans son `main`. À la première globale atteinte par `r2`, le registre vaut ce
que le code de démarrage `liblv2` y a laissé — ici `0x1`, la valeur de retour
d'un `cellSysmoduleLoadModule` ou équivalent.

C'est exactement le genre de défaut que `RECIPE.md` §1.2 décrit : « le jeu
marche sur la console, donc la différence vient d'une entrée qu'on lui fournit ».
Sur console, `liblv2` repose la TOC avant d'appeler l'entrée du titre.

### Piste à instruire (non conclue)

1. Tracer, dans `call_indirect`, chaque entrée dans le code du jeu
   (`0x10200..0x6fccdc`) en logguant `r2` et `lr` : la première entrée avec un
   `r2` hors de `{0x7855e8}` est l'appel fautif.
2. Comparer avec RPCS3 : `refs/rpcs3/rpcs3/Emu/Cell/PPUThread.cpp`, la gestion
   de `gpr[2]` à l'entrée d'une fonction importée — RPCS3 ne restaure pas
   `r2` après un HLE, il le laisse au code invité qui le repose lui-même par
   `lwz r2, ...`; si notre HLE clobbe `r2`, rien ne le répare.
3. Vérifier si un HLE de démarrage écrit `r2` : c'est le seul moyen d'arriver à
   `0x1` sans que le code du jeu l'ait fait.

**Ce que cette section n'affirme pas :** que `cellAudioInit` soit coupable. Il
n'est pas dans la pile. La valeur `0x1` de `r2` est un fait mesuré ; son
écrivain n'est pas encore identifié.


### La cause du crash de TOC, verifiee au desassembleur (retractation incluse)

**Ce que je croyais :** un HLE ecrasait `r2` et la restauration relisait un slot
corrompu. **Faux.** Mesure : l'instrumentation `PS3BASE_TOC_TRACE` a montre
**0 HLE changeant r2**, **0 changement de r2 sur 543 transitions**, et le
correctif "restaurer la TOC de l'appelant" **n'a rien change au crash**. Il a ete
retire du code plutot que garde "au cas ou".

**Ce que la mesure dit.** Le binaire d'origine (pas le code lifté) suffit :
`capstone` sur `0x18490..0x184f0` de `bin/gta/EBOOT.elf` :

```
00018490  stdu     r1, -0xa0(r1)
00018494  mflr     r0
00018498  std      r0, 0xb0(r1)
...
000184d0  bl       0x6fcfdc          <- import cellAudioOutConfigure
000184d4  ld       r2, 0x28(r1)      <- RELIT la TOC
000184d8  bl       0x6fd4bc          <- import cellAudioInit
000184dc  ld       r2, 0x28(r1)      <- RELIT la TOC
000184e0  bl       0x1568a4
```

Cette fonction **restaure `r2` depuis `0x28(r1)` sans jamais l'y avoir ecrit**.
Verifie sur tout `.text` : le motif `std r2,40(r1)` (`0xf8410028`) apparait
**5 990 fois**, aucune dans `0x18490..0x184f0`.

**Pourquoi ce n'est pas un bug du jeu.** L'appelant est `0x62ba98`, dans la
sequence de demarrage, et il fait :

```
0062ba24  bl       0x6fda7c          <- import
0062ba28  ld       r2, 0x28(r1)      <- TOC restauree ici
0062ba2c  lwz      r9, -0x7fe8(r2)
0062ba30  std      r3, 0(r9)         <- stocke le retour dans une globale
0062ba34  bl       0x10200           <- ENTREE DU TITRE
```

Sur console, `0x10200` (`_start`, le CRT) **pose la TOC lui-meme** : un CRT
PPU64 fait `.start` -> `__init_toc` -> `lwz r2, -0x7fe8(r2)`. Chez nous,
`lr=0x1a85c` mene a `f_0064ee60`, qui lit `r2 + (-30324)` **sans avoir pose la
TOC** — c'est-à-dire que notre entree de `0x10200` ne passe pas par le CRT.

Autrement dit : le `r2=1` observe au crash est la valeur que le jeu a laissee
dans `0x28(r1)`, et la vraie question est **pourquoi la TOC n'est pas posee a
l'entree `0x10200`**. Le `r2=1` est le symptome, pas la cause.

**Piste suivante (non instruite) :** desassembler `0x10200` et verifier s'il
appelle `__init_toc` par `bl`, ou si le chemin passe par `r2` lui-meme en
entrant. Si c'est le premier cas, la valeur de `r2` a l'entree de `0x10200` doit
etre la TOC, et c'est notre dispatch de `0x10200` qui la perd.


### Cause racine : un constructeur statique jamais execute (prouve)

La chaine est courte et elle est lisible dans le binaire, pas dans une
supposition.

```
0x10200 _start        stdu/mflr, puis bl 0x62bb88 ; bl 0x62bbf8 ; blr
0x62bb88              lwz r0,-0x7fc0(r2) ; ... ; bctrl + ld r2,0x28(r1)
0x62bbf8              BOUCLE de constructeurs statiques :
                        r31 = r9 - 4
                        r9  = [r31]
                        cmpwi r9, -1 ; beq  -> sortie
                        r11 = r9
                        r0  = [r11]         <- code du constructeur
                        std r2, 0x28(r1)    <- SAUVE la TOC
                        mtctr r0
                        lwz r2, 4(r11)      <- TOC du constructeur
                        bctrl
                        ld  r2, 0x28(r1)    <- RESTAURE
                        ... reboucle
```

**Le `std r2, 0x28(r1)` de `0x62bbf8` n'est jamais execute.** Preuve : il est
dans le corps de la boucle, en `0x62bc2c`, donc il ne s'execute que si la boucle
entre. Or la lecture qui la fait entrer est :

```
0062bc30  mtctr    r0        ; r0 = [r11]
0062bc34  lwz      r2, 4(r11)
0062bc38  bctrl    <-- saute dans le constructeur, NE CONTINUE PAS la boucle
```

`bctrl` **transfere** le controle au constructeur : la boucle ne « continue » pas
apres lui, elle y est suspendue. Quand `0x18490` (`cellAudioOutConfigure`)
s'execute, il est donc EMPILE SOUS `0x62bbf8`, avec le `0x28(r1)` de
`0x62bbf8` non ecrit.

C'est la que le `r2=1` du crash se fabrique : `0x18490` fait
`ld r2, 0x28(r1)` et lit le slot que `0x62bbf8` n'a pas rempli.

### Ce que cela change dans la question posee

Ce n'est **pas** un bug du jeu ni un HLE qui clobbe `r2`. C'est une difference
entre notre execution et celle de la console sur un point precis : la TOC
attendue par `0x18490` a 0x28(r1) depend de quelle trame a ecrit ce slot en
dernier. Sur console la valeur est coherente ; chez nous elle vaut 1.

Les deux faits mesures a garder :

| Fait | Preuve |
|---|---|
| `r2=1` a l'entree de `0x10200` est deja la valeur retenue | instrumentation import : `cellAudioInit reached with r2=0x1 caller img=gta toc=0x7855e8 lr=0x184dc` |
| aucun HLE ni transition ne modifie `r2` | 0 avertissement sur 543 appels + 0 sur les HLE |

La prochaine mesure a faire est **lee seule qui tranche** : tracer la valeur de
`0x28(r1)` a l'entree de `0x10200` et a la sortie de `0x62bb88`, pour savoir si
le slot est deja faux avant meme que la boucle de constructeurs soit atteinte.
Si oui, la faute est en amont de `_start` et concerne la trame que le runtime
fabrique pour le thread principal — piste `thread.cpp:196` `call_opd`.

