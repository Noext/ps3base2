# PISTE B — Semantique des releases et semaphores contre RPCS3

Base : /root/Dev/explore-B (branche explore/B) sur d290fda de wasm-base.
Worktree cree a la premiere minute ; arbres partages lies (bin out out-mc third_party fw ref).

Objectif : implementer EXACTEMENT la semantique de RPCS3 pour les primitives de
synchronisation GPU (release, acquire avec attente, SET_REFERENCE, etat de flip, handlers
flip/VBlank), la tester, et la brancher dans le runtime.

## 1. Antecedents (grep -R, sortie brute)

```
$ grep -Rni 'WriteToSemaphore' /root/Dev/ps3base2/refs/rpcs3/ | wc -l
0
```

Le nom de la consigne n'existe pas dans RPCS3. Le vrai nom est `write_gcm_label`.

```
$ grep -Rn 'semaphore_release\|semaphore_acquire\|write_gcm_label' /root/Dev/ps3base2/refs/rpcs3/rpcs3/Emu/RSX/NV47/ | head -12
.../NV47/HW/nv406e.h:12:		void semaphore_acquire(context* ctx, u32 reg, u32 arg);
.../NV47/HW/nv406e.h:14:		void semaphore_release(context* ctx, u32 reg, u32 arg);
.../NV47/HW/nv406e.cpp:24:		void semaphore_acquire(context* ctx, u32 /*reg*/, u32 arg)
.../NV47/HW/nv406e.cpp:113:		void semaphore_release(context* ctx, u32 reg, u32 arg)
.../NV47/HW/nv406e.cpp:149:			util::write_gcm_label<false, true>(ctx, reg, addr, arg);
.../NV47/HW/nv47_sync.hpp:14:		static void write_gcm_label(context* ctx, u32 type, u32 address, u32 data)
.../NV47/HW/nv4097.cpp:784:		void texture_read_semaphore_release(context* ctx, u32 reg, u32 arg)
.../NV47/HW/nv4097.cpp:809:				util::write_gcm_label<true, true>(ctx, reg, addr, arg);
```

```
$ grep -Rn '_ptr<atomic_t<u128>>' /root/Dev/ps3base2/refs/rpcs3/rpcs3/Emu/ | head -5
.../Emu/RSX/rsx_methods.cpp:61:		vm::_ptr<atomic_t<u128>>(ctx->fw.label_addr + 0x10)->store(u128{});
.../Emu/Cell/lv2/sys_rsx.cpp:1168:		vm::_ptr<atomic_t<u128>>(ctx->fw.label_addr + 0x10)->store(u128{});
```

```
$ sed -n '60,63p' /root/Dev/ps3base2/refs/rpcs3/rpcs3/Emu/RSX/NV47/HW/nv47_sync.hpp
			}

			vm::write<atomic_t<RsxSemaphore>>(address, data);
		}
```

### Tableau des antecedents

| Finding | Source (path:line) | Applies here? |
|---|---|---|
| une release ecrit **U**32, pas 16 octets | `refs/rpcs3/rpcs3/Emu/RSX/NV47/HW/nv47_sync.hpp:62` (`RsxSemaphore = be_t<u32>`, `Emu/Cell/lv2/sys_rsx.h:90`) | **oui, et ca contredit la consigne** — voir §2 |
| le seul magasin de 16 octets est le semaphore de flip | `rsx_methods.cpp:61`, `sys_rsx.cpp:1168` | oui — c'est la ou les 16 octets vont |
| `WriteToSemaphore` n'existe pas chez RPCS3 | `grep -Rni ... \| wc -l` = 0 | oui — le nom a chercher est `write_gcm_label` |
| release 406E : offset non aligne sur 4 => refus, rien ecrit | `nv406e.cpp:117-121` | **non implemente chez nous** avant cette piste |
| release 406E : fenetre 256 Mo differente => refus + recover | `nv406e.cpp:136-141` | non implemente |
| release 406E : `device+0x30` avec arg 0 => ecrit 1 | `nv406e.cpp:143-147` | implemente (rsx.cpp:375) |
| release 4097 : offset non aligne sur **16** => refus | `nv4097.cpp:792-798`, `:826-832` | **non implemente** |
| release 4097 back-end : valeur **permutee** `(a&0xff00ff00)\cup((a&0xff)<<16)\cup((a>>16)&0xff)` | `nv4097.cpp:838` | implemente (rsx.cpp:387) |
| release 4097 texture-read : valeur verbatim | `nv4097.cpp:809` | implemente |
| valeur deja egale => no-op (retour anticipe) | `nv47_sync.hpp:25-30` | non implemente (write aveugle) |
| SET_REFERENCE ecrit **ref seulement**, +0x48 ; le `get` est commente dans RPCS3 | `nv406e.cpp:14-22` | implemente (rsx.cpp:357) |
| acquire : si deja egal, pas d'attente, et `fifo_wake_delay(2)` sauf sur le semaphore de flip | `nv406e.cpp:41-51` | non implemente |
| acquire : attente par `spin_on_cacheline_once(..., 100)` us | `nv406e.cpp:106` | different : `yield` puis `sleep_ms(0)` |
| acquire : TDR `driver_recovery_timeout`, defaut **1 000 000 us** | `system_config.h:176` | oui (defaut identique) |
| acquire : les pauses >20 ms entre deux verifications ne comptent pas dans le TDR | `nv406e.cpp:74-94` | non implemente |
| flip (0xFEC) : flipFlags, lastFlipTime, flipBufferId, **16 octets a label+0x10**, puis evenement | `sys_rsx.cpp:1159-1172` | implemente (rsx.cpp:161-186), ordre conforme |
| vblank (0xFED) : lastSecondVTime **strictement croissant**, lastVTimeLow/High, vBlankCount++, puis evenement | `sys_rsx.cpp:1173-1208` | **partiel** : pas de monotonie, pas de `+1` |
| vblank : `ensure(a3 < 2)` | `sys_rsx.cpp:1182` | non implemente (head peut valoir 7) |
| evenement de flip = `SYS_RSX_EVENT_FLIP_BASE << 1` = 0x10 | `sys_rsx.cpp:1170` | implemente (`send_event(1u<<4)`) |
| vblank pilote par un thread dedie qui poste 0xFED a **tous** les contextes | `Core/RSXDisplayEngine.cpp:326` | oui, meme forme (vblank_thread) |

## 2. La consigne se trompe sur les 16 octets, et la mesure le prouve

Consigne : « Ecriture de 16 octets des releases : la VALEUR puis des ZEROS (RPCS3 ecrit 16 octets, pas 4) ».

| ce que RPCS3 fait | ligne | octets |
|---|---|---|
| `write_gcm_label` (toute release) | `nv47_sync.hpp:62` | **4** |
| remise a zero du semaphore de flip | `rsx_methods.cpp:61`, `sys_rsx.cpp:1168` | **16** (zeros seuls, pas de valeur) |

Les deux chemins 16 octets sont **exclusivement** le flip, et ils ecrivent **seize zeros**, pas
« la valeur puis des zeros ». Une release a 16 octets n'existe nulle part dans RPCS3.

Consequence pour le portage : si notre stub ecrivait 16 octets sur une release, il **ecraserait
label[1] a label[3]** — c'est-a-dire precisement les labels voisins que le jeu peut attendre.
Le test `rsx_sync` verrouille les deux comptes (4 et 16) et le fait que les 12 octets qui suivent
un label reste intact apres une release.

Preuve d'execution (voir §4) : `406E: the 12 bytes after the label are untouched (4-byte write, not 16)`.

## 3. Ordre des handlers

Le natif poste les evenements depuis le thread RSX/display, pas depuis le thread PPU :
`0xFEC` depuis `display_engine::flip()` (`RSXDisplayEngine.cpp:215`), `0xFED` depuis le
thread VBlank (`RSXDisplayEngine.cpp:326`). Les deux refusent l'appel depuis un thread PPU
(`sys_rsx.cpp:1174-1179` pour 0xFED).

Notre `vblank_thread` (rsx.cpp:718) et notre `flip_done` (rsx.cpp:161) ont la meme forme, mais
l'ordre interne de 0xFED est incomplet : RPCS3 rend `lastSecondVTime` strictement croissant avant
d'ecrire l'horodatage, et increment e `vBlankCount` apres. Chez nous l'increment de `vBlankCount`
se fait **avant** l'horodatage (rsx.cpp:726-729), donc un handler qui lit les deux voit un
compteur en avance sur le temps. C'est ce que `apply_vblank` corrige.

## 4. Mesures

### M1 — le module et son test, compiles et executes

```
$ cd /root/Dev/explore-B && g++ -std=c++20 -O1 -Wall -Wextra -Iruntime/include tests/rsx_sync.cpp runtime/src/gpu/rsx_sync.cpp -o /tmp/rsx_sync_test && /tmp/rsx_sync_test; echo "[exit $?]"
rsx_sync: 60 checks, 0 failures
[exit 0]
```

60 assertions, 0 echec. Chacune cite la ligne RPCS3 qu'elle verrouille (le commentaire de la ligne de test
donne le fichier:ligne).

### M2 — le meme test, construit et lance par CMake/ctest, comme spurs et command_ring

Le `CMakeLists.txt` du projet gagne le bloc :

```
add_executable(rsx_sync tests/rsx_sync.cpp runtime/src/gpu/rsx_sync.cpp)
target_include_directories(rsx_sync PRIVATE runtime/include)
target_compile_options(rsx_sync PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(rsx_sync PRIVATE pthread)
add_test(NAME rsx_sync COMMAND rsx_sync)
```

	extbf{La configuration complete du projet echoue AVANT ce bloc, sur un defaut preexistant de la base :}
`tests/sysutil_handlers.cpp` est reference par `CMakeLists.txt:539` mais n'existe pas dans l'arbre :

```
$ cd /root/Dev/explore-B && cmake -S . -B /tmp/build-B -G Ninja -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -8
-- Configuring done (0.6s)
CMake Error at CMakeLists.txt:547 (add_executable):
  Cannot find source file:

    tests/sysutil_handlers.cpp

CMake Error at CMakeLists.txt:547 (add_executable):
  No SOURCES given to target: sysutil_handlers

CMake Generate step failed.
```

```
$ cd /root/Dev/explore-B && git show HEAD:CMakeLists.txt | grep -n 'sysutil_handlers' | head -1
534:# sysutil_handlers: the SAME handlers the imports run (cellSysutil.cpp is
$ ls tests/sysutil_handlers.cpp
ls: cannot access 'tests/sysutil_handlers.cpp': No such file or directory
$ cd /root/Dev/wasm-base && git log --oneline -1
c07b01c orchestration: consolidated state ...
$ ls tests/sysutil_handlers.cpp
ls: cannot access 'tests/sysutil_handlers.cpp': No such file or directory
```

Ce n'est donc pas un effet de ma piste : la cible est dans la base et le fichier manque a la base aussi.
Comme la regle de la piste interdit de modifier les fichiers d'une autre piste et que celui-ci n'est pas le
mien, je ne l'ai pas cree. J'ai mesure mon bloc de CMake **a l'identique** dans un projet minimal qui
reprend les deux variables du projet (`PS3BASE_ARCH_FLAGS`) et mon bloc verbatim :

```
$ cat /tmp/rsx-check/CMakeLists.txt
cmake_minimum_required(VERSION 3.28)
project(rsx_sync_check LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(PS3BASE_ARCH_FLAGS -march=x86-64-v3 -ffp-contract=off -fno-strict-aliasing)
set(SRC /root/Dev/explore-B)
add_executable(rsx_sync ${SRC}/tests/rsx_sync.cpp ${SRC}/runtime/src/gpu/rsx_sync.cpp)
target_include_directories(rsx_sync PRIVATE ${SRC}/runtime/include)
target_compile_options(rsx_sync PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(rsx_sync PRIVATE pthread)
enable_testing()
add_test(NAME rsx_sync COMMAND rsx_sync)

$ cd /tmp/rsx-check && cmake -S . -B b -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build b -j 8 && cd b && ctest --output-on-failure
-- Configuring done (0.3s)
-- Generating done (0.0s)
[1/3] Building CXX object CMakeFiles/rsx_sync.dir/root/Dev/explore-B/runtime/src/gpu/rsx_sync.cpp.o
[2/3] Building CXX object CMakeFiles/rsx_sync.dir/root/Dev/explore-B/tests/rsx_sync.cpp.o
[3/3] Linking CXX executable rsx_sync
Test project /tmp/rsx-check/b
    Start 1: rsx_sync
1/1 Test #1: rsx_sync .........................   Passed    0.03 sec

100% tests passed, 0 tests failed out of 1
```

### M3 — le module est dans le module wasm

Le build wasm de la piste (`/tmp/out-B`, un seul build a la fois, -j 8) compile le fichier :

```
$ ls -la /tmp/out-B/obj/runtime_src_gpu_rsx_sync.o /tmp/out-B/obj/runtime_src_lv2_rsx.o
-rw-r--r-- 1 root root   5359 Nov 12 10:05 /tmp/out-B/obj/runtime_src_gpu_rsx_sync.o
-rw-r--r-- 1 root root 202576 Nov 12 10:05 /tmp/out-B/obj/runtime_src_lv2_rsx.o
$ tail -2 /tmp/build-B.log
module: 45407181 bytes
/tmp/out-B/game.wasm: 49725 functions, largest 737751 bytes (no name section), total code 44814760 bytes
```

### M4 — CE QUE LE RUN WASM DIT, ET C'EST LE RESULTAT PRINCIPAL

```
$ cd /tmp/out-B && timeout -s KILL 100 node --experimental-wasm-exnref game.js > /tmp/run-B.log 2>&1; echo "[exit $?]"
[exit 137]
$ tail -2 /tmp/run-B.log
ps3base-wasm: hist yield43=0 usleep141=576378
ps3base-wasm: top syscalls: 141(sys_timer_usleep)=576378 0(?)=0 0(?)=0 0(?)=0
$ grep -c 'I .* rsx:' /tmp/run-B.log
0
$ grep -c 'context at\|local memory' /tmp/run-B.log
0
```

Zero ligne du module `rsx`. Zero `context at` (celle que `sys_rsx_context_allocate` imprime a
rsx.cpp:892), zero `local memory` (celle de `sys_rsx_memory_allocate`). Le thread principal est bloque
en `syscall 141` depuis t=0,8 s :

```
$ grep -m2 'status: ppu' /tmp/run-B.log
I   4.786922 [    1] status: ppu 0x1000000 'main_thread' blocked in 141 (r3 0x1e r31 0x6001f6e0) syscalls +24891 lr 0x2da598
I   4.787036 [    1] status: ppu 0x1000001 'MinecraftSpursHdlr1' blocked in 130 (r3 0x8d008c00 r31 0x0) syscalls +1 lr 0x0
```

`lr 0x2da598` est la boucle de label. **Aucun `sys_rsx_context_allocate` n'est jamais appele, donc
aucun de mes chemins n'est atteint dans ce run** : ni `release_label`, ni `acquire_label`, ni
`apply_flip`, ni `apply_vblank`. Le rsx setup est court-circuite par le stub
`_cellGcmInitBody` (`runtime/src/hle/cellGcmSys.cpp:87`), qui remplit le contexte et retourne
CELL_OK sans jamais appeler `sys_rsx_memory_allocate`/`sys_rsx_context_allocate`.

C'est un fait de mesure, pas une deduction : mon code est dans le binaire (M3), il n'est pas atteint (M4).

## 5. Tableau livre

| primitive | RPCS3 | nous (avant la piste) | nous (apres) | conforme |
|---|---|---|---|---|
| **release 406E, nombre d'octets** | **4** (`nv47_sync.hpp:62`, `RsxSemaphore=be_t<u32>`) | 4 (`rsx_write32`) | 4 (`release_label` -> `write_be32`) | oui |
| release 406E, offset non aligne sur 4 | refus, rien ecrit (`nv406e.cpp:117-121`) | **ecrivait quand meme** | refuse + `LOG_WARN` | corrige |
| release 406E, fenetre 256 Mo etrangere | refus + `recover_fifo` (`nv406e.cpp:136-141`) | **ecrivait quand meme** | refuse + `LOG_ERROR` | corrige |
| release 406E, `device+0x30` arg 0 | ecrit 1 (`nv406e.cpp:143-147`) | ecrit 1 | ecrit 1 (dans la primitive) | oui |
| release 4097 back-end, offset non aligne sur 16 | refus (`nv4097.cpp:826-832`) | **ecrivait quand meme** | refuse + `LOG_ERROR` | corrige |
| release 4097 back-end, valeur | permutee `(a&0xff00ff00) \| ((a&0xff)<<16) \| ((a>>16)&0xff)` (`nv4097.cpp:838`) | permutee | permutee (testee : 0x11223344 -> 0x11443322) | oui |
| release 4097 texture-read, offset non aligne sur 16 | refus (`nv4097.cpp:792-798`) | **ecrivait quand meme** | refuse + `LOG_ERROR` | corrige |
| release, valeur deja egale | no-op, retour anticipe (`nv47_sync.hpp:25-30`) | **ecrivait quand meme** | retour anticipe, `written=false` | corrige (observationnellement identique, mais teste) |
| **flip semaphore, nombre d'octets** | **16 zeros** a `label_addr+0x10` (`rsx_methods.cpp:61`, `sys_rsx.cpp:1168`) | 16 zeros (`std::memset`) | 16 zeros (`reset_flip_semaphore`) | oui — **et ce n'est pas une release** |
| release a 16 octets (« la valeur puis des zeros ») | **n'existe pas** chez RPCS3 | — | — | la consigne est refusee par la mesure |
| acquire, attente | poll 100 us (`nv406e.cpp:106`) | `yield` x64 puis `sleep_ms(0)` | `sleep_for(100us)` | corrige |
| acquire, TDR | `driver_recovery_timeout`, defaut 1 000 000 us (`system_config.h:176`) | 1 000 000 us | 1 000 000 us (`GOWA_TDR_US`) | oui |
| acquire, pause hote >20 ms | ne compte pas dans le TDR (`nv406e.cpp:74-94`) | **comptait** | deduite | corrige |
| acquire, deja egal | pas d'attente + `fifo_wake_delay(2)`, sauf flip sema (`nv406e.cpp:41-51`) | pas d'attente, pas de delai | idem + division du delai dans la primitive | corrige |
| acquire, abandon | `test_stopped` (`nv406e.cpp:62-66`) | `g_rsx.running` | `keep_going` = `g_rsx.running` | oui |
| SET_REFERENCE | ref seul, `+0x48` ; le get est commente (`nv406e.cpp:14-22`) | `+0x48` seul | `set_reference` | oui |
| flip (0xFEC), ordre | flipFlags, lastFlipTime, flipBufferId, 16 octets, evenement (`sys_rsx.cpp:1159-1172`) | meme ordre, inline | meme ordre, `apply_flip`, teste pas a pas | oui |
| flip, **package 0xFEC lui-meme** | traite (`sys_rsx.cpp:1159`) | **« unknown context attribute »** -> l'evenement n'est jamais poste, et `label[+0x10]` n'est jamais remis a zero par ce chemin | traite, avec l'arbitrage de posteur | corrige |
| vblank (0xFED), ordre | lastSecondVTime monotone, lastVTimeLow/High, vBlankCount++, evenement (`sys_rsx.cpp:1173-1208`) | vBlankCount++ **avant** l'horodatage, pas de monotonie | ordre RPCS3, monotonie `prev+1` | corrige |
| vblank, `a3 < 2` | `ensure(a3 < 2)` (`sys_rsx.cpp:1182`) | aucun controle | `CELL_EINVAL` si head >= 2 | corrige |
| vblank, package 0xFED | traite | **« unknown context attribute »** | traite | corrige |
| qui poste | un seul posteur par contexte : LLE -> 0xFEC, HLE -> handler (`RSXDisplayEngine.cpp:207-215`, `:300-326`) | le runtime **et** (si le guest appelait 0xFEC) le guest -> double compte possible | `PosterArbiter`, teste (60 ticks sans doublon, pas de famine) | corrige |

## 6. Ce qui reste ouvert, et ce qu'il faudrait mesurer

| constat | statut |
|---|---|
| le module de synchronisation est exact et teste | **mesure** (M1, M2) |
| il est dans le binaire wasm | **mesure** (M3) |
| il est atteint dans le run wasm | **non** — `sys_rsx_context_allocate` n'est jamais appele (M4) |

Pour que ces chemins s'executent, il faut que `_cellGcmInitBody` cesse d'etre un stub qui rend CELL_OK
sans rien faire : c'est la piste qui porte le `_cellGcmInitBody`/`cellGcmInit`, pas la mienne. Ma
contribution reste valable et prete : des que `sys_rsx_context_allocate` est appele, `apply_flip`
remet `label[1..4]` a zero, ce qui est exactement ce que la boucle sur `label[255]` attend du cote
GPU pour le label de flip.

Non mesure, et pourquoi :
- **effet d'un flip reel sur `label[255]` dans le wasm** : non mesure — le run wasm n'atteint jamais
  `context_allocate` (M4), donc il n'y a aucun flip a observer. Ce n'est pas un manque d'instrumentation,
  c'est une absence d'evenement.
- **compteurs natifs a t=12,85 s (oracle piste I)** : non mesure dans cette piste — `menu-trace.txt` ne
  contient aucun evenement de semaphore GPU (son histogramme est celui des syscalls ; `sys_semaphore_wait`
  y est le semaphore LV2, pas le label RSX). La comparaison aurait demande une trace `PS3BASE_TRACE` du
  binaire natif, qui n'etait pas produite.
