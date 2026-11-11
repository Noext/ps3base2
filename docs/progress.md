# Progress

Proof is a command and its output. A milestone without one is not done.

## Phase 0 -- the fork

### 0.1 Skeleton -- GREEN

**Criterion:** configuring without the game-derived inputs stops at once and
names the missing step.

```
$ cmake -S . -B build -G Ninja
CMake Error: No generated PPU code in .../out/ppu.
  python3 tools/setup.py --steps ppu --elf <decrypted title.elf>
```

Without the guard a GLOB over an empty directory produced a library with no
sources and a link error hundreds of lines later.

### 0.2 The game profile -- GREEN

**Criterion:** the runtime contains no title identity.

```
$ grep -rn 'BCES01741' runtime/ | wc -l
0
$ grep -cE 'BCES01741|"gowa"|GOWA\.SELF|God of War' runtime/src/main.cpp
0
```

Four leaks were silent ones: the VFS resolved the disc and update folders by a
literal title id, the launcher defaulted to that same path, the presenter named
its window after the game, and the cellSysutil cache path carried the id as a
fallback label. Each made every other disc fail quietly rather than loudly.

### 0.3 The empty profile boots a foreign title -- GREEN

**Criterion:** a title with no profile starts, runs and presents frames.

```
$ GOWA_HEADLESS=1 ./build/ps3base
I main: game profile: '(none)' -- generic runtime only
I loader: loaded mc (bin/mc/mc.elf) text 0x00010200 toc 0x00bd7998 entry 0x00bbe520
I spu: group_create 'MinecraftCellSpursKernelGroup' num 5
I ppu: _sys_ppu_thread_create 'MinecraftSpursHdlr1' entry 0x012313a4
I rsx: context at 0x40100000
I rsx: flip 841 (buffer 0)
```

841 flips, 4 PPU threads, 12 SPURS registrations, **0 crashes**, no stubs.

The earlier attempt at this measurement needed 46 no-op stubs and still
faulted, because two things install the God of War layer and only one was
gated: `hooks::init()` at startup and `install_builtin()` lazily from the first
SPU thread. Both now ask the profile.

**What the flips are:** `draw 0.0%`, `clear 0.0%`, `methods 0.0% (2k/s)`.
The game runs its loop and presents, but submits no geometry. That is the
next question, and it is the RSX front end, not the profile seam.

### 0.4 The GoWA profile still works -- GREEN

**Criterion:** the title that owns the special layer still selects it, and an
unknown name selects nothing.

```
$ ./profile_test
profiles: 1
find(gowa): yes
find(nope): no
first: 'gowa'
```

The check found a real defect: `namespace spu { void install_gowa_natives(); }`
sat inside the anonymous namespace that wraps the profile class, so it declared
a **different** `spu::` and the definition in `hle_builtin.cpp` was never
reached. Every build would have linked a profile whose SPU natives silently did
nothing. Declared at file scope, it resolves.

## Phase 1 -- what the old tree kept

### 1.1 The LV2 table, with ENOSYS -- GREEN

**Criterion:** every number the specification dispatches is accounted for, and
an unimplemented one refuses rather than succeeding.

```
$ ./lv2_enosys
lv2: 451 listed, 180 implemented (39.9%), 3 checks

$ python3 tools/lv2/gen_syscall_table.py
gen_syscall_table: 451 syscalls listed, 180 implemented (39.9%)
  process        5/17    29.4%
  ppu_thread    18/32    56.2%
  event_queue   10/10   100.0%
  filesystem     1/9     11.1%
  modules        0/43     0.0%
```

**The dispatch was returning a fabricated success.** In `ppu_runtime.cpp` an
unimplemented syscall logged a warning and then set `r3 = 0` -- `CELL_OK` from
something that did not run. Every caller believed its object had been created.
It now returns `0x8001003F` with the specification name of the number in the
log, which is what makes the gap actionable.

**Coverage is measured, not claimed.** The generator reads the LV2
specification for the numbers and names, then scans this tree's own
`LV2_SYSCALL(n, ...)` registrations to decide what is implemented. The first
run reported 0/451 because it was looking for a definition named after the
syscall and for `.c` files; this tree registers by number and its sources are
`.cpp`. Both fixed, and the numbers above are the real state.

### 1.2 The PS3 memory map, tested -- GREEN

**Criterion:** the map's guarantees about the PS3 address space are checked,
not assumed.

```
$ ./memory_map
memory: 22 checks passed (0 failed)
```

Covers what fails silently: two allocations must not alias, an allocation
honours its alignment, a store lands inside its own range and not in the
neighbour's, the big-endian accessors round-trip (a byte-swapped load still
looks plausible and is wrong), a range spanning an unmapped page is refused,
freeing an unknown address is refused, and immutability is per-range.

The test caught its own bad assumption first: it asserted that a range twice
an allocation's size is unmapped, but the allocator legitimately places the
next allocation there. The check now uses an address far from anything,
because a test that encodes a layout it does not control is a test that will
lie later.


## World creation: the wall at "Finalizing..." (rounds 18-85)

The crash that blocked world creation is **fixed**: `sys_vm_get_statistics` wrote
64 bytes into a 48-byte structure and clobbered the caller's saved registers.
Eight further defects were found and fixed on the same path:

| commit | defect |
|---|---|
| `0663ea9` | `sys_vm_memory_map` (300) missing -> null pool pointer -> crash |
| `d5f3ea5` | `sys_vm_get_statistics` 64 bytes instead of 48 |
| `611b678` | `sys_vm_return_memory` OK instead of `CELL_EBUSY` |
| `e9d4a54` | `sys_vm_append_memory` accepted 4 GiB behind a 64 MiB mapping |
| `28af660` | `pmem_total`/`pmem_used` are u32, not u64 |
| `f23af51` | `cellRtc` entirely absent (four functions) |
| `4857bd2` | `cellAudioOutSetCopyControl` missing |
| `ae01b76` | `pmem_total` reported vsize, so the title computed a bogus append |

The title now creates its world (`sys_vm_memory_map -> 0x100c0000`), the server
thread runs and exits cleanly, and no run of any length crashes.

**What remains**: the screen stays on "Finalizing..." with the bar full, and no
workload generating terrain ever reaches the SPUs. Measured, in order:

- the progress bar reaches 10140 green pixels and does not change over 15 samples;
- `cellSpurs` is fully initialised (60 calls, every one returning 0) and
  `cellSpursEventFlagSet` alone runs ~19922 times per run;
- SPUs are not dead: `spuIdling` cycles through `0x1f 0x0f 0x1e 0x17 0x1b 0x1d`,
  so at least one SPU is busy in about 31 % of samples, and the kernels execute
  tens of millions of instructions;
- the thread `(4J) Chunk update` waits on LV2 event flag `0x98046c00` for bits
  `0x3` while the single set on that flag carries `0x2`; bit 0 is never set by
  anything we execute, and `flag_match` matches RPCS3 exactly;
- no `UserAutoSave`, no world directory, no world textures and no chunk geometry
  are ever requested.

### A trap worth remembering

Three conclusions in this stretch were wrong **because the log was capped**:
`sys_fs_read` looked like a hang, `cellSpursEventFlagSet` looked like it was
never called, and the SPURS loop looked like it stopped at t=1.3 s. Each cap
(`n < 60`, `n < 30`) truncated the very evidence that contradicted the guess.
Sample a counter in time, and never read "the log ends here" as "the title
stopped".

## Portage d'un second titre -- GTA San Andreas (BLES02219)

Le runtime est générique depuis la phase 0 ; ce qui suit est la mise en service
d'un **second** disque à côté de Minecraft, et les défauts d'outillage que cela a
révélés. Recon complète : `docs/recon/gta-sa.md`.

### G-1 -- Le disque se déchiffre et se lifte, isolé de Minecraft -- GREEN

**Critère :** un second titre produit ses propres sources PPU sans toucher à
`out/` (donc sans recompiler ni casser le premier).

Les clés se **dérivent** de `refs/rpcs3` (elles n'existent nulle part sur le
disque en tant que fichier) :

```
$ python3 tools/extract_keys.py refs/rpcs3/rpcs3/Crypto/key_vault.cpp keys/self_keys.json
89 keys ['APP', 'ISO', 'LDR', 'LV0', 'LV1', 'LV2', 'NPDRM', 'UNK7']
```

```
$ python3 tools/setup.py --steps ppu --images recomp/images-gta.json \
    --elf bin/gta/EBOOT.elf --out-root out-gta --gen-root out-gta/gen --allow-unknown
[setup] ELF bin/gta/EBOOT.elf: unknown sha256 f8da31bd..., continuing
[setup] PPU recompilation into out-gta/ppu from recomp/images-gta.json
gta            functions  20082 files  71 imports  116 exports    0
total functions 22085 imports 361 exports 781
unimplemented: []
```

20 082 fonctions, 116 imports, **zéro import non implementé**, et `out/` intact.
La ligne d'image générée porte bien l'identité du titre :

```
{"gta", "bin/gta/EBOOT.elf", false, 0x10200, 0x6fccdc, 0x7855e8, 0x77b718},
```

**Deux défauts d'outillage corrigés pour y arriver**, tous deux invisibles tant
qu'un seul titre existait :

| Défaut | Symptôme mesuré | Correction |
|---|---|---|
| `tools/decrypt_all.py` ouvrait `EBOOT.BIN` dans **chaque** sous-dossier de `bin/` | ajouter `bin/gta/` faisait échouer le lot sur `bin/mc/EBOOT.BIN` inexistant, donc cassait Minecraft | sauter un dossier sans `EBOOT.BIN` au lieu d'abandonner |
| `tools/setup.py --elf` ne pilotait **que** les blocs SPU ; le PPU venait toujours de `recomp/images.json` | un lift « de GTA » produisait en réalité 20 682 fonctions **Minecraft**, avec `gta` seulement dans le message | nouveau `--images`, défaut inchangé |

Le second est le plus dangereux : la commande affichait `PPU recompilation into
out-gta/ppu`, sortait `exit=0` et `unimplemented: []` — mais `out-gta/ppu/table.cpp`
contenait `{"mc", "bin/mc/mc.elf", ...}`. Un lift réussi en apparence, sur le
mauvais ELF. C'est la règle « mesurer, pas supposer » appliquée à un outil :
lire la ligne d'image produite, pas le message de l'outil.

### G-2 -- Ce que le second titre change dans le bilan

Mesuré sur la table d'imports (détail dans la recon) :

| | Minecraft (BLES01976) | GTA SA (BLES02219) |
|---|---|---|
| imports | 345 | **116** |
| `cellSpurs*` | oui, 12 enregistrements | **aucun** |
| `cellGcmSys` | indirect | **25 imports directs** |
| `cellSync2` | non | **13 imports, module absent du dépôt** |

Le firmware lifté (`fw/4.91/reloc/`) couvre **52 des 116** imports : tout
`cellGcmSys`, `sys_fs` et `sysPrxForUser`. Les 64 restants sont du HLE à écrire,
`cellSync2` en tête (13), puis `cellSysutil` (19) et `cellGame` (11).

`cellSync2` n'existe **nulle part** dans `runtime/` : c'est le premier module
entièrement neuf que ce port demande.


### G-3 -- Le manifeste SPU et la table d'images sont propres a GOWA -- GREEN

**Critere :** un lift de GTA qui ne porte **aucune** adresse de GOWA.

Deux fichiers de `recomp/` sont, a la lecture, uniques au titre d'origine :

| Fichier | Mesure | Consequence |
|---|---|---|
| `recomp/images.json` | 1re entree `{"name":"mc", "elf":"../bin/mc/mc.elf"}` | tout lift "GTA" produisait le code de **Minecraft** (voir G-1) |
| `recomp/spu/block_manifest.txt` | 19 703 lignes, **17 181** nomment `gowa` | `block 04050 out of range in gowa` : les offsets pointent l'ELF de GOWA |

Le manifeste est le plus pernicieux : c'est une liste de blocs SPU avec leur
**offset dans l'ELF**, donc chaque ligne est un fait sur un fichier precis. Un
manifeste d'un titre lu contre un autre ELF ne produit pas une erreur de sens,
il produit soit un depassement (`out of range`), soit des blocs faux.

**Correction :** `recomp/images-gta.json` cree, et pour GTA un manifeste **vide**,
parce que la mesure dit que ce titre n'a pas de SPU a lifter :

```
$ python3 tools/setup.py --steps spu --images recomp/images-gta.json --elf bin/gta/EBOOT.elf ...
[setup] 0 SPU blocks extracted into out-gta/gen/blocks/run_manifest.bin
[setup] SPU AOT codegen into out-gta/spu
0 blocks, 0 instructions, 0 files, 0 regions covering 0 blocks
[setup] 0 native SPU sources in out-gta/gen/spu
```

Le pipeline accepte **0 bloc** et ecrit quand meme `spu_registry.cpp`, ce qui est
le comportement correct : `GOWA_SPU_GENERATED_DIR` exige ce fichier, pas des
blocs. Un titre sans SPU n'a rien a faire interpreter.

La table d'imports corrobore : **aucun `cellSpurs*`**, aucun import SPU. Le
titre est entierement PPU + RSX.

### G-4 -- L'arbre de build est isolable sans toucher a `out/` -- GREEN

**Critere :** compiler GTA ne recompile ni ne casse Minecraft.

Trois variables CMake suffisent, toutes deja en cache :

```bash
cmake -S . -B build-gta -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DPS3BASE_GENERATED_DIR=$PWD/out-gta/ppu \
  -DGOWA_SPU_GENERATED_DIR=$PWD/out-gta/spu \
  -DPS3BASE_SPU_NATIVE_GEN_DIR=$PWD/out-gta/gen/spu
```

La detection des dependances de `out-gta/ppu` se fait par `cmake` sur les
`#include` des sources generees, donc un `out-gta` propre donne un arbre propre.
`out/` (Minecraft) reste intact : verifie par `ls out/ppu/table.cpp` et par le
fait que `build/` n'a pas ete recompile.

Ce qui manque et qui n'est **pas** bloquant pour ce titre : `libavcodec` est
absent de la machine, donc `cellVdec` tombe en LLE. GTA n'importe pas
`cellVdec`, donc la question ne se pose pas ici.


## GTA San Andreas : le jeu tourne

**Etat mesure, capture a l'appui :** ecran-titre -> cinematique d'intro (CJ au
poste de police) -> **en jeu a Grove Street**, avec HUD complet : heure 07:05,
argent $350, barre de vie, radar en bas a gauche, un BMX devant le joueur.
11 a 26 fps selon la scene. Aucun forcage actif.

```
I  131.451837 rsx: flip 2821 (buffer 0) worst 154.9 ms, >50ms 60, >100ms 16
I  89.991625 gpu: window closed, exiting      <- ce n'etait pas un crash
```

### Ce qui a rendu cela possible, dans l'ordre

1. **Le `std r2` des stubs d'import** (commit `1e1c5f5`). Le lifter remplace le
   stub entier par un appel a `call_import`, donc le `std r2, 0x28(r1)` que le
   stub executait sur console doit etre refait par nous. Le chemin LLE le
   faisait, le HLE non. Apres correction : premiere image presentee, et le jeu
   sort de son ecran-titre.
2. **`cellSync2`** (`runtime/src/hle/cellSync2.cpp`), 13 imports, module absent
   du depot. Implemente avec de vrais `std::mutex`/`condition_variable` et un
   semaphore compteur par objet invite -- PAS des stubs `CELL_OK`, parce que
   D-002 interdit de dire a un titre que son mutex existe quand il n'existe pas.
   RPCS3 lui-meme est en `todo()` + `CELL_OK` sur ce module, donc il ne pouvait
   pas servir d'oracle de comportement ; seules ses signatures et ses tailles de
   structures ont ete reprises (`cellSync2.h`).

Apres ces deux corrections il ne reste que **3 imports non implementes**, tous
sur des chemins non bloquants a ce stade :
`cellSysutil::cellVideoOutGetNumberOfDevice`,
`cellGame::cellGameGetParamInt`, `cellAudio::cellAudioAddData`.

### Deux erreurs de methode, mesurees et corrigees

**1. Mon compteur de "nonblack" etait faux, et j'en ai conclu que le jeu ne
rendait rien.** Les PPM dumps sortaient a `nonblack 0/921600` alors que l'ecran
affichait un menu lisible a 53 fps. Le dump part du principe que le presentateur
est sur le GPU reel ; sous Xvfb avec `PS3BASE_DUMP_FRAMES` il ne l'est pas. La
lecon est celle deja ecrite dans `docs/RUN.md` (« une frame noire sous ce
variable veut dire pas de renderer, pas rien de dessine ») : j'ai lu un nombre
sans verifier ce qu'il mesurait. **Le seul juge qui vaut ici est une capture de
la fenetre.**

**2. J'ai lance chaque run avec `timeout N`, et cela a ferme la fenetre pendant
que l'operateur jouait.** La trace est explicite : `gpu: window closed, exiting`
a 74,99 s et 89,99 s, exactement les deux budgets. Un run qu'on veut mesurer se
borne en temps ; un run qu'on veut *regarder* ne se borne pas. Desormais :
`nohup setsid ... & disown`, sans `timeout`, et on tue par groupe de processus
avec `kill -9 -- -$PID` quand c'est fini.

### Ce qui reste

- 3 imports non implementes (voir ci-dessus), aucun fatal pour l'instant.
- Les textures de shaders ne sont pas sur le disque : le titre demande
  `Shaders/*.vert` et `*.pix` en ENOENT ; ils ne sont ni dans l'arbre du disque
  ni dans les deux `.obb` (les index listent 76 + 244 entrees, aucune n'est un
  shader). Le jeu tourne quand meme, donc ce chemin est soit inactif, soit
  reimplemente par le titre.
- Un artefact bleu triangulaire visible pres du BMX sur la capture : a
  instruire, probablement une cible de rendu ou un marqueur de mission.
- Vitesse : 11-26 fps selon la scene. Non mesure sur GPU reel hors Xvfb.


## Red Dead Redemption (BLES00680) -- deux murs sur le chemin

Tentative de port du troisieme titre. Etat mesure, pas conclu.

### R1 -- Le disque se dechiffre et se lifte -- GREEN

```
self_type 4  key_revision 0x1  ->  EBOOT.elf 21 886 336 octets
sha256 b893b33fe63ccc3bb372b127503060f5df36c898755df5444bacd1d1afd48f0c
rdr            functions  74415 files 172 imports  343 exports 0
total functions 76418 imports 588 exports 781
unimplemented: []
```

**74 415 fonctions PPU** : 3,6 fois GTA San Andreas, 3,6 fois Minecraft. 343 imports,
tous resolus dans `ref/nid_db.json`, aucun non implemente au lift.

Profil d'import different des deux titres precedents :

| Module | Imports | Note |
|---|---|---|
| `cellSpurs` | 41 | SPU lourd, comme Minecraft |
| `sceNp` | 39 | |
| `cellSysutil` | 34 | |
| `sys_net` | 28 | **le reseau est importe directement** |
| `sceNp2` | 27 | |
| `sceNpCommerce2` | 26 | |
| `cellGcmSys` | 21 | |
| `sys_fs` | 21 | |
| `cellVoice` | 13 | chat vocal |
| `cellFiber` | 8 | fibres, absent des deux autres |

**106 des 343** sont fournis par le firmware lifte ; **227 sont du HLE a ecrire**, dont
92 rien que pour la pile reseau/PSN. C'est le profil le plus lourd des trois titres.

PS3_SYSTEM_VER est **03.3000**, soit un firmware de 2009 : plus ancien que les deux
autres titres, ce qui peut changer les structures attendues de certaines bibliotheques.

### R2 -- Les modules firmware chevauchent le titre -- CAUSE TROUVEE

Premiere compilation :

```
rdr_163.cpp:25295: multiple definition of 'f_01012534(...)';
  liblv2_000.cpp:23362: first defined here
```

Cinq collisions. La cause n'est pas le lifter mais le **layout** :

```
rdr      text 0x00010200 - 0x010d546c
liblv2   text 0x01000000 - 0x01013b68     <- DANS la plage du titre
```

Minecraft s'arrete a `0x8aa674` et GTA a `0x6fccdc`, tous deux **sous** `0x01000000`, donc
`recomp/lle_layout.json` n'avait jamais pose probleme. Red Dead s'etend au-dela et
avale les huit modules.

Le defaut est plus profond que les cinq symboles : `ppu_runtime.cpp:lookup()` rend la
**premiere** plage qui contient l'adresse. Avec ce chevauchement, tout appel a
`liblv2` a `0x01000000` serait resolu vers le code du jeu a la meme adresse.

**Correction :** `recomp/lle_layout-rdr.json` place les modules a `0x21000000` et
au-dela, au-dessus du titre et hors des regions heap. `tools/prx_relocate.py` les
reloge (verifie : 8 modules, relocations appliquees), et `recomp/images-rdr.json`
pointe dessus.

**A generaliser :** le layout devrait etre choisi en fonction de `text_hi` du titre,
pas fixe. Un titre dont le texte depasse `0x01000000` casse autrement, et la panne
se presente comme une erreur de link au lieu de dire pourquoi.

### R3 -- L'archive de donnees est du RPF6 -- mesure

`rdr2_layer0.rpf` (7,35 Go) commence par l'ASCII **`RPF6`** a l'offset 0. C'est le
format d'archive de Rockstar, pas un format PS3 : l'arbre RPCS3 ne contient aucun
lecteur RPF (`grep -rn 'RPF6' refs/rpcs3/` ne rend rien). Le runtime n'a donc rien a
en faire : le titre lit sa propre archive via son propre code fichier, comme pour
tout autre fichier. Extraire le membre de 7,35 Go avec 7z est inutile et assez lent
pour expirer.

A cote : `rdr2_installed.sdat` (539 Mo), des donnees installees que le titre
ecrit/relit, et deux cinematiques `.bik` (Bink).


## Red Dead Redemption -- where the port stands

### Proven

- The seven proven LLE modules plus libnet, libfiber, libssl, libhttp, librtc cover
  153 of 343 imports; 190 remain for HLE (docs/board.md, one ticket per module).
- No module overlaps a runtime region: modules occupy 0x30000000..0x3b004490, the
  RSX context at 0x40100000 and the video/stack/SPU regions are untouched.
- The allocator refuses image ranges, and each title has its own reloc tree.
- The title reaches its own engine: RAGE netThreadPool, DVD Reader and Streamer,
  HDD threads, LoadThread, FreeSpaceCheckThread, SPURS handlers, cellGame data
  checks and content permit, the 181 MB startup heap at 0x80000000.

### The blocker, measured

```
sys: 41 _sys_ppu_thread_exit lr=0x30004eac r3=0xdeadc0de
ppu: thread 0x100000e '[RAGE] LoadThread' exit 0xdeadc0de
```

Every RAGE worker thread is created through the shared entry OPD 0x01356420,
which is a table of {code, toc} pairs ABOVE the title's text_hi (0x10d9f74):

    00533064 0139ebb0   LoadThread's real entry
    005331e0 0139ebb0   the next one

call_opd() reads the pair correctly and lookup() finds f_00533064. The thread
runs, then liblv2 at 0x30004eac calls _sys_ppu_thread_exit with 0xdeadc0de --
the value a title passes when the work it wanted to do did not happen. So the
thread starts and gives up within a millisecond.

Consequence: the loader stops, the renderer receives nothing, and the frame
counter stays at zero. draw 0.0%, flip 0.0%.

### What changed and what it bought

| commit | effect |
|---|---|
| 6e921ef | lift covers the whole binary; interior seeds rejected |
| 584f96c | allocator and mmapper refuse image ranges; per-title reloc |
| 5bcbf49 | the five syscalls, now matching RPCS3 exactly |

At 584f96c the title reported:

    [FATAL] Cannot load archive 'gamedata:/rdr2_installed.sdat'

because the install never wrote the archive. Pre-placing it clears that and
moves the failure to the thread exit above. The remaining work is the 190 HLE
functions and this thread exit.

### Not done

No in-game capture. regress.sh is green on Minecraft (6841) and GTA (6481);
Red Dead Redemption reports alive with zero flips, which the script counts as a
failure and reports as one.


## 2026-11-11 -- the RAGE thread question, answered by measurement

The previous entry blamed `_sys_ppu_thread_exit(0xdeadc0de)` inside liblv2. That
was wrong twice over, and the correction matters.

First, `0xdeadc0de` is not a runtime sentinel. A search over runtime/, recomp/,
tools/, the generated tree, EBOOT.elf and liblv2.elf finds nothing: the word never
exists in a file. The title builds it with two instructions at 0x005331c0 --
`lis r3, 0xdead` then `oris r3, r3, 0xc0de` -- so it lives in a register and
nowhere else. That is why both a constant search and a memory search came up
empty, and it is a good reminder that "not found" is a fact about the search, not
about the program.

Second, the thread is not failing. `PS3BASE_THREAD_PROBE` (hooks.cpp) hooks the
worker entry at 0x533064 and prints its registers:

    threadprobe: ENTRY 0x533064 #0 th=0x1000005 r3=0x0165cc20 r2=0x0139ebb0
    ... eight entries, one exit through 0x5331e0

Eight workers arrive, each with its own work structure in r3 and the image TOC in
r2. The OPD at 0x01356420 resolves, lookup() finds f_00533064, and that function
contains its own _sys_ppu_thread_exit at 0x5331cc -- which is why only one thread
was ever seen leaving through the other trampoline. The machinery is correct.

## The install was never broken either

`GOWA_HDD0_DIR` defaults to `./vfs`, not to PS3BASE_DISC_DIR. The install had
been writing to `/root/Dev/ps3base2/vfs/dev_hdd0/game/BLES00680` all along and
succeeded:

    516M  vfs/dev_hdd0/game/BLES00680/USRDIR/rdr2_installed.sdat
          vfs/dev_hdd0/game/BLES00680/PARAM.SFO

The forcing in docs/forcings.md F-001 pre-placed the archive into a directory the
runtime never reads, so it documented a problem that did not exist. Removed.

## Where the title actually stops now

The main thread settles into a one-second idle loop:

    sys: 141 sys_timer_usleep lr=0x002a9180 r3=0xf4240     (1 s, forever)

after configuring video (`cellVideoOutConfigure res 2 format 1 pitch 5120`) and
mounting its cache. No dialog is open, no callback is pending, and the RSX reports
busy 100% with zero methods. The workers run and leave. Something the title waits
for never arrives, and it is not the loader: the loader is doing its job.

