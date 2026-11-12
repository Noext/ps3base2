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
$ cd /root/Dev/explore-B && g++ -std=c++20 -O1 -Wall -Iruntime/include tests/rsx_sync.cpp runtime/src/gpu/rsx_sync.cpp -o /tmp/rsx_sync_test && /tmp/rsx_sync_test; echo "[exit $?]"
rsx_sync: 56 checks, 0 failures
[exit 0]
```

56 assertions, 0 echec. Chacune cite la ligne RPCS3 qu'elle verrouille.

### M2 — le meme test, par le CMakeLists du projet (comme spurs/command_ring/audio_ring)

```
$ cd /root/Dev/explore-B && cmake -S . -B /tmp/build-B -G Ninja -DCMAKE_BUILD_TYPE=Release
# (sortie brute : voir §4.1)
$ cmake --build /tmp/build-B --target rsx_sync -j 8 && cd /tmp/build-B && ctest -R rsx_sync --output-on-failure
```

(sortie collee a la fin de cette section apres execution)

## 5. Tableau livre

(rempli apres la mesure M2, avec la colonne « nous » relevee dans le code de rsx.cpp)

## 6. Non mesure

- duree de vie du label[255] sur 152 s de run wasm : **non mesure** — le wasm complet n'a pas ete
  reconstruit dans cette piste (un seul build a la fois, 12 Go libres, ~40 min par build) ; la
  verification a porte sur les tests unitaires, pas sur une execution du titre.
