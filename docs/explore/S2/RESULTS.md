# S2 — Anneau : chaque objet une seule fois, refused = 0

Worktree `/root/Dev/s-S2` (branche `explore/sS2`), base `083bdbe` + fusion de `explore/rR3`
(le producteur `rsx_ring.cpp`, `vk_null.cpp`, `gpu_wasm_support.cpp`, `wgsl_emit.cpp`).
Arbres partages lies : `bin out out-mc fw ref`. Out dir unique : `/tmp/out-S2`.

**Ce fichier est ecrit AVANT les mesures.** Chaque section porte la commande exacte et la sortie
brute collee. Aucune valeur fabriquee. Un SKIP ne compte pas comme vert.

---

## 0. Antecedents (REGLE 12 — grep fait AVANT d'ecrire)

| Finding | Source (path:line) | Applies here? |
|---|---|---|
| L'anneau porte tout, 7 kinds, et refuse quand il est plein | `explore/iH4` `runtime/src/gpu/rsx_ring.cpp:186` (`push_record`) | **oui** : c'est le point a modifier, pas a reecrire |
| La dedup actuelle est PAR ADRESSE et par unite, seulement entre deux ecritures consecutives | `explore/rR3` `runtime/src/gpu/rsx_ring.cpp:388` (`g_tex_key[unit]`) | **oui mais insuffisante** : elle ne deduit pas par CONTENU, et un shader alterne entre deux programmes est renvoye a chaque alternance |
| 3,51 Go / 56 183 enregistrements sur un run avec consommateur, refused=0 | `explore/iH4` doc H4 (mesure tee) | **oui** : le debit reel est de l'ordre du Go ; le refus observe en R3 vient de l'absence de consommateur |
| `refused=1160458`, 16,4 Mo de textures, 26 draws, 13 flips | `docs/explore/R0TER-ETAT.md:94`, `docs/explore/R3/RESULTS.md` sec. 3 | **oui** : c'est la ligne de base a reproduire avant de modifier |
| Le device WebGPU vit dans la PAGE, jamais dans le wasm | `docs/explore/R3/RESULTS.md` sec. 2 | **oui** : la mesure "page" se fait donc en page, pas dans le worker |
| `-sSTACK_SIZE=8388608` exige par l'emetteur WGSL | `docs/explore/R3/RESULTS.md` sec. 5.3 | **oui** : sans lui le module meurt avant la boucle |
| Aucun depot de `refs/` ne contient de file d'objets dedupliquee cote RSX | `grep -Rn "dedup" refs/` = a verifier ci-dessous | a remplir par la mesure |

    $ grep -Rn "dedup" refs/rpcs3/refs/rpcs3/Emu/RSX 2>/dev/null | head -5
    (voir section 0.1)

---

(les sections 1..n sont ajoutees au fur et a mesure des mesures, commande + sortie brute)
