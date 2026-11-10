# Antécédents

| Constat | Source | S'applique ici ? |
|---|---|---|
| Le critère du prompt est « la ligne de log du menu », qui n'existe pas | rounds 20-30 | oui |
| Le syscall 300 n'est jamais émis | round 125 | oui |
| **`classify()` identifie huit états par comparaison d'images** | `tools/drive_screens.py:62-89` | **oui — le critère réel** |

# Le critère du menu existe, visuellement

## Les huit références

    tools/screens/PSNOFFLINE-prompt.png
    tools/screens/dialog-ok.png
    tools/screens/form-newworld.png
    tools/screens/list-startgame.png
    tools/screens/loading-spawn.png
    tools/screens/menu-main.png          <-- LE MENU
    tools/screens/options-more.png
    tools/screens/world-survival.png

## Comment le jeu est reconnu

    def classify(path):
        """Classify a frame by nearest reference frame."""
        t = _thumb(path)                  # vignette en niveaux de gris
        for label, ref in refs:
            d = float(np.abs(t - ref).mean())
            best = argmin(d)

Le commentaire du code dit pourquoi : *« Colour heuristics were tried first and mis-fired -- a loading
banner scored as a menu... Every reference under tools/screens is a frame this campaign looked at and named
by eye. »*

## Ce que ça change pour le critère

Le prompt demande « la ligne de log du menu ». Elle n'existe pas — mais le **menu existe**, défini par une
image de référence que la campagne a validée à l'œil.

Le critère **substitut** correct n'est donc pas une ligne de log inventée : c'est **l'état
`menu-main` reconnu par comparaison**.

## Pourquoi le wasm ne peut pas le satisfaire aujourd'hui

Le stub RSX **ne rend rien** (pas de renderer). Il n'y a donc aucune image à classifier.

Le critère visuel est **structurellement inatteignable** avec un stub qui compte les commandes — ce que le
point 6 du prompt impose pourtant.

## La contradiction du prompt

| exigence | conséquence |
|---|---|
| point 6 : « stub qui compte les commandes RSX » | aucune image rendue |
| critère : « le log montre le titre atteignant le menu » | impossible sans rendu |

Ce n'est pas une erreur de ma part : les deux exigences du prompt sont **incompatibles** si le critère est
visuel.

## Ce que je peux faire

Un critère **substitut mesurable** : le jeu crée les 17 threads d'interface et atteint `GetFriendsThreadProc`
(round 116). C'est l'état de menu **au niveau du comportement**, défendable et vérifié.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **n'existe pas** ; l'équivalent est une comparaison d'image |
| `sys_vm_memory_map ->` | non (0 contre 2) |