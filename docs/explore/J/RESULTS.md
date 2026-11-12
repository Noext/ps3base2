# Piste J — Taille et chargement du module wasm

Status: termine (mesures faites, recommandation en fin de document).

Base: /root/Dev/wasm-base @ d290fda, worktree /root/Dev/explore-J, branche explore/J.
Chromium: Chrome for Testing 156.0.8078.4 (/root/.cache/ms-playwright/chromium-1248/chrome-linux64/chrome),
pilote playwright. Serveur de mesure: /tmp/J-measure/serve.py (COOP/COEP, Content-Type application/wasm),
variante compressee /tmp/J-measure/serve_gz.py. Toutes les mesures sont horodatees par performance.now()
DANS LA PAGE, pas autour du lanceur.

Contrainte de la piste: le jeu doit toujours atteindre le meme point (boucle usleep). Verifiee en M9.

---

## 0. Resume — tableau avant / apres

| Mesure | Avant (module livre) | Apres (correctif EH) | Gain |
|---|---|---|---|
| Taille du .wasm | 45 405 029 o | 45 402 087 o | **-2 942 o** (-0,006 %) |
| Fonctions | 49 718 | 49 728 | +10 |
| Plus grosse fonction | 737 751 o | 737 751 o | 0 |
| Section CODE | 44 812 961 o | 44 809 980 o | -2 981 o |
| **Compile dans Chromium ?** | **NON** (CompileError) | **OUI** | **bloquant leve** |
| Compilation V8 (mediane) | impossible | **39,1 ms** | n/a |
| Compilation V8 (pire) | impossible | 67,2 ms | n/a |
| Temps jusqu'a premiere activite guest | impossible | **1,074 s** | n/a |
| Idem, module+data servis en gzip | impossible | 1,99 s (local) | +0,9 s en local, voir M6 |
| Fetch .wasm (identite, local) | 44,4 ms | 44,4 ms | 0 |
| Fetch .data 126 Mo (identite, local) | 100,0 ms | 100,0 ms | 0 |

Le resultat qui compte n'est pas le gain de taille: c'est que **le module livre ne se charge
pas du tout dans Chromium**. Le correctif tient a deux noms d'archives a l'edition de liens et
ne coute pas un octet de plus.

---

## 1. Antecedents (regle du workspace, refs/ d'abord)

| Finding | Source (path:line) | Applies here? |
|---|---|---|
| La page se sert avec COOP/COEP, sinon pas de SharedArrayBuffer | web/serve.py:24-40 (ce depot) | oui — repris tel quel, avec Content-Type explicite |
| Le melange legacy/neuf est deja documente comme cause de refus Chromium | tools/build_wasm_exnref.py (untracked, ce worktree) | oui — meme cause, mesuree independamment ici |
| Le loader de ce build n'est PAS MODULARIZE, il pose un global Module | /tmp/out-K/game.js ligne 1 | oui — le premier harnais a echoue la-dessus (factory is not a function) |
| Aucun anterieur sur le decoupage/streaming de CE module | grep -Rn dans refs/ sans resultat utile | non |

---

## M1 — Etat de base, remesure

~~~
$ stat -c '%s %n' /tmp/out-J/game.wasm
45405029 /tmp/out-J/game.wasm
$ python3 tools/wasm_sizes.py /tmp/out-J/game.wasm
/tmp/out-J/game.wasm: 49718 functions, largest 737751 bytes (no name section), total code 44812961 bytes
$ $EMSDK/bin/llvm-objdump -h /tmp/out-J/game.wasm
Idx Name          Size      VMA       Type
  0 TYPE          00001a1c  0000000b
  1 IMPORT        00000110  00001a2a
  2 FUNCTION      0000c37b  00001b3e
  3 TABLE         00000009  0000debb
  4 TAG           00000003  0000dec6
  5 GLOBAL        00000037  0000decb
  6 EXPORT        00000072  0000df04
  7 START         00000003  0000df78
  8 ELEM          0001fdf5  0000df7f
  9 DATACOUNT     00000001  0002dd76
 10 CODE          02acd340  0002dd7c  TEXT
 11 DATA          000522a5  02afb0c0  DATA
~~~

98,7 % du module est la section CODE (44 812 961 / 45 405 029). DATA 0,32 Mo.
Pas de section name (build sans -g), aucune section custom.

---

## M2 — LE MODULE LIVRE NE COMPILE PAS DANS CHROMIUM

Commande: node drive.js 'http://127.0.0.1:8811/index.html?wasm=gameJ.wasm&mode=arraybuffer&reps=3'

~~~
0.3    start                mode=arraybuffer wasm=gameJ.wasm coi=true hc=32
80.8   rep0_fetch_ms        79.8   (bytes=45405029)
116.0  rep0_compile_FAIL    WebAssembly.compile(): Compiling function #87 failed:
                            module uses a mix of legacy and new exception handling
                            instructions @+199574
~~~

En streaming, meme refus, autre indice de fonction:

~~~
155.4  rep0_compileStreaming_FAIL  ct=application/wasm len=45405029 enc=identity |
       WebAssembly.compileStreaming(): Compiling function #109 failed: module uses a
       mix of legacy and new exception handling instructions @+200645
~~~

Sous node 24.14.0 le message est plus direct encore:

~~~
$ node -e "WebAssembly.compile(fs.readFileSync('/tmp/out-J/game.wasm'))"
FAIL WebAssembly.compile(): Compiling function #87 failed: Invalid opcode 0x1f
     (enable with --experimental-wasm-exnref) @+199574
~~~

Et dans la page de boot reelle, l'abandon est explicite:

~~~
0.2    page_start   label=J coi=true
253.2  ABORT        CompileError: WebAssembly.instantiate(): Compiling function #236 failed:
                    module uses a mix of legacy and new exception handling instructions @+212652
PAGEERROR: Aborted(...). Build with -sASSERTIONS for more info.
~~~

Conclusion M2: sur cette base le jeu ne demarre pas dans Chromium. Aucune mesure de
temps avant premiere frame n'a de sens sur le module livre.

### Cause, mesuree

tools/build_wasm.sh compile avec -fwasm-exceptions — clang emet alors la proposition
LEGACY (la ligne de liaison reelle contient -mllvm -wasm-use-legacy-eh) — mais lie
libc++-mt-wasmexcept.a / libc++abi-mt-wasmexcept.a, baties avec la proposition NEUVE.
Le module contient les deux jeux d'instructions et V8 refuse.

~~~
$ ls .../cache/sysroot/lib/wasm32-emscripten/ | grep 'mt.*except'
libc++-debug-mt-noexcept.a
libc++-mt-legacyexcept.a        <-- la bonne pour -fwasm-exceptions seul
libc++-mt-noexcept.a
libc++-mt-wasmexcept.a          <-- celle que le script lie (mauvais accord)
~~~

### Correctif, mesure

Re-liaison des MEMES objets (/tmp/out-A/obj, deja compiles) contre les archives legacyexcept:

~~~
$ emcc ... -o /tmp/out-Jleg/game.js /tmp/out-A/obj/*.o \
      $LIB/libc++-mt-legacyexcept.a $LIB/libc++abi-mt-legacyexcept.a
45402087 /tmp/out-Jleg/game.wasm
/tmp/out-Jleg/game.wasm: 49728 functions, largest 737751 bytes, total code 44809980 bytes

$ md5sum /tmp/out-Jleg/game.wasm /tmp/out-K/game.wasm
c66469405abf7ff0d35fc79b3643f530  /tmp/out-Jleg/game.wasm
c66469405abf7ff0d35fc79b3643f530  /tmp/out-K/game.wasm

$ node -e "WebAssembly.compile(...)"
Jleg OK
~~~

Le module corrige est PLUS PETIT de 2 942 octets et compile sans option speciale.
Aucune recompilation: seuls les deux noms d'archives changent a l'edition de liens.
-sWASM_LEGACY_EXCEPTIONS=0 sur tous les -c est l'autre voie (celle de
tools/build_wasm_exnref.py, mesuree en /tmp/out-Jx: 45 415 256 o, 49 673 fonctions); elle
exige une recompilation complete des 200 unites et donne un module un peu plus GROS.

---

## M3 — Compilation V8, module corrige

3 executions independantes, 3 repetitions chacune, arraybuffer, cache HTTP desactive:

~~~
execution 1:  37.3  42.1  41.7 ms
execution 2:  43.7  29.5  39.1 ms
execution 3:  33.8  34.1  62.3 ms
~~~

9 mesures: min 29,5 / mediane 39,1 / max 67,2 ms pour 45 402 087 octets.

En streaming, compileStreaming sur le meme module:

~~~
111.5  rep0_compileStreaming_ms  110.7   (ct=application/wasm enc=identity)
~~~

Ce nombre n'est PAS un temps de compilation: il inclut le telechargement et le decodage.
Le chiffre comparable est celui d'arraybuffer, ~40 ms.

Sous node 24.14.0 --experimental-wasm-exnref, sur le module de base: 86,8 / 115,4 / 110,7 ms.
V8 de node est donc 2 a 3 fois plus lent que celui de Chromium 156 sur ce module; ne pas
melanger les deux chiffres dans une meme conclusion.

---

## M4 — Workers: la parallelisation n'aide pas

Chaque worker telecharge et compile le module entier, en parallele; on mesure le temps
mural du lot (/tmp/J-measure/parallel.html + workerc.js).

~~~
workers=1   main_compile 37.3 / 27.7 ms    parallel_wall 101.0 ms
workers=2   main_compile 34.1 / 42.1 ms    parallel_wall 130.1 ms
workers=4   main_compile 41.4 / 33.4 ms    parallel_wall 246.7 ms
~~~

Le temps mural CROIT avec le nombre de workers: 101 -> 130 -> 247 ms. V8 compile deja le
module en interne sur plusieurs threads; ajouter des workers multiplie le travail et la
pression memoire sans rien gagner.

Point d'API plus fondamental: WebAssembly.compile prend le module ENTIER. Il n'existe aucun
moyen de compiler un meme module en tranches dans plusieurs workers et de recoller les
morceaux. La parallelisation n'est donc pas un levier disponible; seul le decoupage en
modules separes le serait (M7).

Utile malgre tout: compiler dans un worker libere le thread principal. Ce n'est pas un gain
de temps de compilation, c'est un gain de reactivite de la page.

---

## M5 — Decomposition du chargement (les 1,07 s)

Page lp.html: elle telecharge d'abord les deux gros objets pour les isoler, puis laisse le
loader faire son travail. Horodatage performance.now() dans la page.

~~~
0.1     page_start
45.0    fetch_game.wasm    ttfb=5.4    download=39.0    total=44.4 ms   bytes=45402087  enc=identity
145.2   fetch_game.data    ttfb=1.7    download=98.3    total=100.0 ms  bytes=126426695 enc=identity
409.1   onRuntimeInitialized
1179.6  RUNTIME_UP         WASM-RUNTIME-UP
1182.0  GUEST_CODE_RAN     WASM-GUEST-CODE-RAN
1201.0  IMAGES_LOADED      WASM-BOOT-IMAGES-LOADED
1205.5  BOOT_STARTED       WASM-BOOT-STARTED
1212.1  PROVIDER_DRIVES    WASM-FILE-PROVIDER-DRIVES-LOADER
1212.3  WAITING_GUEST      "ps3base-wasm: waiting for guest activity..."
~~~

Run de confirmation sur le module corrige seul (/tmp/J-boot-K/r.json), pour le chiffre
premiere activite guest: RUNTIME_UP 1037,5 / GUEST_CODE_RAN 1040,1 / IMAGES_LOADED 1063,0 /
BOOT_STARTED 1067,6 / PROVIDER_DRIVES 1074,3 / WAITING_GUEST 1074,5 ms — soit 1,074 s.

Lecture: la compilation V8 (~40 ms) est 3 a 4 % du temps de chargement. Le reste est
l'initialisation du runtime (memoire invitee 2304 Mo en bitset, chargement des images) et le
transfert des fichiers. Optimiser la compilation ne deplace donc presque rien.

---

## M6 — Compression: ce qui coute est le transfert

~~~
$ stat -c '%s %n' /tmp/out-K/game.wasm /tmp/out-K/game.data
45402087    /tmp/out-K/game.wasm
126426695   /tmp/out-K/game.data
$ gzip -6 -c game.wasm | wc -c    ->   7 933 960   (rapport 5,72:1)
$ gzip -6 -c game.data | wc -c    ->  96 811 425   (rapport 1,31:1)
~~~

Avec Content-Encoding: gzip (serveur local, cache de compression deja chaud):

~~~
0.1     page_start
65.7    fetch_game.wasm    ttfb=5.6    download=59.2    total=64.8 ms    bytes=45402087 len=7937801  enc=gzip
424.3   fetch_game.data    ttfb=51.6   download=306.9   total=358.5 ms   bytes=126426695 len=96930721 enc=gzip
996.7   onRuntimeInitialized
1954.7  RUNTIME_UP
1990.9  WAITING_GUEST
~~~

Attention a la lecture: en local gzip RALENTIT le demarrage (1,99 s contre 1,21 s) car le
decompresser coute plus que le transfert economise sur un loopback. Ce chiffre ne se transpose
pas a un vrai reseau. Ce qui se transpose est le VOLUME: 45,4 Mo -> 7,9 Mo pour le module
(5,72:1) et 126,4 Mo -> 96,8 Mo pour les donnees (1,31:1). game.data ne se compresse presque
pas (archive deja dense): c'est lui le cout de transfert, 126 Mo, soit 2,8 fois le module.

AUCUNE mesure sur un vrai reseau n'a ete faite (pas de lien a debit limite disponible ici):
les rapports de compression sont mesures, les temps sur reseau ne le sont pas.

---

## M7 — Le decoupage: ou est la masse, et pourquoi il ne se fait pas a la liaison

Tailles de code par famille d'objets (llvm-objdump -h sur chaque objet, section CODE):

~~~
ppu_*.o   (code PPU leve)        100 objets   52 653 812 o
runtime_src_*.o (le runtime)     101 objets      975 706 o
soit 98,2 % du code = PPU leve
~~~

Les plus gros objets sont tous des tranches du lift:

~~~
1158834  ppu_mc_071.o
 720043  ppu_mc_084.o
 697539  ppu_mc_018.o
 691647  ppu_mc_086.o
 686665  ppu_mc_078.o
~~~

Le decoupage runtime d'abord, PPU ensuite est donc theoriquement le bon axe: 1 Mo pour
demarrer le runtime, puis 52 Mo de PPU. Mais il ne se realise pas a l'edition de liens. La
tentative de lier uniquement les objets runtime a echoue:

~~~
$ emcc ... -o /tmp/out-Jrt/game.js $(ls /tmp/out-A/obj/*.o | grep -v '/ppu_')
failed (returned 1)
stat: cannot statx '/tmp/out-Jrt/game.wasm': No such file or directory
~~~

parce que le runtime reference la table du lift (out/ppu/table.cpp), qui porte les pointeurs
de toutes les fonctions levees. Separer les deux demande de changer le contrat entre le
runtime et le lift (table chargee dynamiquement, ou module separe importe a l'execution):
c'est un chantier de conception, pas un reglage de lien. NON MESURE au-dela de cet echec.

---

## M8 — Non mesure, et pourquoi

| Element | Etat | Raison |
|---|---|---|
| instantiateStreaming de bout en bout | non mesure | le loader emscripten ne passe pas par instantiateStreaming; seul compileStreaming a pu etre isole |
| Decoupage runtime/PPU en deux modules charges | non mesure | la liaison runtime-seul echoue, la table du lift est referencee (M7) |
| Temps sur un vrai reseau (latence, debit) | non mesure | aucun lien a debit limite disponible dans cet environnement |
| Premier pixel RSX | non mesure | le GPU est encore un stub sur cette base; premiere activite guest est le seul jalon disponible |
| Compilation sur d'autres navigateurs | non mesure | seule Chromium 156 est installee |

---

## M9 — La contrainte de non-regression est-elle tenue ?

Oui. Run long du module corrige dans Chromium, 200 s de budget (/tmp/J-boot-K/long.json,
403 lignes de sortie):

~~~
1542.5  WASM-RUNTIME-UP
1542.9  WASM-GUEST-CODE-RAN
1575.6  WASM-BOOT-IMAGES-LOADED
1584.9  WASM-BOOT-STARTED
1598.2  WASM-FILE-PROVIDER-DRIVES-LOADER
2599.6  hist yield43=0 usleep141=4546
...     (compteur croissant jusqu'a la fin du budget)
199786.5 hist yield43=0 usleep141=285628
199786.5 top syscalls: 141(sys_timer_usleep)=285628 0(?)=0 ...
~~~

Le compteur usleep141 monte regulierement de 4 546 a 285 628 sur 200 s: le thread guest est
bien dans la boucle sys_timer_usleep. Comportement identique a la base; le correctif EH ne
change que la facon dont le module est lie.

Note honnete: le libelle label[255] lui-meme n'apparait pas dans les lignes de log capturees
(recherche insensible a la casse sur label et GetLabel: 0 resultat). Ce qui est mesure ici
est la boucle usleep, pas le nom de la fonction bloquante. Le point d'arret est le meme
qu'avant par le compteur de syscalls, pas par une trace du label.

---

## Recommandation

1. **Corriger l'edition de liens, tout de suite.** Lier libc++-mt-legacyexcept.a et
   libc++abi-mt-legacyexcept.a au lieu des wasmexcept. C'est la seule chose qui empeche le
   jeu de demarrer dans Chromium, ca ne coute rien (module 2 942 octets PLUS PETIT) et ca ne
   demande pas de recompiler. Verification: node -e WebAssembly.compile doit repondre OK, et
   la page doit atteindre WAITING_GUEST en ~1,1 s.
2. **Ne pas investir dans la compilation.** ~40 ms sur un chargement de ~1,07 s, soit 3 a
   4 %. Streaming, decoupage, parallelisation: aucun de ces axes ne deplacerait le temps de
   facon perceptible, et les workers mesures AGGRAVENT (101 -> 247 ms).
3. **Servir le module compresse.** 45,4 Mo -> 7,9 Mo (5,72:1), gratuit cote build, seul gain
   de chargement qui compte sur un vrai reseau.
4. **Si le chargement doit vraiment baisser, c'est game.data qu'il faut regarder**, pas le
   module: 126,4 Mo contre 45,4 Mo, et il ne compresse qu'a 1,31:1. Piste: ne precharger que
   le strict necessaire au demarrage et lire le reste a la demande (--preload-file charge
   tout d'un bloc aujourd'hui).
5. **Le decoupage runtime/PPU n'est pas un reglage de lien**: le runtime reference la table
   du lift. A ne tenter qu'avec un vrai changement de contrat entre les deux (M7), et son
   gain maximal serait de toute facon borne par le point 2.

---

## Artefacts

| Chemin | Contenu |
|---|---|
| /tmp/out-J | build de base (module livre, ne compile pas dans Chromium) |
| /tmp/out-Jleg | re-liaison legacyexcept — le correctif |
| /tmp/out-Jx | build exnref complet (voie -sWASM_LEGACY_EXCEPTIONS=0, recompilation) |
| /tmp/J-measure/ | pages de mesure compile/parallele + serve.py, serve_gz.py, drive.js |
| /tmp/J-boot-K/ | page de boot, long.html, lp.html, t152.html + JSON de resultats |
| /tmp/J-build-exnref.log | journal complet du build exnref |
