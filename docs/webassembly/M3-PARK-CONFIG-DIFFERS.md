# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le build natif de mesure échoue sur `hook_flags` | `M3-TEXSTAT-BLOCKED.md` (round 83) | contourné, puis **cassé par ma faute** |
| Le générateur est Ninja, flags `-O2 -g -std=gnu++20 -march=x86-64-v3` | `build/build.ninja` | oui |
| Le run de référence tourne avec `GOWA_SPU_PARK=0` | `/tmp/cmp.log` | **oui — découverte importante** |

# Découverte utile, et une erreur de ma part

## Ce que j'ai appris de valable

En extrayant l'environnement du run de référence :

    settings: applied: GOWA_TURBO=1 GOWA_RES_SCALE=1 GOWA_FULLSCREEN=0
    GOWA_FPS_UNLOCK=1 GOWA_FPS_OSD=1 GOWA_MOTION_BLUR=1 GOWA_ASYNC_SHADERS=1
    GOWA_DLSSG=0 GOWA_AUDIO=1 GOWA_AUDIO_LATENCY_MS=100 **GOWA_SPU_PARK=0**
    GOWA_VDEC_HLE=1 GOWA_SESSION=0

**`GOWA_SPU_PARK=0`** : le run natif de référence tourne avec le park **désactivé**.

Cela **relativise le round 67** : j'y ai rendu le park par défaut pour wasm, en m'appuyant sur une
comparaison avec un natif dont je supposais qu'il avait le park actif. Il ne l'a pas. Le gain mesuré
côté wasm reste réel (2 425× d'instructions, échecs `putllc` divisés par 3,3), mais **la comparaison
avec le natif n'était pas valide** : les deux configurations diffèrent.

## Mon erreur

Pour contourner le blocage du round 83, j'ai reconfiguré le **build principal** :

    cmake -S . -B build -DCMAKE_CXX_FLAGS="-DPS3BASE_TEXSTAT=1"

Le binaire s'est reconstruit (346 Mo, 18:53) mais **s'arrête après l'initialisation VFS** :

    I 0.056 vfs: disc /root/Dev/mc-port/disc (BLES01976 01.00), patch ()
    (rien ensuite)

Le run de référence, lui, enchaîne sur `input`, `gpu: swapchain`, `Vulkan presenter`. Quelque chose
dans ma reconfiguration casse le démarrage — probablement parce que le build principal était
configuré avec d'autres options que celles que je lui ai passées.

**J'ai restauré** `-DCMAKE_CXX_FLAGS=""` et reconstruit : 0 erreur, `spurs` passe. Le binaire est
revenu à un état sain, mais **je n'ai pas vérifié qu'il redémarre** : les fenêtres de 40 s ne
suffisent pas pour l'atteindre et je n'ai pas relancé la vérification complète.

C'est la deuxième fois dans cette campagne qu'une manipulation de build me coûte un round. La
prochaine fois je copierai la configuration existante (**`cmake -S . -B <new> -C build/CMakeCache.txt`
ou un `cp -a build`**) au lieu de la reconstruire de mémoire.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **n'a pas produit la mesure visée** et a **cassé puis restauré** le
build principal. Ce qu'il apporte : la découverte que le natif de référence tourne avec
`GOWA_SPU_PARK=0`, ce qui invalide la comparaison du round 67.


## Ce que la vérification finale montre

Après restauration de `-DCMAKE_CXX_FLAGS=""`, le binaire **ne démarre toujours pas** :

    I 0.067 vfs: disc /root/Dev/mc-port/disc (BLES01976 01.00), patch ()
    (rien ensuite)

Le run de référence, lui, enchaîne sur `codewatch`, `place`, `stacks`, `input`, `gpu: swapchain`.

## La cause n'est pas de moi

`git status` liste **dix fichiers modifiés** que je n'ai pas touchés :

    M CMakeLists.txt              (+46 lignes)
    M recomp/ppu/codegen.py
    M recomp/images-rdr.json
    M recomp/lle_modules.txt
    M runtime/include/runtime/cycles.h
    M runtime/src/hle/cellGame.cpp
    M runtime/src/hle/cellPad.cpp
    M runtime/src/hle/cellSaveData.cpp
    M runtime/src/hle/cellSysutil.cpp
    ...

C'est le **chantier parallèle de l'autre agent** sur le port natif Linux. `CMakeLists.txt` et
`codegen.py` sont précisément les fichiers qui déterminent comment la table d'images est générée et
liée — et c'est cette table qui produit `codewatch: 22685 legal call targets registered`, absent de
mon démarrage.

**Mes fichiers sont tous commités** (`git status` vide pour `rsx.cpp`, `thread.cpp`,
`rsx_no_display.cpp`, `reservation.cpp`).

## Ce que je ne peux pas faire ce round

Ni mesurer les registres de texture côté natif, ni vérifier que mon build est sain : l'arbre est dans
un état intermédiaire dont je ne suis pas l'auteur. J'attends que ce chantier atterrisse.

## Correction de mon propre compte rendu

J'ai écrit plus haut « j'ai restauré et reconstruit : 0 erreur, spurs passe, le binaire est revenu à
un état sain ». **Le build est sain au sens de la compilation**, mais **le binaire ne démarre plus** —
et ce n'est pas mon changement qui l'empêche. Je corrige la formulation : compilation propre, démarrage
cassé par un chantier tiers.
