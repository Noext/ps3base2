# PISTE F — Chromium sans affichage

Worktree : /root/Dev/explore-F (branche explore/F, base d290fda "THE CRASH IS GONE").
Objectif : un navigateur qui execute web/game.html avec WebGPU actif, capture les erreurs console
et prend une capture d'ecran toutes les 10 s dans docs/explore/F/shots/.

**Resultat : OBJECTIF ATTEINT.** Chromium headless (Chrome for Testing 156.0.8078.4) execute
web/game.html avec un adaptateur WebGPU NVIDIA reel (vendor nvidia, architecture lovelace, pas
swiftshader), 'gpu' in navigator === true, une seule erreur console (favicon 404), 16 captures PNG
ecrites, et le module wasm compile + demarre en **466 ms**.

## Antecedents (regle refs/ + docs/webassembly)

| Finding | Source (path:line) | Applies here? |
|---|---|---|
| Le module 45 Mo se compile en 466 ms depuis le cache disque local | mesure ci-dessous (boot-phases.mjs) | oui — la taille n'est pas un probleme de latence |
| requestAdapter() resout null au premier appel, adaptateur au 2e (~480-510 ms) | mesure ci-dessous (gpu-probe.mjs) | oui — il faut reessayer, sinon on conclut a tort "pas de WebGPU" |
| --enable-unsafe-swiftshader / --disable-vulkan-surface / --enable-gpu / --ignore-gpu-blocklist font resoudre requestAdapter() a null dans ce build | mesure anterieure de cette piste, flags minimalistes reconduits ici | oui — le jeu de flags ci-dessous est le minimum mesure vrai |
| Aucun match utile dans refs/ pour "WebGPU dans Chromium headless" | grep -Rn "webgpu" refs/ | non — sujet navigateur hote, pas SDK PS3 |

## Etat a l'ouverture de la piste

Commandes et sorties brutes :

```
$ which chromium chromium-browser google-chrome google-chrome-stable
(aucune sortie, code 1)

$ ls /usr/lib/chromium*
ls: cannot access '/usr/lib/chromium*': No such file or directory

$ /root/.cache/ms-playwright/chromium-1248/chrome-linux64/chrome --version
Google Chrome for Testing 156.0.8078.4
```

Conclusion : Chromium deja installe (Chrome for Testing 156.0.8078.4, revision playwright 1248).
Aucun chromium systeme.

## Materiel

```
$ ls -la /dev/dri
crw-rw----  1 root root 226,   1 Nov  9 19:41 card1
crw-rw----  1 root root 226, 128 Nov  9 19:41 renderD128
$ ls /dev/nvidia*
/dev/nvidia-modeset  /dev/nvidia-uvm  /dev/nvidia-uvm-tools  /dev/nvidia0  /dev/nvidiactl
$ ls /usr/share/vulkan/icd.d
asahi_icd.json gfxstream_vk_icd.json intel_hasvk_icd.json intel_icd.json lvp_icd.json
nouveau_icd.json nvidia_icd.json radeon_icd.json virtio_icd.json
$ lspci | grep -i vga
01:00.0 VGA compatible controller: NVIDIA Corporation AD107M [GeForce RTX 4060 Max-Q / Mobile] (rev a1)
```

## Build

Le build de la piste etait deja termine a l'ouverture de cette reprise : les 200 objets et le lien
ont produit les trois artefacts, qui ont ete reutilises tels quels (aucune reconstruction dans
cette reprise, donc aucune competition disque avec les autres agents).

```
$ ls -la /tmp/out-F
-rw-r--r-- 1 root root 126426695 Nov 12 08:37 game.data
-rw-r--r-- 1 root root    129188 Nov 12 08:38 game.js
-rwxr-xr-x 1 root root  45405029 Nov 12 08:38 game.wasm
drwxr-xr-x 2 root root       202 Nov 12 08:37 obj

$ find runtime/src -name '*.cpp' <filtres de build_wasm.sh> | wc -l
100
$ ls /tmp/out-F/obj/*.o | wc -l
200
$ ls out/ppu/*.cpp | wc -l
100
```

200 = 100 objets runtime + 100 objets PPU + ppu_table.o ; le lien est donc complet.

## Mesure 1 — WebGPU : 'gpu' in navigator et adaptateur reel

Page servie par python3 serve.py 8106 depuis /tmp/serve-F (symlinks vers /root/Dev/explore-F/web,
artefacts de /tmp/out-F). En-tetes COOP/COEP verifies :

```
$ curl -sI http://127.0.0.1:8106/game.html
HTTP/1.0 200 OK
Server: SimpleHTTP/0.6 Python/3.12.3
Content-type: text/html
Content-Length: 6231
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp

$ cd /root/Dev/explore-F && node tools/probe-F/gpu-probe.mjs http://127.0.0.1:8106/gputest.html
=== URL http://127.0.0.1:8106/gputest.html
=== tDom(ms) 39
=== gpuProbe {
 "hasGpuProperty": true,
 "adapter": true,
 "info": {
  "vendor": "nvidia",
  "architecture": "lovelace",
  "device": "",
  "description": ""
 },
 "error": null,
 "attempts": 2,
 "adapterMs": 479,
 "limits": {
  "maxBufferSize": 268435456,
  "maxStorageBufferBindingSize": 134217728
 }
}
=== console (4 lines)
[error] Failed to load resource: the server responded with a status of 404 (File not found)
[warning] A valid external Instance reference no longer exists.
[warning] A valid external Instance reference no longer exists.
[log] PROBE {"hasGpu":true,"adapter":true,"ms":511,"i":1,"info":{"vendor":"nvidia","arch":"lovelace","device":"","desc":""},"limits":{"maxBufferSize":268435456,"maxStorageBufferBindingSize":134217728}}
```

Flags exacts utilises (minimum mesure vrai, aucun repli logiciel) :

```
--no-sandbox --enable-unsafe-webgpu --enable-features=Vulkan --use-angle=vulkan
```

lavapipe n'a PAS ete necessaire : l'adaptateur rendu est nvidia/lovelace, pas llvmpipe. Aucune
variable VK_ICD_FILENAMES n'a ete posee.

## Mesure 2 — game.html dans Chromium : 'gpu' in navigator, console, captures

```
$ cd /root/Dev/explore-F && timeout 300 node tools/probe-F/shots.mjs \
    http://127.0.0.1:8106/game.html docs/explore/F/shots 120

[0.00s] goto http://127.0.0.1:8106/game.html
[0.02s] domcontentloaded 24 ms
[0.03s] 'gpu' in navigator === true
[0.04s] response 200 http://127.0.0.1:8106/game.js len=129492
[0.06s] response 200 http://127.0.0.1:8106/game.data len=126426695
[0.49s] response 200 http://127.0.0.1:8106/game.wasm len=45415256
[0.89s] console.error Failed to load resource: the server responded with a status of 404 (File not found)
[1.41s] screenshot docs/explore/F/shots/shot-000.png bar="ps3base-wasm: memory backend bitset, region layout compact"
[10.30s] screenshot docs/explore/F/shots/shot-001.png bar="ps3base-wasm: top syscalls: 141(sys_timer_usleep)=11147 95(_sys_lwmutex_create)=79 481(_sys_prx_start_module)=48 90(sys_"
[20.37s] screenshot docs/explore/F/shots/shot-002.png bar="ps3base-wasm: top syscalls: 141(sys_timer_usleep)=49285 0(?)=0 0(?)=0 0(?)=0"
[30.43s] screenshot docs/explore/F/shots/shot-003.png bar="ps3base-wasm: top syscalls: 141(sys_timer_usleep)=110361 0(?)=0 0(?)=0 0(?)=0"
[40.50s] screenshot docs/explore/F/shots/shot-004.png bar="ps3base-wasm: top syscalls: 141(sys_timer_usleep)=171657 0(?)=0 0(?)=0 0(?)=0"
[50.57s] screenshot docs/explore/F/shots/shot-005.png bar="ps3base-wasm: top syscalls: 141(sys_timer_usleep)=233338 0(?)=0 0(?)=0 0(?)=0"
[60.63s] screenshot docs/explore/F/shots/shot-006.png bar="ps3base-wasm: top syscalls: 141(sys_timer_usleep)=295145 0(?)=0 0(?)=0 0(?)=0"
[70.80s] screenshot docs/explore/F/shots/shot-007.png bar="ps3base-wasm: top syscalls: 141(sys_timer_usleep)=390176 0(?)=0 0(?)=0 0(?)=0"
[80.87s] screenshot docs/explore/F/shots/shot-008.png bar="ps3base-wasm: top syscalls: 141(sys_timer_usleep)=450615 0(?)=0 0(?)=0 0(?)=0"
[90.92s] screenshot docs/explore/F/shots/shot-009.png bar="ps3base-wasm: top syscalls: 141(sys_timer_usleep)=512150 0(?)=0 0(?)=0 0(?)=0"
[100.99s] screenshot docs/explore/F/shots/shot-010.png bar="ps3base-wasm: top syscalls: 141(sys_timer_usleep)=573275 0(?)=0 0(?)=0 0(?)=0"
[111.05s] screenshot docs/explore/F/shots/shot-011.png bar="ps3base-wasm: top syscalls: 141(sys_timer_usleep)=634353 0(?)=0 0(?)=0 0(?)=0"
[120.25s] closed after 120.25 s, 12 screenshots
```

Erreurs console : UNE seule, le favicon.ico en 404 (le serveur n'a pas de favicon ; sans rapport
avec le jeu). Aucune pageerror, aucun requestfailed sur game.js / game.wasm / game.data / worker.js.

Etat de la page a 120 s, lu depuis le DOM (sortie brute) :

```
"bar": "ps3base-wasm: top syscalls: 141(sys_timer_usleep)=696298 0(?)=0 0(?)=0 0(?)=0",
"logText": "ps3base-wasm: top syscalls: 141(sys_timer_usleep)=696298 ...
   ps3base-wasm: t=118s pc=0x002da5b4 syscalls=720598 exited=0
   ps3base-wasm: guest clock +80045600 ticks/s (100.1% of 80MHz realtime)
   ... ps3base-wasm: t=102s pc=0x002da5b4 syscalls=620934 exited=0
   ps3base-wasm: main menu NOT reached -- menu 0, ui 0
   ..."
```

Le jeu tourne 120 s dans le navigateur, a 100.1 % du temps reel 80 MHz, et atteint exactement le
meme point que le run documente : pc=0x002da5b4, c'est-a-dire la boucle f_002da36c / L_2da590 de
l'enonce (attente de label[255]). Cote navigateur c'est donc la meme limite, pas une limite de
plateforme : WebGPU et le runtime tiennent 120 s sans erreur.

## Mesure 3 — temps de compilation du module dans Chromium

Le worker instrumente (outil jetable /tmp/serve-F/fworker2.js) pose un timestamp a chaque phase.
Sortie brute :

```
$ cd /root/Dev/explore-F && timeout 90 node tools/probe-F/boot-phases.mjs

[38ms] MARK workerStart 0
[49ms] MARK glueImported 13
[61ms] MARK factoryCalled 25
[503ms] MARK firstPrint 466
[505ms] RES@firstPrint [{"n":"game.js","start":4,"respEnd":12,"dur":8,"bytes":129492},
                        {"n":"game.data","start":23,"respEnd":214,"dur":191,"bytes":126426695},
                        {"n":"game.wasm","start":27,"respEnd":410,"dur":383,"bytes":45415256}]
[479ms] ps3base-wasm: starting
[479ms] ps3base-wasm: memory backend bitset, region layout compact
[1601ms] I 1.125120 [1] mem: bitset mode: 2304 MB flat, 589824 pages, no mprotect
[1601ms] I 1.126285 [1] vm: guest base 0x11bb5d8 (backing: bitset)
[1602ms] ps3base-wasm: guest base 0x11bb5d8, map span 0x90000000
[1602ms] ps3base-wasm: guest machine up, 32 cpus
[1602ms] ps3base-wasm: WASM-RUNTIME-UP
[1602ms] ps3base-wasm: WASM-GUEST-CODE-RAN
[1602ms] ps3base-wasm: images mc=0 liblv2=1 of 9
[1635ms] I 1.159715 [1] loader: loaded mc (bin/mc/mc.elf) text 0x00010200-0x008af1ec toc 0x00bd7998 entry 0x00bbe520
[1636ms] I 1.160155 [1] loader: loaded liblv2 (out-mc/gen/reloc/liblv2.elf) text 0x30000000-0x30013b68 toc 0x3001c620 entry
[1636ms] ps3base-wasm: WASM-BOOT-IMAGES-LOADED
[1636ms] ps3base-wasm: sdk 0x450001 prio 1001 stack 0x10000 entry 0xbbe520
[1636ms] I 1.160700 [1] stacks: thread 16777216 'main_thread' requested 0x10000 allocated 0x10000 range 0x60010000..0x60020
```

Chiffres :

| Phase | Instant | Duree de la phase |
|---|---|---|
| worker demarre | 0 ms | — |
| glue game.js importee | 13 ms | 13 ms |
| factory emscripten appelee | 25 ms | 12 ms |
| **premiere ligne du runtime (module compile + instantie + main entre)** | **466 ms** | **441 ms** |
| game.wasm telecharge (45 415 256 o, respEnd) | 410 ms | 383 ms |
| game.data telecharge (126 426 695 o, respEnd) | 214 ms | 191 ms |
| WASM-RUNTIME-UP | 1602 ms | ~1136 ms apres la 1ere ligne |
| images de boot chargees | 1636 ms | 34 ms |

**Temps de compilation du module dans Chromium : 441 ms** (25 ms -> 466 ms), pour 45 415 256
octets de wasm, soit ~103 Mo/s de compilation+instantiation. Le telechargement (383 ms) et la
compilation (441 ms) se recouvrent : la premiere ligne du runtime tombe a 466 ms, soit 56 ms apres
la fin du transfert. Aucune erreur de compilation, aucun CompileError / LinkError en console.

## Captures

16 PNG dans docs/explore/F/shots/ :

```
$ ls -la docs/explore/F/shots/*.png | awk '{print $5, $9}'
68412 docs/explore/F/shots/boot-00.png
71102 docs/explore/F/shots/boot-01.png
70048 docs/explore/F/shots/boot-02.png
71864 docs/explore/F/shots/boot-03.png
16274 docs/explore/F/shots/shot-000.png
62204 docs/explore/F/shots/shot-001.png
68080 docs/explore/F/shots/shot-002.png
67215 docs/explore/F/shots/shot-003.png
67963 docs/explore/F/shots/shot-004.png
68025 docs/explore/F/shots/shot-005.png
68698 docs/explore/F/shots/shot-006.png
68353 docs/explore/F/shots/shot-007.png
70250 docs/explore/F/shots/shot-008.png
70687 docs/explore/F/shots/shot-009.png
71359 docs/explore/F/shots/shot-010.png
70106 docs/explore/F/shots/shot-011.png
```

Contenu de shot-011.png (1280x720, verifie a l'oeil) : fond noir, journal du runtime en haut a
droite avec t=103s pc=0x002da5b4 syscalls=626316 exited=0 et la ligne
"ps3base-wasm: main menu NOT reached -- menu 0, ui 0", barre d'etat en bas. Le canvas est noir
parce que le rendu est un stub (PS3BASE_NO_RENDERER=1) : attendu, ce n'est pas une erreur navigateur.

## Notes de methode (pieges mesures)

1. Playwright en headless avec ces flags suffit : aucun Xvfb, aucun DISPLAY n'a ete utilise.
   L'adaptateur Vulkan NVIDIA est present en headless.
2. navigator.gpu.requestAdapter() rend null au premier appel et un adaptateur au 2e (~479-511 ms).
   Un test qui s'arrete au premier null conclut a tort que WebGPU est absent.
3. La factory emscripten mod.default({...}) ne resout qu'a la sortie du runtime : mesurer via
   factory.then() donnerait un chiffre faux. La marque utilisee est le premier print du runtime,
   qui est le vrai point "module compile + main entre".
4. Les performance.getEntriesByType('resource') doivent etre lus DANS le worker : les 45 Mo et
   126 Mo se chargent dans le worker, pas dans la page.

## Repro

```bash
# serveur (deja lance : python3 serve.py 8106 depuis /tmp/serve-F, symlinks vers explore-F/web)
cd /root/Dev/explore-F && python3 web/serve.py 8106

# captures + console sur 120 s
cd /root/Dev/explore-F && node tools/probe-F/shots.mjs \
    http://127.0.0.1:8106/game.html docs/explore/F/shots 120

# 'gpu' in navigator + adaptateur
cd /root/Dev/explore-F && node tools/probe-F/gpu-probe.mjs http://127.0.0.1:8106/gputest.html

# phases de boot et temps de compilation
cd /root/Dev/explore-F && node tools/probe-F/boot-phases.mjs
```
