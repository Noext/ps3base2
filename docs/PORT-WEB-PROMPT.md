# Mission: ps3base en WebAssembly + WebGPU

Ce prompt est auto-portant. Il se lit seul, sans la conversation qui l'a produit.

## Contexte

Dépôt : `/root/Dev/ps3base2` (branche `main`). C'est un fork de GoWA-pc qui fait
tourner Minecraft PS3 (BLES01976) en **récompilation statique** : le code PPU du
jeu est traduit en C++ par `recomp/ppu/codegen.py` vers `out/ppu/` (4,33 M de
lignes, 22 685 fonctions), les blocs SPU vers `out/spu/` (49 474 blocs), et un
runtime C++ fournit le noyau LV2, le RSX par-dessus Vulkan et les HLE.

État actuel : le jeu rend le monde, joue une heure sans crash sous garde, et
**99,9 % du code SPU est compilé à l'avance** (mesuré, voir
`docs/findings/spu-generation.md`). Un crash PPU/HLE connu subsiste
(`f_0064c044`, corruption du champ `+28` d'un nœud).

Objectif : porter ce runtime dans un navigateur (WebAssembly + WebGPU), sur le
modèle de `/root/Dev/ps2-web-recomp` — qui n'est **pas** dans `refs/`.

L'étude de faisabilité complète est dans `docs/findings/webassembly-feasibility.md`.
**Lis-la d'abord, puis lis `/root/Dev/ps2-web-recomp/README.md`.** Ce prompt résume
leurs conclusions mais ne les remplace pas.

## Contraintes dures, héritées de ce dépôt

1. **clang/clang++ uniquement, RelWithDebInfo uniquement.** gcc casse
   `runtime/src/spu/edge/spu_float.h` sur un macro `slow`.
2. **Builds plafonnés à `-j 8`.** La machine est partagée ; `-j 16` la rend
   inutilisable pour l'opérateur.
3. **Un seul run du jeu à la fois.** Lancer plusieurs instances fausse les mesures.
4. **Un enfant lancé depuis un appel d'outil est tué quand l'appel rend la main**
   (`gpu: window closed, exiting`, zéro crash). Seul
   `nohup setsid <cmd> & disown` survit. Ce piège a coûté plusieurs tours.
5. **Ne jamais tuer par nom d'image.** `pkill -9 -x ps3base` tue les runs des
   autres. Toujours `kill -9 -- -$GAME_PID`.
6. **Toute affirmation doit être mesurée.** Une capture, une ligne de log, ou un
   chiffre. Pas de « ça devrait marcher ». Si une mesure contredit une hypothèse,
   l'hypothèse est retractée explicitement.

## Vérifications existantes à réutiliser (ne pas réinventer)

```bash
cmake --build build -j 8 --target ps3base          # doit afficher 0 erreur
./build/memory_map            # 22 checks
./build/spu_aot_variants      # 9 checks
./build/reservation_increment # 8 checks
./build/list_walk_guard       # 6 checks
./build/lwmutex_hle           # 5 checks
./build/reservation_xproc     # 4 checks
./build/lock_counter          # 3 checks
./build/lwmutex_contention    # 3 checks
# guest_spinlock et lv2_enosys n'impriment pas de compte : se fier au code de sortie
```

Outils de non-régression fonctionnelle :

| outil | ce qu'il prouve |
|---|---|
| `tools/drive_world.py <display> <log> [timeout] [shots]` | pilote par lecture d'écran jusqu'à un monde ; imprime `WORLD-MAPPED` |
| `tools/run_world.sh <vfs> --seconds N` | lance une session complète avec verdict mécanique |
| `tools/play.sh` | lancement utilisateur, GPU réel |
| `tools/compare_worlds.py <a> <b>` | compare deux sauvegardes, **refuse si les graines diffèrent** |
| `tools/spu_converge.sh` | boucle STRICT -> collecte -> régénère -> rebuild |
| `tools/spu_speed.sh` | mesure de vitesse contrôlée, refuse si la machine n'est pas au repos |

Deux règles apprises à la dure :
- **`tools/drive_world.py` est le seul pilotage fiable.** Il lit l'anneau de focus
  et refuse d'appuyer s'il n'est pas sur le bon widget. Ne jamais compter des
  appuis : le formulaire de création enveloppe, et le champ Seed avale les flèches.
- **`vm_map=1` ne prouve qu'une allocation.** Le signe « je suis dans le monde »
  est `UserAutoSave >= 1` **plus** une capture avec cœurs et hotbar.

## Ordre des jalons — ne pas sauter, ne pas réordonner

Chaque jalon a un **critère de validation** qui doit passer avant d'ouvrir le
suivant. Si un critère échoue, on corrige ou on retracte, on ne continue pas.

---

### Jalon 1 — Runtime sans `mprotect` ni signaux (vérifié en natif)

**Pourquoi d'abord :** c'est le seul jalon indépendamment utile, il retire deux
des six dépendances non portables, et il se valide avec les tests existants, sans
navigateur. Commencer par le wasm signifie déboguer en même temps le modèle
mémoire, les threads, les shaders et la chaîne d'outils.

**Travail :**

1. Abstraire la couche mémoire derrière une interface
   (`runtime/include/runtime/memory.h` expose déjà `init/map/commit/is_mapped/
   range_mapped/mark_immutable`). Aujourd'hui l'implémentation appelle
   directement `mmap`/`mprotect` dans `runtime/src/host.cpp:107-111` (Windows)
   et `host.cpp:267-281` (POSIX), et `syscall(SYS_futex, …)` en
   `host.cpp:261-265`.
2. Fournir **deux** implémentations derrière cette interface :
   - `os` : l'actuelle, inchangée ;
   - `bitset` : aucune protection système, un bitmap de pages engagées consulté
     par `is_mapped`/`range_mapped`, et un buffer plat.
   Sélection par variable d'environnement, `PS3BASE_MEMORY=os|bitset`.
3. Compiler hors du binaire les rapporteurs de crash : `runtime/src/crash.cpp` et
   `runtime/src/crash_posix.cpp` installent `SIGSEGV/SIGBUS/SIGILL/SIGFPE`. Le
   web n'a pas de signaux. Le chemin `bitset` ne doit pas les référencer.
4. Porter `host::` futex sur `emscripten_futex_wait/wake` **derrière la même
   interface**, sans changer le comportement natif.
5. Les gardes de pile ajoutées dans `runtime/src/stacks.cpp` (64 Ko `PROT_NONE`
   sous chaque pile invitée) deviennent, en mode `bitset`, un **contrôle logiciel
   de plage** qui logge thread, r1, lr et la fonction liftée — même information,
   sans faute système.

**Validation (toutes obligatoires) :**

- `cmake --build build -j 8 --target ps3base` → 0 erreur.
- Les 8 suites de tests ci-dessus passent, **en mode `os` et en mode `bitset`**.
- `PS3BASE_MEMORY=bitset tools/run_world.sh vfs-j1 --seconds 600` atteint
  `vm_map=1`, `UserAutoSave>=1`, 0 crash, et une capture avec cœurs + hotbar.
- `PS3BASE_MEMORY=os` donne le même résultat (pas de régression).
- `grep -c 'sigaction\|SYS_futex' ` sur le binaire en mode `bitset` : les
  symboles ne sont plus référencés par le chemin d'exécution.

**Preuve à rendre :** les deux logs de run, les deux captures, le compte de tests
dans chaque mode.

---

### Jalon 2 — Backend WebGPU natif

**Pourquoi :** prouve la traduction d'API sans navigateur et permet d'écrire les
shaders une seule fois en WGSL. `ps2-web-recomp` fait exactement ça : son
`host/window.cpp` partage le chemin wgpu entre natif et web.

**Travail :**

1. Choisir **wgpu-native** (recommandé : `ps2-web-recomp` l'utilise déjà et son
   script le télécharge) ou Dawn.
2. Le renderer à porter est `runtime/src/gpu/rsx_renderer.cpp` (4 626 lignes,
   **100 points d'entrée Vulkan** distincts) plus `presenter.cpp` (1 126).
3. Traduire les 28 `VK_FORMAT_*` utilisés. Trois n'ont pas d'équivalent direct et
   demandent une décision explicite documentée :
   - `A1R5G5B5_UNORM_PACK16` → pas de format 16 bits RGBA en WebGPU, expansion
     vers `rgba8unorm` (2× la bande passante) ;
   - `BC1/BC2/BC3` → conditionnés à la feature `texture-compression-bc` ;
   - les facteurs de mélange `CONSTANT_COLOR`/`CONSTANT_ALPHA` et leurs
     `ONE_MINUS` (4 des 15 utilisés) → **pas d'équivalent**, émulation nécessaire.
4. WebGPU n'a pas d'`dynamicState` (2 états dynamiques aujourd'hui). Il faut un
   **cache de pipelines indexé par état** (blend/depth/format). RSX change d'état
   par draw : c'est le coût principal de ce jalon, à mesurer.
5. Remplacer la compilation de shaders à l'exécution
   (`runtime/src/gpu/shader_compiler.cpp`, glslang → SPIR-V) par une génération
   **WGSL** au moment du port, avec cache disque.

**Validation :**

- Le binaire natif démarre, atteint le menu, puis un monde, avec le backend WebGPU.
- `tools/drive_world.py` rapporte `WORLD-MAPPED`.
- Une capture montre cœurs + hotbar (le critère structurel, pas des pixels sombres).
- Comparer une image du backend Vulkan et du backend WebGPU sur le **même point
  de spawn** : documenter les différences visuelles, ne pas les ignorer.
- Compter et rapporter : nombre de pipelines créés sur 60 s de jeu, temps CPU du
  thread de rendu.

**Preuve à rendre :** capture, `WORLD-MAPPED`, tableau des formats avec la
décision, nombre de pipelines.

---

### Jalon 3 — Build WebAssembly sans affichage jusqu'au menu

**C'est la porte. C'est ici que le projet peut échouer.**

**Travail :**

1. Décider et **documenter** la stratégie mémoire. L'espace invité fait exactement
   4 Gio par construction (`g_base + 0x100000000ull`) ; mesuré en jeu, seulement
   **98,6 Mo sur 213 Mo déclarés** sont réellement utilisés.
   - **Option A, Memory64** (`-sMEMORY64`) : carte plate fidèle, aucun changement
     de code, mais plancher navigateur Chrome 133+ et pointeurs doublés.
   - **Option B, wasm32 + traduction d'adresses** : fonctionne partout, mais un
     lookup sur le chemin le plus chaud du runtime.
   Choisir explicitement, écrire le choix et sa justification.
2. Compiler PPU + SPU + LV2 avec `emcc` (Emscripten 6.0.11 est déjà dans
   `/root/Dev/ps2-web-recomp/tools/emsdk`). **Ne pas** inclure le rendu.
3. Options de compilation **vérifiées nécessaires** : `-msimd128 -msse4.1 -mfma
   -std=c++20`. Sans `-msse4.1 -mfma`, `__m128`/`__m128i` et les 22 intrinsics
   SSE utilisés ne compilent pas. Ils compilent **tous** avec.
4. Threads : `-sPTHREAD_POOL_SIZE` est fixe, or le titre crée un thread hôte par
   thread invité. Si le pool ne suffit pas, appliquer la technique de
   `ps2-web-recomp` (dérouler la pile hôte au changement de thread) **pour les
   threads SPU au minimum**, et documenter.
5. Fichiers : `IDBFS` pour les sauvegardes, **HTTP Range** pour le disque.
6. Le chemin d'affichage est remplacé par un stub qui compte les commandes RSX.

**Validation :**

- Le module `.wasm` se construit ; **rendre sa taille, le temps de compilation
  total, et le pic RSS**.
- Rendre le nombre de fonctions produites, et **la plus grosse fonction générée
  en octets** — c'est le risque que V8 refuse d'optimiser les grosses fonctions,
  non mesuré à ce jour.
- Sous Node (pas de navigateur nécessaire), le module boote et le log montre le
  titre **atteignant le menu principal**. Critère mécanique : la ligne de log du
  menu, pas une capture.
- Seuils d'alerte : si le module dépasse ~80 Mo ou une fonction dépasse ~1 Mo de
  code, s'arrêter et rapporter avant de continuer.

**Preuve à rendre :** taille du module, temps, RSS, nombre et taille max de
fonction, la ligne de log du menu.

---

### Jalon 4 — Page web complète

**Travail (suivre la forme de `ps2-web-recomp`) :**

1. Émulation dans un **worker** ; la page possède le device WebGPU.
2. **Tampon circulaire** de commandes RSX du worker vers le thread de page —
   `runtime/src/gpu/ring.cpp` existe déjà côté Vulkan, à adapter.
3. **AudioWorklet** alimenté par mémoire partagée.
4. Entrées : clavier + Gamepad API. Le pad virtuel de `tools/play.sh` n'a pas
   d'équivalent web et sera abandonné.
5. Note de conception à respecter : `ps2-web-recomp` documente que les
   `setTimeout` imbriqués sont bridés à 4 ms et que la boucle doit céder via un
   `MessageChannel`. Ne pas réinventer.

**Validation :**

- La page charge dans Chrome (Linux : `chrome://flags/#enable-unsafe-webgpu` et
  `#enable-vulkan`, puis redémarrage).
- Atteint le menu, puis un monde créé.
- Une capture de la page montre cœurs + hotbar.
- Le son sort (ou une raison mesurée de son absence, comme l'absence de
  périphérique audio dans cet environnement).

**Preuve à rendre :** capture de la page dans le monde, version de Chrome, flags
utilisés.

---

### Jalon 5 — Performance

**Travail :** mesurer, ne pas optimiser à l'aveugle.

1. fps dans le monde, wasm vs natif, sur le même point de spawn.
2. Profil du worker : où va le temps (émulation vs sérialisation vs attente GPU).
3. Comparer avec le profil natif existant (`docs/findings/profile-generation.md`)
   en gardant en tête que **ce profil a été pris sous Xvfb** : son thread
   `Window` à 67 % est le coût d'Xvfb, pas celui de l'émulateur. Ne pas le citer
   comme référence de performance de l'émulateur.

**Validation :** fps mesurés, profil, et une liste de goulots nommés.

---

## Ce qu'il ne faut pas faire

- **Ne pas modifier le runtime pour ce jalon autrement qu'en ajoutant des
  implémentations derrière une interface.** Le chemin natif actuel doit continuer
  à passer tous les tests à chaque étape.
- **Ne pas toucher `out/` à la main** : il est régénéré par `tools/setup.py`.
- **Ne pas prétendre qu'un jalon est passé sans sa preuve.** Le critère de chaque
  jalon est mécanique exprès.
- **Ne pas réordonner.** Le jalon 3 commence seulement quand 1 et 2 sont validés.

## Livrable attendu à chaque fin de jalon

Un commit, et un paragraphe dans `docs/findings/webassembly-port.md` avec :
ce qui a été fait, **la preuve mesurée**, ce qui a échoué et a été retracté, et ce
qui reste ouvert. Si un jalon révèle que la faisabilité est fausse, **le dire** :
un « non » argumenté vaut mieux qu'un jalon marqué vert sans preuve.

## Estimation de référence

De l'étude : 6 à 9 semaines avec Memory64, 8 à 12 en wasm32 avec traduction
d'adresses. Jalons 1 et 2 bien faits : 3 à 5 jours. Jalon 3 : 2 à 3 semaines, et
c'est là que le projet peut échouer. Jalons 4 et 5 : 2 à 3 semaines.
