# PISTE V — Verificateur

Base : /root/Dev/wasm-base @ d290fda. Worktree : /root/Dev/explore-V (branche explore/V).
Arbres partages lies (bin out out-mc third_party fw ref). Out dir : /tmp/out-V.

Livrable : scripts/regress-wasm.sh (verdict wasm, < 10 min a cache chaud),
scripts/regress-native-V.sh (le regress natif, serialise),
scripts/replay-fix.sh (rejoue un correctif sur un module issu de d290fda),
et le tableau "piste | annonce | rejoue | resultat" en section 5.

---

## 0. Antecedents (grep -Rn dans refs/ et docs/, AVANT toute hypothese)

```
$ grep -Rn 'cellGcmGetLabelAddress' /root/Dev/ps3base2/refs/rpcs3/rpcs3/Emu/Cell/Modules/cellGcmSys.cpp
/root/Dev/ps3base2/refs/rpcs3/rpcs3/Emu/Cell/Modules/cellGcmSys.cpp:159:u32 cellGcmGetLabelAddress(u8 index)
/root/Dev/ps3base2/refs/rpcs3/rpcs3/Emu/Cell/Modules/cellGcmSys.cpp:161:	cellGcmSys.trace("cellGcmGetLabelAddress(index=%d)", index);
/root/Dev/ps3base2/refs/rpcs3/rpcs3/Emu/Cell/Modules/cellGcmSys.cpp:1554:	REG_FUNC(cellGcmSys, cellGcmGetLabelAddress);

$ sed -n '159,163p' .../Cell/Modules/cellGcmSys.cpp
u32 cellGcmGetLabelAddress(u8 index)
{
	cellGcmSys.trace(...);
	return _cellGcmGetContext()->fw.label_addr + 0x10 * index;
}

$ grep -Rn 'label_addr' /root/Dev/ps3base2/refs/rpcs3/rpcs3/Emu/Cell/lv2/sys_rsx.cpp
.../sys_rsx.cpp:554:	ctx->fw.label_addr = vm::cast(*lpar_reports);
.../sys_rsx.cpp:1168:	vm::_ptr<atomic_t<u128>>(ctx->fw.label_addr + 0x10)->store(u128{});

$ sed -n '14,34p' .../Emu/RSX/NV47/HW/nv47_sync.hpp
template <bool FlushDMA, bool FlushPipe>
static void write_gcm_label(context* ctx, u32 type, u32 address, u32 data)
{
    atomic_fence_seq_cst();
    const bool is_flip_sema = (address == (ctx->fw.label_addr + 0x10) || address == (RSX(ctx)->device_addr + 0x30));
    if (!is_flip_sema)
    {
        ...
        if (vm::_ref<RsxSemaphore>(address) == data)
        {
            // It's a no-op to write the same value ...
            return;
        }
```

| Finding | Source (path:ligne) | S'applique ici ? |
|---|---|---|
| le label est label_addr + 0x10*index : une adresse MAPPEE, pas un offset | refs/rpcs3/.../cellGcmSys.cpp:159-163 | oui — c'est le point que la base ne fait pas (base : rend l'offset brut 0x1fe0) |
| label_addr vient de *lpar_reports (zone reports de context_allocate) | refs/rpcs3/.../sys_rsx.cpp:554 | oui — informe les correctifs des pistes B et wasm-main |
| le seul magasin de 16 octets est le semaphore de FLIP, et il ecrit 16 ZEROS | refs/rpcs3/.../sys_rsx.cpp:1168 | oui — la consigne "release = 16 octets, valeur puis zeros" est refutee (cf. piste B) |
| une release ecrit 4 octets (RsxSemaphore = be_t<u32>) et ne fait rien si la valeur est deja egale | refs/rpcs3/.../nv47_sync.hpp:14-34 | oui — la base ecrit aveuglement |
| rien dans RPCS3 ne reveille un label sans GPU : c'est le RSX thread qui ecrit | refs/rpcs3/.../nv47_sync.hpp (release) | oui — c'est pourquoi un stub sans GPU ne reveille jamais label[255] |
| le natif atteint le menu ; le wasm n'y arrive pas | docs/explore/I/RESULTS.md §4-§7 | oui — le natif est l'oracle, et il faut DISPLAY=:0 |
| le marqueur 'ui >= 2' de wasm_entry.cpp est faux (thread_options_load existe 42 s avant le menu) | docs/explore/I/RESULTS.md §10, message orchestrateur | oui — le verificateur le teste (section 3.4) |

---

## 1. Reference : ce que la base fait, mesure

Aucun script de verification wasm n'existait (scripts/ vide au depart ; tools/regress.sh ne connait
que le natif). Le premier travail a donc ete d'etablir la reference chiffree.

Sortie brute du run de reference retenu (/tmp/V-regress-final.log, 10:23:26) :

```
$ cd /root/Dev/explore-V && OUT=/tmp/out-V scripts/regress-wasm.sh

regress-wasm: root /root/Dev/explore-V
regress-wasm: mode relink, run budget 160s, objects /tmp/out-V

GREEN test hook_guard: HOOK-MECHANISM-OK
GREEN test command_ring: command_ring: 0 failures
GREEN test audio_ring: audio_ring: 0 failures
GREEN test spurs: spurs: 12 checks, 0 failures

GREEN build: relink of 200 objects in 33s
GREEN build: game.wasm 45405029 bytes
      /tmp/out-V/game.wasm: 49718 functions, largest 737751 bytes (no name section), total code 44812961 bytes

GREEN run: exit 124 (alive at the 160s budget), 825 log lines
      last sample: t=140s pc=0x002da5b4
GREEN run: pc 0x002da5b4, the usleep arm of f_002da36c's wait loop
GREEN run: main_thread blocked in 141 (usleep) with lr 0x2da598
      usleep 141 calls: first second 5729, peak cumulative 954755
GREEN run: usleep 141 is the traffic (peak 954755 calls in one report window)
      note: MAIN MENU REACHED absent (expected on d290fda)

regress-wasm: GREEN
exit=0
```

Point mesure, en clair : module lie (45 405 029 octets), titre vivant jusqu'au budget (exit 124),
pc bloque a 0x002da5b4 dans f_002da36c, main_thread endormi en syscall 141 avec lr 0x2da598,
~950 000 usleep cumules. C'est exactement l'etat decrit dans la consigne.

### 1.1 Deux mesures de la base qui ont du etre corrigees dans le script

Ces deux corrections sont des MESURES, pas des gouts :

**(a) le compteur d'usleep n'est pas cumulatif jusqu'au bout.** Le runtime remet a zero son
histogramme d'une seconde apres le late dump, ce qui fait chuter la valeur d'un coup :

```
$ grep -ao 'usleep141=[0-9]*' /tmp/regress-wasm-last.log | cut -d= -f2 \
    | awk 'NR>1 && $1<prev {print NR": reset "prev" -> "$1} {prev=$1}'
5: reset 22216 -> 5665
152: reset 852656 -> 5731
```

Lire la derniere ligne donnerait 5731 et un faux ROUGE. Le script prend le MAXIMUM de la serie
(852 656 pour ce run, 954 755 pour le run final).

**(b) le compte de la premiere seconde est sensible a la charge.** Mesure a 5 547 (consigne),
5 268, 5 380, 5 729, et **4 818** sous une charge de 58 sur 32 coeurs — ce dernier cas faisait
passer au ROUGE un run sain. Le dernier run de controle (10:41, /tmp/V-regress-committed.log) a
meme mesure **7** : un relink de 75 s sous charge a decale le premier rapport d'une seconde, donc
"la premiere seconde" n'est pas forcement une seconde de jeu. Le critere retenu est le pic
cumulatif (plancher 100 000), a un ordre de grandeur du regime "sonde active" (672 233 a 954 755
selon la charge), et jamais le debut de run.

---

## 2. Le script livre

    /root/Dev/explore-V/scripts/regress-wasm.sh

Trois etages, chacun juge mecaniquement (code de sortie et compteur, jamais un grep de mot) :

| etage | critere | mesure de reference |
|---|---|---|
| 1 tests | spurs (12 checks), audio_ring, command_ring, hook_guard : exit 0 | 4/4 verts, ~5 s |
| 2 build | les 200 objets se relient ; game.wasm 45 405 029 octets +/-5% | relink 33-96 s selon charge |
| 3 run | exit 124 ; pc 0x002da5b4 ; "blocked in 141 ... lr 0x2da598" ; pic usleep >= 100 000 | 160 s |

Duree totale mesuree : **74,67 s** a cache chaud sur un run a budget court
(`/usr/bin/time` : `WALL 74.67 s`, tests + relink + 25 s de run). A budget nominal 160 s le mur
est d'environ 3 min 40, sous la barre des dix minutes.

Options : --fast (pas de relink), --build (rebuild complet ~40 min, hors budget), --list. Le
script cherche lui-meme un cache d'objets ($OUT/obj, sinon le plus recent /tmp/out-*/obj avec
>= 100 objets) pour qu'un verificateur d'une autre piste n'ait pas a connaitre son emplacement.

### 2.1 Contre-epreuve : le script doit savoir dire ROUGE

Un verificateur qui ne sort jamais 1 ne verifie rien. Run negatif a budget de 3 s :

```
$ OUT=/tmp/out-V RUN_SECONDS=3 scripts/regress-wasm.sh --fast
GREEN test hook_guard: HOOK-MECHANISM-OK
GREEN test command_ring: command_ring: 0 failures
GREEN test audio_ring: audio_ring: 0 failures
GREEN test spurs: spurs: 12 checks, 0 failures
GREEN build: relink of 200 objects in 34s
GREEN build: game.wasm 45405029 bytes
GREEN run: exit 124 (alive at the 3s budget), 210 log lines
      last sample: t=1s pc=0x002da5b4
GREEN run: pc 0x002da5b4, the usleep arm of f_002da36c's wait loop
RED   run: no 'blocked in 141 ... lr 0x2da598' line
      usleep 141 calls: first second 5397, peak cumulative 5397
RED   run: only 5397 usleep 141 calls at peak (floor 100000)
      note: MAIN MENU REACHED absent (expected on d290fda)
regress-wasm: RED
exit=1
```

A budget de 25 s il sort vert (pic 105 047, au-dessus du plancher) : le plancher distingue
"sonde installee" de "boot qui n'a pas atteint la boucle", il ne compte pas les secondes.

---

### 2.2 Ce que le verificateur prouve, et ce qu'il ne prouve pas

Applique au module du correctif B (OUT=/tmp/replay-B scripts/regress-wasm.sh --fast) :

    $ cd /root/Dev/explore-V && OUT=/tmp/replay-B scripts/regress-wasm.sh --fast
    GREEN test hook_guard: HOOK-MECHANISM-OK
    GREEN test command_ring: command_ring: 0 failures
    GREEN test audio_ring: audio_ring: 0 failures
    GREEN test spurs: spurs: 12 checks, 0 failures

    GREEN build: relink of 201 objects in 51s
    GREEN build: game.wasm 45407181 bytes
          /tmp/replay-B/game.wasm: 49725 functions, largest 737751 bytes (no name section), total code 44814760 bytes

    GREEN run: exit 124 (alive at the 160s budget), 909 log lines
          last sample: t=156s pc=0x002da5b4
    GREEN run: pc 0x002da5b4, the usleep arm of f_002da36c's wait loop
    GREEN run: main_thread blocked in 141 (usleep) with lr 0x2da598
          usleep 141 calls: first second 5398, peak cumulative 845526
    GREEN run: usleep 141 is the traffic (peak 845526 calls in one report window)
          note: MAIN MENU REACHED absent (expected on d290fda)

    regress-wasm: GREEN
    exit=0

VERT signifie ici **"le point mesure tient"**, pas **"le correctif a marche"**. C'est la bonne
semantique pour un garde de non-regression, et il faut le dire clairement :

- un module qui RESTE bloque au meme pc sort vert (c'est le cas des deux correctifs rejoues) ;
- un module qui FRANCHIT la boucle et atteint le menu sortirait aussi vert, mais le critere
  "pc 0x002da5b4" devra etre remplace a ce moment-la, sinon il sera rouge a tort ;
- un module qui casse (RuntimeError, exit 1, boucle disparue trop tot) sort rouge.

Le script repond a "la base est-elle encore la base ?", et c'est ce qu'une piste doit verifier
avant d'annoncer un correctif : le correctif ne doit pas casser le point mesure, et s'il le
deplace il doit le dire avec ses propres chiffres.

---

## 3. Le regress NATIF, et le piege qui l'a rendu faux deux fois

### 3.1 Le lancement correct (percee I) est confirme

Les trois binaires natifs demarrent et presentent, en cwd=/root/Dev/ps3base2 avec
DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 :

```
$ cd /root/Dev/ps3base2 && timeout 25 env DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 \
    PS3BASE_DISC_DIR=/root/Dev/mc-port/disc PS3BASE_LOG=info ./build/ps3base --no-launcher
exit=124, 0 ligne '^E ', rsx: flip 661 (buffer 0) ... flip 721

$ cd /root/Dev/ps3base2 && timeout 60 env ... ./build-gta/ps3base --no-launcher
exit=124, 0 ligne '^E ', rsx: flip 2161 ... flip 2221

$ cd /root/Dev/ps3base2 && timeout 100 env ... PS3BASE_DISC_DIR=/root/Dev/rdr-port/vfs/dev_bdvd \
    ./build-rdr/ps3base --no-launcher
exit=124, 0 ligne '^E ', vfs: disc ... (BLES00680 01.00), rsx: flip 1321 ... 1381
```

### 3.2 tools/regress.sh tue les runs des AUTRES agents (mesure, deux fois)

Premiere execution :

```
$ cd /root/Dev/ps3base2 && DISPLAY=:0 SECONDS_PER_GAME=120 tools/regress.sh
regress: 120s per game on display :0
Killed
minecraft    FAIL  exit 137        flips=3841   crashes=0
Killed
gta-sa       FAIL  exit 137        flips=2281   crashes=0
rdr          PASS  alive           flips=3241   crashes=0 screen=socialclub@16s abort=none
regress: 2 game(s) REGRESSED
```

Cause, lue dans le script lui-meme :

```
$ sed -n '144,151p' /root/Dev/ps3base2/tools/regress.sh
for p in /proc/[0-9]*; do
    c=$(tr "\0" " " < "$p/cmdline" 2>/dev/null) || continue
    case "$c" in
        *ps3base*--no-launcher*)
            [ "$(cat "$p/comm" 2>/dev/null)" != "tmux: server" ] && kill -9 "${p#/proc/}" 2>/dev/null
            ;;
```

Avant chaque titre, regress.sh tue **tout** processus dont la ligne de commande contient
`ps3base --no-launcher`, en supposant que c'est le residu du titre precedent. Il ne distingue pas
un residu d'un run mesure par un autre agent : deux invocations concurrentes s'entretuent, et la
victime meurt en Killed/exit 137 (SIGKILL) au milieu de son run. C'est ce qui s'est produit :
deux autres agents lancaient regress.sh a 10:01 et 10:07. Le meme run, sans concurrent, donne un
resultat different (3.3).

### 3.3 Solution : serialiser, et ne jamais juger un run tue

    /root/Dev/explore-V/scripts/regress-native-V.sh

Il attend qu'aucun autre tools/regress.sh ne tourne, lance une tentative, et REJOUE si la sortie
contient Killed (le run n'a pas ete juge, il a ete assassine). Resultat :

```
$ /root/Dev/explore-V/scripts/regress-native-V.sh 4 125
attempt 1 (waited 90s for a competing regress.sh)
regress: 125s per game on display :0
minecraft    (load 23.93 before the run)
minecraft    PASS  alive           flips=6781   crashes=0
gta-sa       (load 34.45 before the run)
gta-sa       PASS  alive           flips=6361   crashes=0
rdr          (load 22.83 before the run)
rdr          PASS  SPURS abort     flips=3181   crashes=0 screen=socialclub@17s abort=at 115s

regress: all present games passed (0 skipped)
attempt 1: clean (exit 0)
exit=0
```

**Trois titres VERTS.** Seuils de docs/regress-baseline.md tenus : minecraft 6781 >= 6500,
gta-sa 6361 >= 6300, rdr 3181 >= 2000 avec l'ecran Social Club lu a 17 s et un abort SPURS a
115 s (apres l'echeance de 60 s, donc tolere par le critere).

### 3.4 Le marqueur de menu du wasm est FAUX, et le verificateur le teste

runtime/src/wasm_entry.cpp:650-657 declare le menu atteint si `g_menu_thread_creations > 0 &&
g_ui_thread_creations >= 2`, et compte thread_options_load parmi les threads d'interface. Or le
natif cree thread_options_load a t=12,16 s, soit 42 s avant le menu (docs/explore/I/RESULTS.md
§10). Les deux threads que le natif cree a l'ouverture du menu sont thread_readSaveCacheFile et
thread_getSavesInfo, a t=54,97 s.

Le script verifie donc un eventuel "MAIN MENU REACHED" contre ces deux threads : vert seulement
si les deux sont presents dans le log, sinon ROUGE "false marker". Sur la base, le log dit la
verite et ne declenche rien :

```
$ grep -a 'main menu' /tmp/regress-wasm-last.log
ps3base-wasm: main menu NOT reached -- menu 0, ui 0
```

Les trois logs (base, replay-V, replay-B) donnent tous `menu 0, ui 0` : aucune piste n'a jamais
produit ce marqueur, donc le controle est preventif, pas correctif.

---

## 4. Rejeu des correctifs annonces, sur un module issu de d290fda

### 4.1 Methode (scripts/replay-fix.sh)

Aucun second repertoire de build n'est cree. Les 200 objets de d290fda servent de cache, lies
(et non copies) dans /tmp/replay-<X>/obj ; seuls les .cpp que le worktree du correctif modifie
sont recompiles, puis on relie et on lance. Les fichiers NON SUIVIS sont inclus dans la liste
(git ls-files --others) : la piste B ajoute runtime/src/gpu/rsx_sync.cpp sans diff a ce chemin,
et l'oublier aurait "rejoue" un module qui ne contient pas son correctif.

```
$ cd /root/Dev/explore-V && BASE_OBJ=/tmp/out-V/obj scripts/replay-fix.sh <worktree> <out> 160
```

### 4.2 Correctif "label" (branche wasm-main, 6c0d91c..52a15dc ; meme contenu dans /root/Dev/replay-Vjs)

Arbre de rejeu verifie identique au correctif annonce :

```
$ git -C /root/Dev/ps3base2-wasm diff d290fda -- runtime > /tmp/ref-patch.diff
$ git -C /root/Dev/replay-Vjs     diff d290fda -- runtime > /tmp/replay-patch.diff
$ diff -q /tmp/ref-patch.diff /tmp/replay-patch.diff && echo IDENTICAL
IDENTICAL

 runtime/include/runtime/rsx_iomap.h |  4 ++++
 runtime/src/hle/cellGcmSys.cpp      | 34 ++++++++++++++++++++++++++++++++++
 runtime/src/lv2/rsx.cpp             | 25 +++++++++++++++++++++++++
 runtime/src/ppu_runtime.cpp         | 10 ++++++++++
 runtime/src/spu/thread.cpp          | 17 ++++++++++++++++-
 5 files changed, 89 insertions(+), 1 deletion(-)
```

Rejeu :

```
$ BASE_OBJ=/tmp/out-V/obj scripts/replay-fix.sh /root/Dev/replay-Vjs /tmp/replay-V 160
replay-fix: 0 objects referenced from the base cache
replay-fix: recompiling 4 changed translation units:
      runtime/src/hle/cellGcmSys.cpp
      runtime/src/lv2/rsx.cpp
      runtime/src/ppu_runtime.cpp
      runtime/src/spu/thread.cpp
replay-fix: linked in 51s, 45405609 bytes
replay-fix: last sample t=158s pc=0x002da5b4
replay-fix: exit 124, 909 log lines
replay-fix: usleep 141 cumulative: 42521
replay-fix: MAIN MENU REACHED: 0 line(s)
replay-fix: label probe lines: 0
```

*Note de methode* : ``0 objects referenced from the base cache'' est un defaut du premier jet du
script (il comparait $OUT/obj/$b a lui-meme lorsque OUT etait le cache) ; le cache a bien ete
utilise — 200 objets presents dans /tmp/replay-V/obj, dont 196 liens vers /tmp/out-V, verifie par
llvm-nm (runtime_src_lv2_rsx.o porte _ZN3lv217gcm_label_addressEj, absent de la base). Le script a
ete corrige pour compter sans ambiguite ; la mesure, elle, est valide.

**Le correctif ne change PAS le point mesure.** La boucle usleep est toujours la, pc identique,
menu toujours absent, et les sondes que le correctif ajoute ([labelret], [label138]) n'impriment
RIEN : l'import 138 n'est pas atteint dans le run wasm. L'auteur l'avait lui-meme mesure
(docs/webassembly/LABEL-COMPTEUR-ET-PROBE-MUET.md : "label138=0, labelret=0").

### 4.3 Correctif "synchronisation RSX" (piste B)

```
$ cd /root/Dev/explore-B && g++ -std=c++20 -O1 -Iruntime/include tests/rsx_sync.cpp \
    runtime/src/gpu/rsx_sync.cpp -o /tmp/V-rsx_sync_test && /tmp/V-rsx_sync_test
rsx_sync: 56 checks, 0 failures
exit=0
```

Rejeu du module :

```
$ BASE_OBJ=/tmp/out-V/obj scripts/replay-fix.sh /root/Dev/explore-B /tmp/replay-B 160
replay-fix: 200 objects referenced from the base cache
replay-fix: recompiling 2 changed translation units:
      runtime/src/gpu/rsx_sync.cpp
      runtime/src/lv2/rsx.cpp
replay-fix: linked in 46s, 45407181 bytes
replay-fix: last sample t=158s pc=0x002da5b4
replay-fix: exit 124, 907 log lines
replay-fix: usleep 141 cumulative: 42296
replay-fix: MAIN MENU REACHED: 0 line(s)
```

**Le correctif ne deplace pas le point mesure** : meme pc, meme boucle, meme absence de menu.
Coherent avec ce que B ecrit lui-meme en §6 ("le wasm complet n'a pas ete reconstruit ; la
verification a porte sur les tests unitaires, pas sur une execution du titre"). Le test verrouille
la semantique RPCS3, il ne reveille pas label[255].

### 4.4 Trois modules, memes chiffres

```
== /tmp/regress-wasm-last.log (base d290fda)
  lines: 908 | last t=158s | pc 0x002da5b4 | peak usleep141 891954 | menu 0 | RuntimeError 0 | syscalls 958940
== /tmp/replay-V/run.log (correctif label)
  lines: 909 | last t=158s | pc 0x002da5b4 | peak usleep141 902608 | menu 0 | RuntimeError 0 | syscalls 968443
== /tmp/replay-B/run.log (correctif rsx_sync)
  lines: 907 | last t=158s | pc 0x002da5b4 | peak usleep141 896129 | menu 0 | RuntimeError 0 | syscalls 961918
```

Aucun des deux correctifs n'ameliore ni ne degrade le point mesure. Les ecarts (891k/902k/896k)
sont du bruit de charge, pas un signal — c'est pourquoi le script juge un plancher a 100 000 et
pas une egalite.

### 4.5 Correctifs NON rejoues, et pourquoi

| correctif | pourquoi non rejoue |
|---|---|
| piste S1 (lifter : "01f9378 a supprime le critere base == bctr+4") | le rejeu exige de REGENERER out/ppu (1 Go, arbre partage lie) : exactement ce que les regles disque interdisent sans copie et annonce. S1 est mesure cote natif (trois titres verts), et le wasm utilise le meme out/ppu pre-lifte. |
| piste E (trace native : host.cpp/trace.cpp) | instrumentation, pas un correctif de comportement ; aucun effet attendu sur le point mesure, et la rejouer exigerait un build natif complet. |
| piste I (modele de lancement natif) | ce n'est pas du code : c'est la procedure de lancement. Rejouee directement en 3.1 et 3.3 (trois titres verts). |
| pistes D, K, H, C, G, F, J | aucune n'annoncait de correctif au moment du rejeu (RESULTS.md sans section de resultat) ; "non rejoue" faute d'annonce, pas faute de temps. |

---

## 5. Tableau livre

| piste | annonce | rejoue | resultat |
|---|---|---|---|
| base d290fda | reference | oui (regress-wasm.sh, 3 runs + 2 contre-epreuves) | **VERT** : 4/4 tests, module 45 405 029 o, exit 124, pc 0x002da5b4, lr 0x2da598, pic usleep 891 954-954 755 |
| wasm-main 6c0d91c..52a15dc (= /root/Dev/replay-Vjs) | "cellGcmGetLabelAddress rend une adresse mappee ; le label reflete le compteur du jeu" | oui, module rejoue sur base propre | **SANS EFFET** : pc 0x002da5b4, exit 124, menu 0, sondes label muettes (import 138 jamais atteint) |
| B (rsx_sync : release/acquire/flip selon RPCS3) | "56 assertions, semantique RPCS3 des releases" | oui : test unitaire + module rejoue | test **VERT** (56/0) ; module **SANS EFFET** (pc 0x002da5b4, exit 124, menu 0) |
| I (modele de lancement natif) | "l'exit 124 est un probleme de DISPLAY, pas un blocage" | oui, sur les trois titres | **CONFIRME** : minecraft flip 721 en 25 s, gta-sa 2221 en 60 s, rdr 1381 en 100 s, 0 ligne '^E ' |
| natif tools/regress.sh sur les 3 titres | — | oui, serialise (regress-native-V.sh) | **3 VERTS** : minecraft 6781, gta-sa 6361, rdr 3181 + Social Club a 17 s |
| consigne orchestrateur "release = 16 octets, valeur puis zeros" | "RPCS3 ecrit 16 octets sur les releases" | oui, source RPCS3 relue | **FAUX** : write_gcm_label ecrit 4 octets (nv47_sync.hpp:62) ; les 16 octets sont le flip et sont 16 ZEROS (sys_rsx.cpp:1168) |
| consigne orchestrateur "ui >= 2 prouve le menu" | "le marqueur ui >= 2 corrobore le menu" | oui, threads natifs releves | **FAUX** : thread_options_load a t=12,16 s, 42 s avant le menu ; les vrais corroborateurs sont thread_readSaveCacheFile + thread_getSavesInfo a t=54,97 s. Controle ajoute au script. |
| S1 (lifter) | "01f9378 supprime base == bctr+4 ; correctif 4c48bc6" | **non** | faute de place disque pour regenerer out/ppu (1 Go, arbre partage) — cf. 4.5 |
| D, K, H, C, G, F, J | aucune annonce de correctif | **non** | aucune annonce a rejouer au moment de la mesure |

---

## 6. Ce que ce travail etablit, et ce qu'il n'etablit pas

Etabli par la mesure :

1. La base d290fda est **verte** sur les quatre tests et sur le point mesure (152 s / exit 124 /
   boucle usleep), et le verificateur le prouve en 74 s a 3 min 40 selon le budget de run.
2. Le verificateur **sait dire ROUGE** (deux contre-epreuves : budget de 3 s ; absence du
   marqueur lr).
3. Le natif est **sain sur les trois titres** quand on le lance correctement (DISPLAY=:0) et
   qu'on ne le fait pas assassiner par un regress.sh concurrent. Le "2 jeux sur 3 rouges"
   d'une premiere passe etait un artefact de concurrence, reproduit et explique.
4. **Aucun** des deux correctifs annonces cote wasm (label HLE, rsx_sync) ne deplace le point
   mesure : le wasm s'arrete au meme pc, dans la meme boucle, sans menu.

Non etabli :

- La cause du blocage wasm. La boucle attend label[255] et **l'import 138 n'est jamais atteint**
  (aucune ligne de sonde dans les trois modules, y compris celui qui instrumente l'import et le
  handler). Le desaccord entre "la consigne dit call_import(138)" et "les sondes de l'import ne
  tirent pas" n'est **pas tranche** ici : il faudrait instrumenter le site d'appel dans le code
  PPU lifte (f_002da36c) et pas l'import.
- Le rejeu de S1 cote wasm (place disque pour regenerer out/ppu).

Les deux corrections de script (1.1) sont des mesures, pas des choix, et sont citees avec leur
sortie brute.

---

## 7. Fichiers et artefacts

| chemin | contenu |
|---|---|
| scripts/regress-wasm.sh | le verificateur wasm (0 vert / 1 rouge / 2 environnement) |
| scripts/regress-native-V.sh | le regress natif, serialise, rejoue sur "Killed" |
| scripts/replay-fix.sh | rejeu d'un correctif sur un module issu de d290fda, sans second build |
| /tmp/V-regress-final.log | run vert de reference (10:23:26) |
| /tmp/V-regress-neg2.log | contre-epreuve ROUGE (budget 3 s) |
| /tmp/V-native-regress3.log | regress natif vert, trois titres |
| /tmp/V-native-regress.log, /tmp/V-native-regress2.log | les deux passes faussees par la concurrence (artefact reproduit) |
| /tmp/V-replay1.log + /tmp/replay-V/run.log | rejeu du correctif label |
| /tmp/V-replay2.log + /tmp/replay-B/run.log | rejeu du correctif rsx_sync |
| /tmp/out-V | cache d'objets + module de reference (137 Mo) |
