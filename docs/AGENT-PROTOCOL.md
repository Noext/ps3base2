# Protocole des sous-agents

## Obligatoire des la premiere minute
1. Ton propre worktree : `git worktree add /root/Dev/explore-<tache> <base>`, branche `explore/<tache>`.
2. Ton propre repertoire de sortie : `bash tools/build_wasm.sh . /tmp/out-<tache>` — JAMAIS /tmp/wasm-out (partage).
3. Ton propre port : 8100+<numero>, jamais 8000.
4. `RESULTS.md` dans `docs/explore/<tache>/` cree AVANT toute mesure, complete a chaque mesure avec la
   preuve brute collee (commande + sortie, pas de resume).

## A chaque mesure
- commit `wip: <tache> <ce qui a ete mesure>`
- mise a jour de `.agent-status/<tache>.json` :
  {"task":"<tache>","state":"running|blocked|done","last_measure":"...","evidence":"chemin","blocker":null}
- a la fin : `git rm current_tasks/<tache>.claim`

## Interdits
- Aucune valeur fabriquee pour faire avancer le jeu. Si une mesure n'a pas ete prise, RESULTS.md dit
  "non mesure" et pourquoi.
- Aucune question, aucun rapport intermediaire : tu ecris dans RESULTS.md et tu continues.
- Ne jamais tuer un processus par nom d'image. Uniquement par PID que tu as lance.
- Builds a -j 8 maximum.
- Tu ne modifies QUE les fichiers de ta piste. Un besoin hors perimetre va dans RESULTS.md section Demandes.
