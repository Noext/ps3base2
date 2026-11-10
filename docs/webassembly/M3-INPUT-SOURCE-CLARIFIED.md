# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| Les appuis intégrés multiplient le dessin par 2,7 (3k/s → 8k/s) | round 114 | oui |
| `run_world.sh` référence `PS3BASE_INPUT_CMD` | `tools/run_world.sh:228` | **oui — mais le fichier reste vide** |
| `drive_world.py` utilise `xdotool keydown/keyup` | `tools/drive_world.py:52-54` | **oui — la vraie source des appuis natifs** |

# Le fichier de commandes n'était pas la source

## La correction

Le round 113 supposait que le natif recevait ses appuis par `PS3BASE_INPUT_CMD`. **C'est faux.**

Le script fait :

    ICMD="$RUN_DIR/input.cmd"
    : > "$ICMD"          # créé VIDE
    PS3BASE_INPUT_CMD=$ICMD

et **ne le remplit jamais**.

Les appuis natifs viennent de `drive_world.py`, qui envoie de **vrais événements X** :

    subprocess.run(["xdotool", "keydown", key], env=env)
    subprocess.run(["xdotool", "keyup", key], env=env)

Ceux-ci passent par SDL, puis `host::poll_pad`, puis `cellPadGetData`.

## Ce que ça change

Le wasm **ne peut pas** recevoir d'appuis par ce chemin : il n'a pas de SDL, pas de fenêtre, pas de X.

Mon injection en **constante de compilation** est donc le **seul** moyen disponible — et elle est
légitime : elle reproduit ce que `poll_pad` ferait, sans le device.

## Ce que ça valide

L'expérience du round 114 n'était pas un substitut approximatif : c'était **la seule reproduction
possible** du chemin d'entrée natif dans un build sans fenêtre.

Et elle a produit un effet mesuré (3k/s → 8k/s).

## Ce qui reste incertain

La **séquence** exacte que `drive_world.py` envoie. Il lit l'état de l'écran (`classify`) et décide
quelle touche presser — un pilotage adaptatif que je ne peux pas reproduire à l'aveugle.

Mes quatre appuis (`cross`, `cross`, `start`, `cross`) sont une approximation.

## La piste

`drive_world.py` contient la **machine à états** du parcours (termes → menu → monde). Lire son
`classify` et sa logique de décision donnerait la séquence réelle.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |