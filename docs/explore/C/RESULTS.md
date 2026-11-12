# Piste C — Test FIFO sans le jeu

Worktree : /root/Dev/explore-C (branche explore/C, base d290fda).
Livrable : tests/rsx_fifo_synth.cpp + tests/rsx_fifo_synth.sh — un FIFO NV4097 fabrique a la
main, execute par le VRAI lecteur de FIFO du runtime (runtime/src/lv2/rsx.cpp) compile en
WebAssembly et lance sous node. Aucune donnee du jeu n'entre dedans.

**Verdict : reference, label et flips sont PROUVES sur un FIFO fabrique. Le CLEAR est livre au
lecteur mais PERDU par un bug du stub sans ecran — mesure ci-dessous, pas une supposition.**

## Antecedents (grep refs/ d'abord, regle du protocole)

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| NV406E_SET_REFERENCE = 0x00000050 >> 2, handler ecrit dma.ref = arg | refs/rpcs3/rpcs3/Emu/RSX/gcm_enums.h:533, refs/rpcs3/rpcs3/Emu/RSX/NV47/HW/nv406e.cpp:16-25 | **oui** — verifie sur ctrl+0x48 |
| set_reference ecrit le ref dans la DMA control, PAS dans le FIFO | refs/.../nv406e.cpp:21-24 | **oui** — PASS mesure |
| NV4097_BACK_END_WRITE_SEMAPHORE_RELEASE = 0x00001d70 >> 2 | refs/rpcs3/rpcs3/Emu/RSX/gcm_enums.h:706 | **oui** |
| Le release permute les octets 1 et 3 avant d'ecrire | refs/.../rsx_methods.cpp nv4097::back_end_write_semaphore_release ; runtime/src/lv2/rsx.cpp:386-387 | **oui** — la valeur attendue EST la valeur permutee |
| **Largeur d'ecriture d'un release = 4 octets** | refs/rpcs3/rpcs3/Emu/Cell/lv2/sys_rsx.h:90 : using RsxSemaphore = be_t<u32> ; ecrit par vm::write<atomic_t<RsxSemaphore>> en nv47_sync.hpp:62 | **oui, et MESURE** : les labels voisins gardent leurs sentinelles |
| Label stride 0x10, index jusqu'a 255 | refs/.../cellGcmSys.cpp:159-162 (label_addr + 0x10 * index) | oui — c'est ce qui rend le debordement a 16 octets dangereux |
| Release sur une valeur DEJA egale => no-op | refs/.../nv47_sync.hpp:25-31 ("It's a no-op to write the same value") | **oui** — nous ecrivons quand meme, mesure ci-dessous |
| SEMAPHORE_ACQUIRE (0x68) attend sema == arg | refs/.../nv406e.cpp:27-95 | oui — present dans la sequence native rejouee, ne bloque pas |
| Un voisin porte un test synthetique de FIFO sans jeu | refs/jdc-ps3recomp/libs/video/tests/test_rsx_synth.c | oui — precedent direct |
| Format des paquets (methode<<18 | count, bit non-inc 0x40000000) | runtime/src/lv2/rsx.cpp:598-600 | oui — c'est le format emis |

## 1. Ce qui a ete construit

    tests/rsx_fifo_synth.cpp          le test (FIFO fabrique + boucle d'attente invitee)
    tests/rsx_fifo_synth_support.cpp  9 stubs de symboles hors-sujet (force, guest, stacks,
                                      cbwatch, loader, image, crash, ps3base_store_probe)
    tests/rsx_fifo_synth.sh           compilation + link + run, sortie /tmp/out-C

Aucun fichier du runtime n'est modifie. Le test lie les VRAIS
runtime/src/lv2/rsx.cpp, runtime/src/memory.cpp, runtime/src/reservation.cpp et
runtime/src/gpu/rsx_no_display.cpp : ce qui s'execute est le meme code que le flux de
commandes du jeu, pas une reimplementation. Le module WebAssembly tourne en 1,1 s.

## 2. Sortie brute — run final

Commande : bash tests/rsx_fifo_synth.sh . /tmp/out-C
(log complet /tmp/C/final-run.log, md5 b938f15ee88a582b823179c056f94b79, 13 PASS, 0 FAIL)

    == piste C: synthetic NV4097 FIFO through the runtime FIFO reader (wasm)
    I   0.819911 [    1] mem: bitset mode: 2304 MB flat, 589824 pages, no mprotect
    I   0.820865 [    1] vm: guest base 0xdb1770 (backing: bitset)
    [setup] sys_rsx_memory_allocate = 0x0 handle=0x5a5a5a5b local=0x50000000
    I   0.821742 [    1] vm: region 3 placed at 0x10000000
    W   0.822803 [    1] rsx: fifo and vblank threads started with default stacks
    [setup] sys_rsx_context_allocate = 0x0 ctx=0x55555555 dma=0x10000000 driver=0x10100000 reports=0x10200000
    I   0.823080 [    1] rsx: iomap io 0x01000000 -> ea 0x00100000 size 0x100000
    [setup] sys_rsx_context_iomap = 0x0 io 0x01000000 -> ea 0x00100000
    [setup] label io 0x01008000 ea 0x00108000 initial 0x00000000, neighbours 0xa1a1a1a1 0xb2b2b2b2 0xc3c3c3c3
    [fifo] 40 bytes at ea 0x00100000 (io 0x01000000), guest byte order as stored:
    [fifo]   +0x000 00040050
    [fifo]   +0x004 deadbeef
    [fifo]   +0x008 000401a4
    [fifo]   +0x00c 00000001
    [fifo]   +0x010 00041d6c
    [fifo]   +0x014 01008000
    [fifo]   +0x018 00041d70
    [fifo]   +0x01c 11223344
    [fifo]   +0x020 0004e920
    [fifo]   +0x024 80000001
    I   0.825230 [    2] rsx: null RSX FIFO thread started
    [wait] label == release(0x11223344) = 0x11443322 after 59 usleep(30) iteration(s)
    PASS the FIFO wrote the label, so the guest wait loop terminates
    [meas] dma+0x48 (reference) = 0xdeadbeef, expected 0xdeadbeef
    PASS SET_REFERENCE wrote dma.ref
    [meas] label after phase 1 = 0x11443322, expected release(0x11223344) = 0x11443322
    PASS the release wrote the byte-swapped value
    [meas] label[1..3] after the release = 0xa1a1a1a1 0xb2b2b2b2 0xc3c3c3c3, sentinels were 0xA1A1A1A1 0xB2B2B2B2 0xC3C3C3C3
    PASS the release wrote exactly 4 bytes: the neighbouring labels are untouched
    I   0.826692 [    2] rsx: flip 1 (buffer 1) worst 0.0 ms, >50ms 0, >100ms 0
    [meas] flips after phase 1 = 1 after 3 ms
    PASS the flip packet incremented the flip counter
    [meas] same-value release: reservation version 640 -> 768 (we WROTE it; RPCS3 would no-op)
    [wait] label == release(0x55667788) = 0x55887766 after 3 iteration(s)
    [meas] label after phase 2 = 0x55887766 (was 0x11443322)
    PASS the label advanced to the second value
    [meas] flips after phase 2 = 2 after 20 ms
    PASS the second flip packet incremented the counter again
    [meas] commands=7 draws=0 clears=0 vp_words=0 const_writes=0
    [meas] framebuffer hash=0x5e509dc5 distinct=1 ; expected uniform 0x20504030 -> hash 0x64509dc5
    PASS the walker decoded all seven register writes, clear included
    PASS the framebuffer does NOT hold the cleared colour (bug, see RESULTS.md)
    [diag] clears==0 while commands==7: method(0x1D94) was called and the texture catch-all at rsx_no_display.cpp:203 swallowed it -- 0x1D94 is inside [0x1A00,0x2000), so case CLEAR_SURFACE (:261) is unreachable.
    [diag] framebuffer hash 0x5e509dc5 == FNV-1a of a uniform 0x00000000 pixel: the buffer still holds its initial contents, so no clear ever ran.
    [not-measured] presented pixel: the build has no display surface (PS3BASE_NO_RENDERER=1, gpu::flip is a no-op); needs piste G's renderer.
    [native] reports 0x10200000: label@0x410 = 0x00000000, label@0x400 = 0x00000000 (both 0)
    [native] 96 bytes, 12 packets, copied from the oracle trace
    [native] label@0x410 = 0x00000001 (want release(0x00010000) = 0x00000001), label@0x400 = 0x00000001, ref = 0x00000000, after 0 ms
    PASS the native first-flip sequence wrote label@0x410
    PASS the native's SET_REFERENCE value (0) is what dma.ref holds
    PASS the 0x0068 acquire did not stall: the 0x6C release satisfied it
    [native] clear counter after the native sequence: 0 (0x1D94 = 3 was delivered; the stub swallows it, same bug)
    [meas] flips final = 3
    [note] final FIFO state: put=0x01000060 get=0x01000060 ref=0x00000000 label=0x55887766
    == ALL CHECKS PASSED (0 failure(s))

## 3. Les trois verifications demandees

**Reference.** 0x0050 avec 0xDEADBEEF -> vm::read32(dma_ea + 0x48) == 0xDEADBEEF. Le ref n'est
PAS ecrit dans le FIFO : il est lu dans le bloc de controle DMA, la ou le runtime l'a place.

**Label.** 0x1D70 avec 0x11223344 -> le label vaut 0x11443322
((a & 0xFF00FF00) | ((a & 0xFF) << 16) | ((a >> 16) & 0xFF)). Puis 0x1D70 avec 0x55667788 ->
0x55887766. Le label AVANCE, et la boucle d'attente du test — lire, comparer, usleep(30),
recommencer, la forme exacte de f_002da36c L_2da590 — se termine en 59 puis 3 iterations.

**Flips.** Deux paquets 0xE920 (flip, tete 0, buffer 1) ; rsx_flip_count() passe de 0 a 1 puis a 2,
en 3 ms puis 20 ms (le flip est asynchrone : la methode met le buffer en attente et le thread
VBLANK le termine au tick suivant, runtime/src/lv2/rsx.cpp:224-225 et :730-731).

## 4. Le CLEAR : livre, mais perdu — BUG DU STUB, mesure

Le paquet 0x1D94 (CLEAR_SURFACE) **arrive bien** au lecteur : commands == 7 compte exactement les
sept registres emis (0x0050, 0x01A4, 0x1D6C, 3x 0x1D70, 0x1D94), et commands est incremente en
tete de gpu::rsx::method (runtime/src/gpu/rsx_no_display.cpp:179).

Il est ensuite **jete** :

    runtime/src/gpu/rsx_no_display.cpp:203   if (reg >= TEXTURE_BASE_R && reg < 0x2000) { ... return; }
    runtime/src/gpu/rsx_no_display.cpp:132   constexpr uint32_t TEXTURE_BASE_R = 0x1A00;
    runtime/src/gpu/rsx_no_display.cpp:43    constexpr uint32_t CLEAR_SURFACE = 0x1D94;
    runtime/src/gpu/rsx_no_display.cpp:261   case CLEAR_SURFACE: g_clears.fetch_add(1); fb_clear(arg); ...

TEXTURE_BASE_R vaut 0x1A00 et CLEAR_SURFACE vaut 0x1D94, donc le filtre « toute la fenetre
texture 0x1A00-0x2000 » **avale 0x1D94 et retourne avant le switch**. Le case CLEAR_SURFACE de la
ligne 261 est du code mort.

Preuve par l'image, pas par la lecture : le hash FNV-1a du framebuffer est 0x5e509dc5, qui est
exactement le hash d'une image uniforme 0x00000000 (recalcule independamment en Python, meme
recurrence) — le framebuffer est encore vierge. Le hash attendu apres un clear de 0x20304050
serait 0x64509dc5. clears reste a 0.

Portee : ce bug est dans le build PS3BASE_NO_RENDERER=1 seulement. Le vrai renderer n'a pas ce
filtre (runtime/src/gpu/rsx_renderer.cpp:4249 : case CLEAR_SURFACE: clear(arg); break; apres les
seuls cas speciaux). Il n'est donc pas dans le chemin du jeu natif ; il est dans le chemin wasm, et
il y rend tout clear invisible a la mesure.

**Pixel presente : NON MESURE.** Le build PS3BASE_NO_RENDERER=1 n'a pas de surface d'affichage
(gpu::flip est un no-op, rsx_no_display.cpp:364-366) : il n'y a pas de pixel a presenter, seulement
le framebuffer CPU interne. Prouver un pixel presente exige le renderer de la piste G. C'est ecrit
dans la sortie du test, pas contourne.

## 5. La sequence NATIVE du premier flip, rejouee

L'oracle natif (piste I) a produit docs/explore/I/menu-trace.txt. J'ai relance le binaire natif
avec GOWA_RSX_TRACE=12.80 GOWA_RSX_TRACE_END=13.10 pour capturer la fenetre du premier flip
(/tmp/C/trace/native-first-flip.log, 20 237 methodes). Le natif emet, dans cet ordre :

    0x0050 0x00000000    SET_REFERENCE
    0xe944 0x00000001    flip/sync tete 1
    0x0060 0x56616661    semaphore DMA context 406E -> device
    0x0064 0x00000030    semaphore offset 0x30
    0x006c 0x00000000    release 406E = 0
    0x0068 0x00000001    acquire 406E == 1
    0x01a4 0x66606660    semaphore DMA context 4097 -> reports
    0x1d6c 0x00000410    semaphore offset 0x410
    0x1d70 0x00010000    back-end release (permute -> 1)
    0x1d6c 0x00000400    offset 0x400 (label - 1)
    0x1d70 0x00010000
    0x1d94 0x00000003    CLEAR_SURFACE

Rejoue tel quel dans le FIFO fabrique : label@0x410 et label@0x400 valent tous deux 0x00000001
(= release(0x00010000)), en moins d'1 ms, et l'acquire 0x0068 ne bloque pas (le runtime substitue 1
a un release nul sur device+0x30, runtime/src/lv2/rsx.cpp:375). **Notre lecteur traite donc
correctement le flux exact que le titre emet au premier flip.**

## 6. Deux ecarts mesures avec RPCS3 (consigne de l'orchestrateur sur les releases)

1. **Largeur 4 octets : CONFORME, et prouve.** Les trois labels voisins sont remplis de sentinelles
   avant le run et les gardent apres (0xa1a1a1a1 0xb2b2b2b2 0xc3c3c3c3). Une ecriture de 16 octets
   les aurait ecrasees — ce que la consigne signalait comme risque. Il n'y a pas de risque ici :
   rsx_write32 ecrit 4 octets (runtime/src/lv2/rsx.cpp:388).

2. **Release sur une valeur deja egale : ECART REEL.** RPCS3 n'ecrit rien
   (refs/.../nv47_sync.hpp:25-31). Nous ecrivons. Mesure par la version de reservation, qui
   s'incremente a chaque store : **640 -> 768**. Consequence pratique : le label est reecrit a
   l'identique, ce qui est inoffensif pour la valeur mais reveille les dormeurs de cette ligne et
   invalide le cache de decodage associe. Non corrige ici (je ne modifie que ma piste) ; a arbitrer
   par la piste qui possede runtime/src/lv2/rsx.cpp.

## 7. Ce que ce test ne prouve pas

- **Le pixel presente** : pas de surface dans ce build (section 4).
- **Que le label[255] du jeu soit ecrit** : cellGcmGetLabelAddress (NID 0xF80196C1) figure bien
  dans out/ppu/table.cpp:23494 comme import 138, mais **aucune HLE ne l'implemente** dans
  runtime/src/hle/cellGcmSys.cpp (8 HLE_FUNC, aucune n'est celle-la). L'adresse que le jeu passe au
  NID 138 est donc celle que l'import non implemente renvoie, pas label_addr + 0x10*255. Ce test
  prouve que le LECTEUR ecrit un label quand on lui en donne l'adresse ; il ne prouve pas que le
  jeu obtient la bonne adresse. La correction est dans runtime/src/hle/cellGcmSys.cpp, hors de ma
  piste.
- **Le CLEAR sous le vrai renderer** : non mesure, le build de cette piste n'a pas de renderer.

## Demandes

- **Piste G** : le test de pixel presente exige le renderer. Alternative minimale : un accesseur
  public gpu::no_display_framebuffer(uint32_t* out, uint32_t w, uint32_t h); en son absence je
  mesure par le hash publie, ce qui est deja une preuve d'image, mais pas une preuve de
  presentation.
- **Proprietaire de runtime/src/gpu/rsx_no_display.cpp** : le filtre texture de la ligne 203 doit
  exclure 0x1D94 (et 0x1D74, 0x1D70, tout ce qui n'est pas un registre d'unite texture). Sinon le
  CLEAR, le ZPASS et les releases sont tous invisibles a la mesure des qu'ils tombent dans
  0x1A00-0x2000.
- **Proprietaire de runtime/src/lv2/rsx.cpp** : le no-op sur release de valeur egale (section 6.2).
- **Proprietaire de runtime/src/hle/cellGcmSys.cpp** : cellGcmGetLabelAddress manque ; c'est
  peut-etre la vraie cause du blocage L_2da590 (le jeu compare une valeur a une adresse qui ne
  contient jamais rien), et c'est a une ligne de distance de cette piste.
