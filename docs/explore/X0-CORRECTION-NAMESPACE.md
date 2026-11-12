# X0 — Correction du namespace + etat des trames

## Ma correction precedente etait inoperante (mesure par nm)

    runtime_src_lv2_rsx.o        U _ZN3lv212_GLOBAL__N_116gcm_flip_handlerEv
    runtime_src_hle_cellGcmSys.o T _ZN3hle16gcm_flip_handlerEv

J'avais sorti la declaration du namespace anonyme, mais elle restait dans `namespace lv2`
(ouvert ligne 44 de rsx.cpp). Le symbole ne resolvait donc pas, et
`-sERROR_ON_UNDEFINED_SYMBOLS=0` en faisait un `abort()` silencieux.

**J'avais annonce « corrige ». C'etait faux.** Les pistes X1 et X4 l'ont mesure independamment.

## La correction qui marche

    namespace hle { uint32_t gcm_flip_handler(); }
    ... hle::gcm_flip_handler() ...

Apres : `U _ZN3hle16gcm_flip_handlerEv` = `T _ZN3hle16gcm_flip_handlerEv`, `missing function: 0`.

## Etat des trames apres toutes les corrections

| mesure | avant toute correction | apres |
|---|---|---|
| lignes `rsx:` | 0 | **40** |
| byte[0xBE070C] | 0 | **1** |
| flips | 0 | **107 885** |
| methodes FIFO | 4 499 617 | **215 999 661** |
| paquets | 1 999 833 | **95 999 830** |
| draws | 49 995 | **2 399 994** |
| clears | 0 | **0** |
| missing function | present | **0** |

    I 10.481971 rsx: fifo 5s: busy 100.0%, idle 0.0%, 911 idle sleeps/s

Le processeur de commandes tourne a **100 % de charge**.

## Ce qui manque

    clears=0
    main menu NOT reached

216 M de methodes, 2,4 M de draws, **zero CLEAR_SURFACE**. Ce n'est plus un probleme de
deblocage mais de **contenu du flux**.

## Les corrections qui ont debloque, dans l'ordre

1. `rsx_setup_for_hle()` appele depuis le HLE de `_cellGcmInitBody` : contexte RSX cree
   (`local memory 0xf900000 at 0x50000000`, `context at 0x00e00000`, identiques au natif) ;
2. threads vblank et fifo demarres depuis ce setup ;
3. `0x00be070c` ecrit a chaque vblank (le titre attend cet octet non nul pour ouvrir sa trame,
   puis le remet a zero lui-meme) ;
4. `namespace hle` pour la declaration du handler de flip (le lien ne le signalait pas :
   `-sERROR_ON_UNDEFINED_SYMBOLS=0` transforme un manquant en abort silencieux).

## Confirmation cote natif (pistes X1 et X2)

    _gcm_intr_thread -> call_opd -> f_330082d8 (dispatcher RSX LLE)
      -> call_indirect OPD 0x00bbe658 -> f_002dd780 -> st8(0x00be070c, 1)

Un seul thread, 59.94 Hz, 2 211 appels en 50 s, `r3=1` a l'entree. Notre vblank fait le meme
travail mais sans l'indirection OPD : X2 a mesure que `0x00bbe658` est un **OPD**, pas du code.