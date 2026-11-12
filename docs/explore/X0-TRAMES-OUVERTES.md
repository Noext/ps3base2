# X0 — Les trames sont ouvertes (orchestrateur, mesure)

| mesure | avant | apres |
|---|---|---|
| lignes rsx: | **0** | **5** |
| byte[0xBE070C] | 0 | **1** |
| flips | 0 | **71 440** |
| methodes FIFO | 4 499 617 | **143 014 545** |
| paquets | 1 999 833 | **63 562 021** |
| draws | 49 995 | **1 589 049** |
| pc | 0x002dc20c (boucle) | **0x003a18b0** |
| clears | 0 | **0** (inchange) |

## Les quatre corrections

### 1. Contexte RSX cree depuis le HLE

Le corps LLE que notre HLE remplace atteint sys_rsx_memory_allocate(668) puis
sys_rsx_context_allocate(670). Sans eux aucun contexte RSX, donc vblank_thread jamais lance.
Ajout de lv2::rsx_setup_for_hle() appele depuis le HLE.

    I 1.023284 rsx: local memory 0xf900000 bytes at 0x50000000 (hle init)
    I 1.023738 rsx: context at 0x00e00000 (hle init)

Taille et adresse identiques au natif.

### 2. Threads vblank et fifo demarres

    I 1.027011 rsx: vblank and fifo threads started (hle init)

### 3. Semaphore de trame ecrit a chaque vblank

f_002dc1e4_body attend un octet NON NUL a 0x00be070c, ouvre sa trame, puis le remet a zero.
Rien ne l'ecrivait : trois greps exhaustifs n'avaient trouve qu'un probe qui le LIT.

### 4. Bug critique trouve par X1 et corrige

extern uint32_t gcm_flip_handler() etait declare DANS le namespace anonyme de rsx.cpp
(ouvert ligne 116). Symbole attendu : _ZN3lv212_GLOBAL__N_116gcm_flip_handlerEv.
Defini : _ZN3hle16gcm_flip_handlerEv. Le lien ne passait que par
-sERROR_ON_UNDEFINED_SYMBOLS=0 : le module contenait abort(missing function).

**Le premier flip aurait tue le module.** Declaration remontee hors du namespace anonyme.

## Ce qui manque

    clears=0
    main menu NOT reached

Le jeu travaille (143 M methodes, 1,59 M draws) mais n'emet aucun CLEAR_SURFACE et n'atteint
pas le menu. pc=0x003a18b0 en sommeil, lr=0x002d9244.

## Confirmation cote natif (piste X1)

    _gcm_intr_thread -> call_opd -> f_330082d8 (dispatcher RSX libgcm_sys LLE)
      -> call_indirect OPD 0x00bbe658 -> f_002dd780 -> st8(0x00be070c, 1)

664 ecritures de 1 par l'interruption, 665 remises a 0 par main_thread a lr=0x002dc33c,
premiere trame a t=13,628540 s. Notre vblank fait maintenant le meme travail.