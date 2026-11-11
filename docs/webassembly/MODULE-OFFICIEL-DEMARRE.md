# Le module officiel demarre : trois causes trouvees et corrigees

## Le resultat

    ps3base-wasm: starting
    ps3base-wasm: guest base 0x11b8168, map span 0x90000000
    ps3base-wasm: guest machine up, 32 cpus
    WASM-RUNTIME-UP
    WASM-GUEST-CODE-RAN
    images mc=...

Le module construit par `tools/build_wasm.sh` **demarre et execute du code guest**. C'etait impossible au
round 191.

## Les trois causes

### 1. Mangling faux dans un stub

    gowa_stub_5 __asm__("_ZN2wj9ext_statsERm")   // unsigned long
    symbole reel        _ZN2wj9ext_statsERy      // unsigned long long

Sous wasm32, `unsigned long long&` se mangle `Ry` et `unsigned long&` `Rm`. Le stub portait le mangling de
l'hote, donc le lien le voyait non defini et le remplacait par un **trampoline abortif** — que les
constructeurs statiques appelaient, produisant `unreachable` **avant le premier printf**.

### 2. `PS3BASE_NO_RENDERER` absent du build

`tools/build_wasm.sh` ne le definissait pas. `rsx_no_display.cpp` compilait donc **sans son corps**, et les 14
symboles `gpu::rsx::*` partaient en trampolines abortifs.

De **25 a 10** symboles manquants apres l'ajout du drapeau.

### 3. `obj/` jamais nettoye

Le script reutilise `/tmp/wasm-out/obj`, donc les objets `jobsys_*` **exclus** restaient d'un build precedent.
Apres `rm -rf obj`, de **10 a 1** symbole manquant.

## Le quatrieme, fourni

`edge::gpu::g_active` : drapeau de God of War que `spu/thread.cpp` consulte. Defini a `false` — la branche
devient le chemin DMA ordinaire, ce que le jeu veut.

## Ce que ces trois causes ont en commun

**`-sERROR_ON_UNDEFINED_SYMBOLS=0` transforme un lien incomplet en plantage silencieux.** Le module se
construit, puis echoue a l'initialisation sur un symbole absent, sans nommer le symbole. J'ai du remonter par
le mangling.

## Ce qui reste

18 symboles non definis, tous dans des sous-systemes GOWA (`wj::install`, `jobsys::*`). Ils sont inoffensifs
au demarrage mais bloqueront si le jeu les appelle.

Le module atteint `images mc=` puis echoue — un probleme **different**, en aval du demarrage.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 5 criteres sur 6 ; **build reproductible, module 44,4 Mo, demarre** |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |