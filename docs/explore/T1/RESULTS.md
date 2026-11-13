# T1 — Rejeu sur le thread principal

Worktree `/root/Dev/t-T1` (branche `explore/tT1`), base `d41c834`.
Arbres partages lies : `bin out out-mc fw ref`. Out dir unique : `/tmp/out-T1`.

**Ce fichier est ecrit AVANT toute mesure.** Les sections 5 a 7 sont remplies au fur et a mesure,
avec des commandes et des sorties brutes, jamais une valeur decrite de memoire.

---

## 0. Antecedents (REGLE 12 — grep fait AVANT d'ecrire)

```
$ cd /root/Dev/t-T1 && grep -rln 'rsx_ring_stat' --include=*.md --include=*.h --include=*.cpp .
(0 ligne sur la base d41c834 : ni le producteur d'anneau ni le backend WebGPU ne sont dans la base)
$ git log --all --oneline --grep='ring' -i | head
ce45bd3 S2: ring carries each object once (content-fingerprint dedup) ...
d173feb wip: S2 each object crosses the ring once ...
fffe3bc wip: S1 STOPPED on user request -- measured: ring full to 16 bytes with popped=0 ...
```

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le renderer tourne sur un pthread, et un pthread a un `WebGPU.Internals` vide (`createCommandEncoder` undefined) | `docs/explore/S1/RESULTS.md` sec. 2.3 (branche `explore/sS1`, commit `fffe3bc`) | **oui** : c'est le mur que T1 doit franchir, et la raison d'etre du rejeu sur le thread principal |
| Le wasm ADOPTE un device cree par JS (`preinitializedWebGPUDevice`) dans le MEME realm | `docs/explore/S1/RESULTS.md` sec. 2.2 | **oui** : le device de T1 est cree par la page et adopte par le module |
| Un anneau sans consommateur : `used=cap-16`, `popped=0`, `refused=1995` | `docs/explore/S1/RESULTS.md` sec. 2.1 | **oui** : c'est l'etat AVANT exact que T1 doit changer |
| Producteur d'anneau + dedup par empreinte de contenu, `refused=0` mesure | `docs/explore/S2/RESULTS.md` (commit `ce45bd3`) | **oui** : T1 part de ce producteur (branche `explore/sS2`) au lieu d'en reecrire un |
| Le consommateur existant est du **JavaScript** (`web/s2-ring.js`) | `ce45bd3:web/s2-ring.js` | **non** : c'est precisement ce que T1 remplace par du C++ sur le thread principal |
| `-sPROXY_TO_PTHREAD=1` met `main()` sur un pthread, le thread principal reste libre | `tools/build_wasm.sh:88` | **oui** : c'est l'architecture A |
| `emscripten_set_main_loop` n'existe pas dans l'arbre (seulement le stub imgui) | `third_party/imgui/examples/libs/emscripten/emscripten_mainloop_stub.h:34` | **oui** : le rejeu C++ est a ecrire |

---

## 1. Ce que T1 change (et ce qu'il ne change pas)

Deux moities, comme demande :

- **enregistrement** : thread RSX (pthread) -> anneau. C'est `runtime/src/gpu/rsx_ring.cpp`
  (producteur S2, dedup par empreinte), branche par `runtime/src/lv2/rsx.cpp` au niveau du
  decodeur FIFO. **Pas reecrit.**
- **rejeu** : thread principal -> device WebGPU. **Ecrit ici**, en C++ :
  `runtime/src/gpu/rsx_replay.cpp` (consommateur de l'anneau, sans aucun appel WebGPU) et
  `runtime/src/gpu/t1_replay_main.cpp` (`emscripten_set_main_loop` -> fonction C++ -> device).

Le device est cree par la page (JS) et adopte par le wasm. **Aucun appel WebGPU depuis un
pthread** : le seul chemin qui en faisait encore (le renderer Vulkan, compile pour wasm) est
**coupe** (`PS3BASE_NO_RENDERER`), et le rejeu lit sa propre copie des registres RSX depuis les
`StateWord` de l'anneau.

---

## 2. Les compteurs

`rsx_ring_stat(i)`, champs 0..22 (dans l'ordre de `gpu::rsxring::Stats`) : 0=records,
1=bytes, 2=state_methods, 10=draws, 11=clears, 12=flips, 13=refused, 14=refused_bytes,
17=producer_threads, 19=refs, 20=distinct_objects, 21=bytes_saved, 22=shader_alternations.
`rsx_ring_control(i)` : 0=used, 1=pushed, 2=popped, 3=wrapped, 4=capacity.

Le critere porte sur `popped` (records reellement sortis de l'anneau par le consommateur
C++ du thread principal) et `recs` (= `rsx_ring_stat(0)`, records pousses).

---

## 3. Provenance (a remplir apres build)

| element | valeur |
|---|---|
| md5 game.wasm | (a mesurer) |
| chemin | `/tmp/out-T1/game.wasm` |
| commande de build | `DISK=/tmp/disk-r4 bash tools/build_wasm_t1.sh /root/Dev/t-T1 /tmp/out-T1` |
| commande de run | (a mesurer) |

---

## 4. Etat

En cours.
